#include "mdLibraryView.h"

#include "mdController.h"

#include "mdProtocol/mdmachines.h"

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
		// CHARGER and SAUVER on KITS, CHARGER on PATTERNS
		m_kitLoad = _document.GetElementById("mdLibKitLoad");
		if(m_kitLoad)
			juceRmlUi::EventListener::Add(m_kitLoad, Rml::EventId::Click, [this](Rml::Event&) { loadKit(); });
		m_kitSave = _document.GetElementById("mdLibKitSave");
		if(m_kitSave)
			juceRmlUi::EventListener::Add(m_kitSave, Rml::EventId::Click, [this](Rml::Event&) { saveKit(); });
		m_patternLoad = _document.GetElementById("mdLibPatternLoad");
		if(m_patternLoad)
			juceRmlUi::EventListener::Add(m_patternLoad, Rml::EventId::Click, [this](Rml::Event&) { loadPattern(); });
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
		m_kitLoadArmed = 0xff;
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::selectPattern(const uint8_t _slot)
	{
		m_selectedPattern = _slot;
		m_pasteArmedTo = 0xff;
		m_patternLoadArmed = 0xff;
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::note(std::string _text)
	{
		m_note = std::move(_text);
		m_noteUntil = m_now + NoteMilliseconds;
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::loadKit()
	{
		// A pattern load waiting brings its own Kit: no Kit load or save before it plays
		const auto slot = m_selected;
		if(slot >= m_kits.size() || m_controller.isPatternBusy() || m_controller.getPatternLoad() < m_patterns.size())
			return;
		// The live Kit's values not saved go: a second click within ConfirmMilliseconds
		if(m_kitLoadArmed == slot && m_now - m_kitLoadArmedAt <= ConfirmMilliseconds)
		{
			m_kitLoadArmed = 0xff;
			const bool again = slot == m_controller.getCurrentKit();
			if(m_controller.loadKit(slot))
				note("kit " + number(slot + 1u) + (again ? " rechargé" : " chargé"));
		}
		else
		{
			m_kitLoadArmed = slot;
			m_kitLoadArmedAt = m_now;
		}
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::saveKit()
	{
		const auto kit = m_controller.getCurrentKit();
		m_kitLoadArmed = 0xff;
		if(m_controller.saveKit())
			note("kit " + number(kit + 1u) + " sauvé");
		m_shownRevision = ~uint64_t{0};
	}

	void LibraryView::loadPattern()
	{
		const auto slot = m_selectedPattern;
		if(slot >= m_patterns.size() || slot == m_controller.getCurrentPattern() || m_controller.isPatternBusy())
			return;
		// Its Kit replaces the live one: a second click within ConfirmMilliseconds
		if(m_patternLoadArmed == slot && m_now - m_patternLoadArmedAt <= ConfirmMilliseconds)
		{
			m_patternLoadArmed = 0xff;
			if(m_controller.loadPattern(slot))
				m_note.clear();
		}
		else
		{
			m_patternLoadArmed = slot;
			m_patternLoadArmedAt = m_now;
		}
		m_shownRevision = ~uint64_t{0};
	}

	std::string LibraryView::loadText() const
	{
		if(m_kitLoadArmed < m_kits.size())
		{
			const auto current = m_controller.getCurrentKit();
			if(m_kitLoadArmed == current)
				return "RECHARGER KIT " + number(current + 1u) + " ? ses réglages non sauvés seront perdus";
			const std::string live = current < m_kits.size() ? "du kit " + number(current + 1u) : "du kit en cours";
			return "CHARGER KIT " + number(m_kitLoadArmed + 1u) + " ? les réglages " + live + " non sauvés seront perdus";
		}
		if(m_patternLoadArmed < m_patterns.size())
		{
			const auto pattern = m_controller.getLibraryPattern(m_patternLoadArmed);
			const std::string kit = pattern && pattern->read ? "son kit " + number(pattern->kit + 1u) : "son kit";
			return "CHARGER " + patternName(m_patternLoadArmed) + " ? " + kit
				+ " remplace le kit en cours, réglages non sauvés perdus";
		}
		const auto loading = m_controller.getPatternLoad();
		if(loading < m_patterns.size())
		{
			// Playing, the machine takes it at the end of the pattern; stopped for long, at the next PLAY
			if(m_controller.getPlayingStep())
				return patternName(loading) + " demandé : joue à la fin du pattern en cours";
			return m_now - m_patternLoadSeenAt > 1500.0 ? patternName(loading) + " demandé : joue au prochain PLAY"
				: "chargement de " + patternName(loading) + "…";
		}
		return m_note;
	}

	void LibraryView::renderLoad(const bool _busy)
	{
		const auto currentKit = m_controller.getCurrentKit();
		// A pattern load waiting brings its own Kit
		const bool kitBusy = _busy || m_controller.getPatternLoad() < m_patterns.size();
		if(m_kitLoad)
		{
			const bool armed = m_kitLoadArmed < m_kits.size();
			const bool again = m_selected == currentKit;
			m_kitLoad->SetClass("mdEdOff", m_selected >= m_kits.size() || kitBusy);
			m_kitLoad->SetClass("mdPlayArmed", armed);
			std::string label = again ? "RECHARGER" : "CHARGER";
			if(armed)
				label += " KIT " + number(m_kitLoadArmed + 1u) + " ?";
			setText(m_kitLoad, m_shownKitLoadLabel, label);
		}
		if(m_kitSave)
		{
			m_kitSave->SetClass("mdEdOff", currentKit >= m_kits.size() || kitBusy);
			setText(m_kitSave, m_shownKitSaveLabel, currentKit < m_kits.size() ? "SAUVER KIT " + number(currentKit + 1u)
				: std::string("SAUVER"));
		}
		if(m_patternLoad)
		{
			const bool armed = m_patternLoadArmed < m_patterns.size();
			m_patternLoad->SetClass("mdEdOff", m_selectedPattern >= m_patterns.size()
				|| m_selectedPattern == m_controller.getCurrentPattern() || _busy);
			m_patternLoad->SetClass("mdPlayArmed", armed);
			setText(m_patternLoad, m_shownPatternLoadLabel, armed ? "CHARGER " + patternName(m_patternLoadArmed) + " ?"
				: std::string("CHARGER"));
		}
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
		// REMPLACER or a CHARGER not confirmed in time, a note shown long enough
		m_now = _nowMilliseconds;
		const auto expired = [&](uint8_t& _armed, const size_t _count, const double _at)
		{
			if(_armed < _count && _nowMilliseconds - _at > ConfirmMilliseconds)
			{
				_armed = 0xff;
				m_shownRevision = ~uint64_t{0};
			}
		};
		expired(m_pasteArmedTo, m_patterns.size(), m_pasteArmedAt);
		expired(m_kitLoadArmed, m_kits.size(), m_kitLoadArmedAt);
		expired(m_patternLoadArmed, m_patterns.size(), m_patternLoadArmedAt);
		if(!m_note.empty() && _nowMilliseconds > m_noteUntil)
		{
			m_note.clear();
			m_shownRevision = ~uint64_t{0};
		}
		// The pattern load the controller waited for is over: the pattern plays, or it did not come in time
		if(const auto loading = m_controller.getPatternLoad(); loading != m_shownPatternLoad)
		{
			if(m_shownPatternLoad < m_patterns.size() && loading >= m_patterns.size())
			{
				const bool loaded = m_controller.getCurrentPattern() == m_shownPatternLoad;
				note(patternName(m_shownPatternLoad) + (loaded ? " chargé" : " : pas chargé, la machine ne l'a pas pris"));
			}
			m_shownPatternLoad = loading;
			m_patternLoadSeenAt = _nowMilliseconds;
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
		// A pattern write or a copy holds CHARGER and SAUVER back; a load waits for the end of the pattern playing
		const bool busy = m_controller.isPatternBusy();
		const bool playing = m_controller.getPlayingStep().has_value();
		// Stopped with a load waiting, the line changes on its own after a while (loadText)
		const bool waitingLong = m_shownPatternLoad < m_patterns.size() && !playing
			&& _nowMilliseconds - m_patternLoadSeenAt > 1500.0;
		if(revision == m_shownRevision && current == m_shownCurrent && currentPattern == m_shownCurrentPattern
			&& reading == m_shownReading && m_waitingForMachine == m_shownWaiting && busy == m_shownBusy
			&& playing == m_shownPlaying && waitingLong == m_shownWaitingLong)
			return false;
		m_shownWaitingLong = waitingLong;
		m_shownRevision = revision;
		m_shownCurrent = current;
		m_shownCurrentPattern = currentPattern;
		m_shownReading = reading;
		m_shownWaiting = m_waitingForMachine;
		m_shownBusy = busy;
		m_shownPlaying = playing;

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
			// What CHARGER asks to confirm takes the line; a load waiting, or what a load or a save did, follows
			const auto load = loadText();
			if(m_kitLoadArmed < kits || m_patternLoadArmed < patterns)
				text = load;
			else if(!load.empty())
				text += " · " + load;
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
		renderLoad(busy);
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
