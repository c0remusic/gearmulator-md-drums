#include "mdLibraryView.h"

#include "mdController.h"

#include "mdLib/mdmachines.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

namespace mdJucePlugin
{
	namespace
	{
		std::string number(const unsigned _value)
		{
			return (_value < 10 ? "0" : "") + std::to_string(_value);
		}

		std::string patternName(const uint8_t _slot)
		{
			return std::string(1, static_cast<char>('A' + _slot / 16)) + number(_slot % 16 + 1u);
		}

		void setText(Rml::Element* _element, std::string& _shown, std::string _text)
		{
			if(!_element || _text == _shown)
				return;
			_element->SetInnerRML(Rml::StringUtilities::EncodeRml(_text));
			_shown = std::move(_text);
		}

		uint64_t copyKey(const Controller::PatternCopyState& _copy)
		{
			return uint64_t{static_cast<uint8_t>(_copy.state)} | uint64_t{_copy.from} << 8 | uint64_t{_copy.to} << 16
				| uint64_t{_copy.serial} << 24;
		}
	}

	LibraryView::LibraryView(Controller& _controller, const md::MachineModel _model, Rml::Element& _document)
		: m_controller(_controller)
		, m_model(_model)
	{
		m_root = _document.GetElementById("mdEdPageLibrary");
		m_info = _document.GetElementById("mdLibInfo");
		m_read = _document.GetElementById("mdLibRead");
		m_detail = _document.GetElementById("mdLibDetail");
		m_patternDetail = _document.GetElementById("mdLibPatternDetail");
		const auto kits = m_controller.getKitLibrarySize();
		for(uint8_t slot = 0; slot < kits; ++slot)
		{
			auto* kit = _document.GetElementById("mdLibKit" + std::to_string(slot));
			m_kits.push_back(kit);
			if(kit)
				juceRmlUi::EventListener::Add(kit, Rml::EventId::Click, [this, slot](Rml::Event&) { select(slot); });
		}
		for(uint8_t slot = 0; slot < Controller::PatternLibrarySize; ++slot)
		{
			auto* pattern = _document.GetElementById("mdLibPattern" + std::to_string(slot));
			m_patterns.push_back(pattern);
			if(pattern)
				juceRmlUi::EventListener::Add(pattern, Rml::EventId::Click, [this, slot](Rml::Event&) { selectPattern(slot); });
		}
		// COPIER takes the pattern clicked, COLLER copies it onto the one clicked then
		m_copy = _document.GetElementById("mdLibCopy");
		if(m_copy)
		{
			juceRmlUi::EventListener::Add(m_copy, Rml::EventId::Click, [this](Rml::Event&)
			{
				if(m_selectedPattern >= m_patterns.size())
					return;
				m_copySource = m_selectedPattern;
				m_pasteArmedTo = 0xff;
				m_shownRevision = ~uint64_t{0};
			});
		}
		m_paste = _document.GetElementById("mdLibPaste");
		if(m_paste)
			juceRmlUi::EventListener::Add(m_paste, Rml::EventId::Click, [this](Rml::Event&) { paste(); });
		const auto tracks = _model == md::MachineModel::Monomachine ? 6 : 16;
		for(int track = 0; track < tracks; ++track)
			m_machines.push_back(_document.GetElementById("mdLibMachine" + std::to_string(track)));
		if(m_read)
		{
			juceRmlUi::EventListener::Add(m_read, Rml::EventId::Click, [this](Rml::Event&)
			{
				if(m_controller.isReadingLibrary())
					return;
				// Read again now, or as soon as the firmware takes requests
				m_readingWanted = true;
				m_lastAttempt = -1.0e9;
				m_shownRevision = ~uint64_t{0};
			});
		}
		// Read once while the plug-in is open: an editor opened again shows what was read
		m_readingWanted = !m_controller.isLibraryRead() && !m_controller.isReadingLibrary();
	}

	std::string LibraryView::kitLabel(const uint8_t _slot, const bool _read, const std::string& _name)
	{
		return number(_slot + 1u) + "  " + (_read ? (_name.empty() ? std::string("(sans nom)") : _name) : std::string("—"));
	}

	std::string LibraryView::patternLabel(const uint8_t _slot, const bool _read, const uint8_t _length, const uint8_t _kit)
	{
		return patternName(_slot) + "  " + (_read ? std::to_string(_length) + " pas · kit " + number(_kit + 1u) : std::string("—"));
	}

	void LibraryView::select(const uint8_t _slot)
	{
		m_selected = _slot;
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::selectPattern(const uint8_t _slot)
	{
		m_selectedPattern = _slot;
		m_pasteArmedTo = 0xff;
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::paste()
	{
		const auto to = m_selectedPattern;
		if(m_copySource >= m_patterns.size() || to >= m_patterns.size() || to == m_copySource)
			return;
		// A pattern the library knows empty takes the copy at once; another asks for a second click
		const auto stored = m_controller.getLibraryPattern(to);
		const bool empty = stored && stored->read && stored->trigs == uint16_t{0};
		if(empty || (m_pasteArmedTo == to && m_now - m_pasteArmedAt <= ConfirmMilliseconds))
		{
			m_pasteArmedTo = 0xff;
			if(m_controller.copyPattern(m_copySource, to))
				m_copySerial = m_controller.getPatternCopy().serial;
		}
		else
		{
			m_pasteArmedTo = to;
			m_pasteArmedAt = m_now;
		}
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::startReading(const double _nowMilliseconds)
	{
		m_lastAttempt = _nowMilliseconds;
		m_waitingForMachine = !m_controller.readLibrary();
		if(!m_waitingForMachine)
			m_readingWanted = false;
	}

	bool LibraryView::update(const double _nowMilliseconds)
	{
		// REMPLACER not confirmed in time
		m_now = _nowMilliseconds;
		if(m_pasteArmedTo < m_patterns.size() && _nowMilliseconds - m_pasteArmedAt > ConfirmMilliseconds)
		{
			m_pasteArmedTo = 0xff;
			m_shownRevision = ~uint64_t{0};
		}
		// Hidden: nothing read or drawn; drawn in full when shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_shownRevision = ~uint64_t{0};
			return false;
		}
		// A copy going on shows on COLLER and the pattern line
		if(const auto copy = copyKey(m_controller.getPatternCopy()); copy != m_shownCopy)
		{
			m_shownCopy = copy;
			m_shownRevision = ~uint64_t{0};
		}
		// The first showing reads the library, RELIRE again; while the firmware boots, a try every second
		if(m_readingWanted && !m_controller.isReadingLibrary() && _nowMilliseconds - m_lastAttempt > 1000.0)
			startReading(_nowMilliseconds);

		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getLibraryRevision();
		const auto current = m_controller.getCurrentKit();
		const auto currentPattern = m_controller.getCurrentPattern();
		const auto reading = m_controller.isReadingLibrary();
		if(revision == m_shownRevision && current == m_shownCurrent && currentPattern == m_shownCurrentPattern
			&& reading == m_shownReading && m_waitingForMachine == m_shownWaiting)
			return false;
		m_shownRevision = revision;
		m_shownCurrent = current;
		m_shownCurrentPattern = currentPattern;
		m_shownReading = reading;
		m_shownWaiting = m_waitingForMachine;

		// Only the cells that changed: a reading brings one Kit or pattern at a time
		size_t kitsRead = 0;
		m_shownLabels.resize(m_kits.size());
		for(uint8_t slot = 0; slot < m_kits.size(); ++slot)
		{
			const auto kit = m_controller.getLibraryKit(slot);
			const bool read = kit && kit->read;
			kitsRead += read ? 1 : 0;
			auto* element = m_kits[slot];
			if(!element)
				continue;
			setText(element, m_shownLabels[slot], kitLabel(slot, read, read ? kit->name : std::string()));
			element->SetClass("mdLibCurrent", slot == current);
			element->SetClass("mdEdSelected", slot == m_selected);
			element->SetClass("mdEdUnread", !read);
		}
		size_t patternsRead = 0;
		m_shownPatternLabels.resize(m_patterns.size());
		for(uint8_t slot = 0; slot < m_patterns.size(); ++slot)
		{
			const auto pattern = m_controller.getLibraryPattern(slot);
			const bool read = pattern && pattern->read;
			patternsRead += read ? 1 : 0;
			auto* element = m_patterns[slot];
			if(!element)
				continue;
			setText(element, m_shownPatternLabels[slot], patternLabel(slot, read, read ? pattern->length : 0, read ? pattern->kit : 0));
			element->SetClass("mdLibCurrent", slot == currentPattern);
			element->SetClass("mdEdSelected", slot == m_selectedPattern);
			element->SetClass("mdLibSource", slot == m_copySource);
			element->SetClass("mdEdUnread", !read);
			element->SetClass("mdLibEmpty", read && pattern->trigs == uint16_t{0});
		}
		if(m_info)
		{
			const auto kits = m_kits.size();
			const auto patterns = m_patterns.size();
			const auto progress = m_controller.getLibraryProgress();
			std::string text;
			if(reading)
			{
				text = progress < kits
					? "lecture des kits : " + std::to_string(progress) + " / " + std::to_string(kits) + "…"
					: "lecture des patterns : " + std::to_string(progress - kits) + " / " + std::to_string(patterns) + "…";
			}
			else if(m_waitingForMachine)
				text = "la machine démarre : la bibliothèque sera lue dès qu'elle répond";
			else if(!m_controller.isLibraryRead())
				text = "bibliothèque non lue";
			else
			{
				text = std::to_string(kitsRead) + " kits et " + std::to_string(patternsRead) + " patterns lus";
				const auto missing = kits + patterns - kitsRead - patternsRead;
				if(missing)
					text += " (" + std::to_string(missing) + " sans réponse)";
			}
			m_info->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}
		if(m_read)
		{
			m_read->SetInnerRML(reading ? "LECTURE…" : "RELIRE");
			m_read->SetClass("mdEdOff", reading);
		}
		renderDetail();
		renderPatternDetail();
		renderCopy();
		return true;
	}

	void LibraryView::renderCopy()
	{
		if(m_copy)
			m_copy->SetClass("mdEdOff", m_selectedPattern >= m_patterns.size());
		if(!m_paste)
			return;
		const auto copy = m_controller.getPatternCopy();
		const bool copying = copy.state == Controller::PatternCopy::Reading || copy.state == Controller::PatternCopy::Writing;
		const bool armed = m_pasteArmedTo < m_patterns.size();
		const bool ready = m_copySource < m_patterns.size() && m_selectedPattern < m_patterns.size() && m_selectedPattern != m_copySource;
		m_paste->SetClass("mdEdOff", !ready || copying);
		m_paste->SetClass("mdPlayArmed", armed);
		setText(m_paste, m_shownPasteLabel, armed ? "REMPLACER " + patternName(m_pasteArmedTo) + " ?"
			: copying ? std::string("COPIE…") : std::string("COLLER"));
	}

	void LibraryView::renderDetail()
	{
		const auto kit = m_selected < m_kits.size() ? m_controller.getLibraryKit(m_selected) : std::nullopt;
		if(m_detail)
		{
			std::string title = "KIT —";
			if(m_selected < m_kits.size())
			{
				title = "KIT " + number(m_selected + 1u);
				if(kit && kit->read)
					title += " · " + (kit->name.empty() ? std::string("(sans nom)") : kit->name);
				if(m_selected == m_controller.getCurrentKit())
					title += " · chargé";
			}
			m_detail->SetInnerRML(Rml::StringUtilities::EncodeRml(title));
		}
		for(size_t track = 0; track < m_machines.size(); ++track)
		{
			auto* element = m_machines[track];
			if(!element)
				continue;
			std::string text = number(static_cast<unsigned>(track + 1)) + "  ";
			if(kit && kit->read && track < kit->machines.size())
			{
				const auto* machine = md::machines::find(m_model, kit->machines[track]);
				text += machine ? std::string(machine->name) : "machine " + std::to_string(kit->machines[track]);
			}
			else
				text += "—";
			element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}
	}

	void LibraryView::renderPatternDetail()
	{
		if(!m_patternDetail)
			return;
		std::string text = "clic : le détail d'un pattern · en ambre : celui de la machine · grisé : sans trig";
		if(m_selectedPattern < m_patterns.size())
		{
			const auto pattern = m_controller.getLibraryPattern(m_selectedPattern);
			text = patternName(m_selectedPattern);
			if(pattern && pattern->read)
			{
				text += " · " + std::to_string(pattern->length) + " pas · kit " + number(pattern->kit + 1u);
				if(pattern->trigs)
					text += " · " + (*pattern->trigs ? std::to_string(*pattern->trigs) + " trigs" : std::string("aucun trig"));
			}
			else
				text += " · pas lu";
			if(m_selectedPattern == m_controller.getCurrentPattern())
				text += " · celui de la machine";
		}
		// What COPIER took, and how COLLER's last copy went
		if(m_copySource < m_patterns.size())
			text += " · à coller : " + patternName(m_copySource);
		const auto copy = m_controller.getPatternCopy();
		const auto copied = "copie de " + patternName(copy.from) + " vers " + patternName(copy.to);
		switch(m_copySerial && copy.serial == m_copySerial ? copy.state : Controller::PatternCopy::None)
		{
		case Controller::PatternCopy::Reading:
		case Controller::PatternCopy::Writing: text += " · " + copied + "…"; break;
		case Controller::PatternCopy::Copied: text += " · " + patternName(copy.from) + " copié sur " + patternName(copy.to); break;
		case Controller::PatternCopy::Refused: text += " · " + copied + " refusée"; break;
		case Controller::PatternCopy::Failed: text += " · " + copied + " sans réponse"; break;
		default: break;
		}
		m_patternDetail->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
	}
}
