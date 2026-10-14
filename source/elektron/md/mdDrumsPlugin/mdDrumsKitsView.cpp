#include "mdDrumsKitsView.h"

#include "mdDrumsBank.h"
#include "mdDrumsController.h"
#include "mdDrumsDom.h"
#include "mdDrumsEditor.h"
#include "mdDrumsProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		// A second click's question, by KitsView::Confirm: its action (KitsView::confirming) and its button's word
		struct Question
		{
			const char* action;
			const char* button;
		};
		constexpr std::array<Question, 4> g_questions{{
			{nullptr, nullptr},
			{"load", "Load anyway"},
			{"savehere", "Replace it"},
			{"delete", "Delete it"}}};

		// Slots 01-16 on columns 1-3, 17-32 on 4-6, 33-48 on 7-9, 49-64 on 10-12
		int slotLeft(const int _slot)
		{
			return 16 + 240 * (_slot / 16);
		}

		std::string escaped(const std::string& _text)
		{
			return Rml::StringUtilities::EncodeRml(_text);
		}
	}

	KitsView::KitsView(Editor& _editor) : m_editor(_editor)
	{
		m_editor.addClick("kit_save", [this](Rml::Event&) { save(); });
		for(int slot = 0; slot < Bank::SlotCount; ++slot)
		{
			const auto id = "slot" + std::to_string(slot + 1);
			m_editor.addClick(id, [this, slot](Rml::Event&) { choose(slot); });
			if(auto* element = find(id))
				juceRmlUi::EventListener::Add(element, Rml::EventId::Dblclick, [this, slot](Rml::Event&)
				{
					choose(slot);
					load();
				});
		}
		m_editor.addClick("kit_load", [this](Rml::Event&) { load(); });
		m_editor.addClick("kit_savehere", [this](Rml::Event&) { saveHere(); });
		m_editor.addClick("kit_rename", [this](Rml::Event&) { rename(); });
		m_editor.addClick("kit_delete", [this](Rml::Event&) { remove(); });
		m_editor.addClick("kit_cancel", [this](Rml::Event&) { cancel(); });
		m_editor.addClick("kit_confirm", [this](Rml::Event&)
		{
			switch(m_confirm)
			{
			case Confirm::Load:		load(); break;
			case Confirm::SaveHere:	saveHere(); break;
			case Confirm::Delete:	remove(); break;
			case Confirm::None:		break;
			}
		});
		m_editor.addClick("kits_import", [this](Rml::Event&) { openChooser(true); });
		m_editor.addClick("kits_export", [this](Rml::Event&) { openChooser(false); });

		refresh();
		startTimerHz(RefreshHz);
	}

	KitsView::~KitsView()
	{
		stopTimer();
	}

	void KitsView::timerCallback()
	{
		// The head band also changes from elsewhere: a state restored, the parameters (Save lit), a Bank made once a
		// flash image is chosen
		if(head() == m_head)
			return;
		m_editor.withDocumentLocked([this] { refresh(); });
	}

	Bank* KitsView::bank() const
	{
		return m_editor.processor().getBank();
	}

	Rml::Element* KitsView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	void KitsView::setOpen(const bool _open)
	{
		if(_open && !m_open)
		{
			if(auto* b = bank())
				b->read();
			m_chosen = m_editor.controller().playedSlot();
			m_note.clear();
		}
		m_open = _open;
		m_confirm = Confirm::None;
		if(m_renaming)
			commitRename(false);
		refresh();
	}

	bool KitsView::isChanged() const
	{
		return head().changed;
	}

	bool KitsView::save()
	{
		auto* b = bank();
		if(!b || !isChanged())
			return false;
		const auto slot = m_editor.controller().playedSlot();
		if(!written(b->write(slot, m_editor.controller().playedKit())))
			return false;
		m_confirm = Confirm::None;
		say("Saved in slot " + twoDigits(slot + 1));
		return true;
	}

	void KitsView::choose(const int _slot)
	{
		const auto slot = std::clamp(_slot, 0, Bank::SlotCount - 1);
		if(slot == m_chosen && m_confirm == Confirm::None)
			return;
		if(m_renaming)
			commitRename(false);
		m_chosen = slot;
		m_confirm = Confirm::None;
		say({});
	}

	void KitsView::load()
	{
		const auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		const auto& kit = *b->slot(m_chosen);
		if(isChanged() && m_confirm != Confirm::Load)
		{
			ask(Confirm::Load, "Changes to " + m_editor.controller().playedKit().displayName() + " will be lost");
			return;
		}
		m_editor.controller().loadKit(kit, m_chosen);
		m_confirm = Confirm::None;
		say("Playing " + twoDigits(m_chosen + 1) + ", " + kit.displayName());
	}

	void KitsView::saveHere()
	{
		auto* b = bank();
		if(!b)
			return;
		const auto playing = m_chosen == m_editor.controller().playedSlot();
		if(!b->isEmpty(m_chosen) && !playing && m_confirm != Confirm::SaveHere)
		{
			ask(Confirm::SaveHere, "Replaces " + b->slot(m_chosen)->displayName() + " in slot "
				+ twoDigits(m_chosen + 1));
			return;
		}
		if(!written(b->write(m_chosen, m_editor.controller().playedKit())))
			return;
		m_editor.controller().setPlayedSlot(m_chosen);
		m_confirm = Confirm::None;
		say("Saved in slot " + twoDigits(m_chosen + 1));
	}

	void KitsView::rename()
	{
		const auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		m_confirm = Confirm::None;
		m_renaming = true;
		if(auto* edit = find("kit_name_edit"))
		{
			edit->SetAttribute("value", b->slot(m_chosen)->displayName());
			edit->Focus();
		}
		say("Type its name, up to 16 characters; Enter keeps it, Esc leaves it");
	}

	void KitsView::setRenameText(const std::string& _text)
	{
		if(auto* edit = find("kit_name_edit"))
			edit->SetAttribute("value", _text);
	}

	void KitsView::commitRename(const bool _keep)
	{
		m_renaming = false;
		auto* b = bank();
		auto* edit = find("kit_name_edit");
		if(!_keep || !b || b->isEmpty(m_chosen) || !edit)
		{
			say({});
			return;
		}
		const auto name = edit->GetAttribute<Rml::String>("value", "");
		auto kit = *b->slot(m_chosen);
		kit.setName(name);
		if(!written(b->write(m_chosen, kit)))
			return;
		// The Kit played keeps its Slot's name: a renaming is no change to the sound
		if(m_chosen == m_editor.controller().playedSlot())
			m_editor.controller().renameKit(name);
		say("Renamed slot " + twoDigits(m_chosen + 1) + " " + kit.displayName());
	}

	void KitsView::remove()
	{
		auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		if(m_confirm != Confirm::Delete)
		{
			ask(Confirm::Delete, "Empties slot " + twoDigits(m_chosen + 1));
			return;
		}
		if(!written(b->write(m_chosen, std::nullopt)))
			return;
		m_confirm = Confirm::None;
		say("Slot " + twoDigits(m_chosen + 1) + " is empty");
	}

	void KitsView::cancel()
	{
		m_confirm = Confirm::None;
		say({});
	}

	bool KitsView::importFile(const std::filesystem::path& _file)
	{
		auto* b = bank();
		if(!b)
			return false;
		std::ifstream in(_file, std::ios::binary);
		const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		const auto kits = Bank::parseSyx(bytes);
		const auto fileName = _file.filename().string();
		if(kits.empty())
		{
			say("No Machinedrum kit in " + fileName);
			return false;
		}
		b->read();
		std::vector<Bank::SlotKit> slots;
		for(int slot = m_chosen; slot < Bank::SlotCount && slots.size() < kits.size(); ++slot)
		{
			if(b->isEmpty(slot))
				slots.emplace_back(slot, kits[slots.size()]);
		}
		if(!slots.empty() && !written(b->write(slots)))
			return false;
		m_confirm = Confirm::None;
		const auto imported = slots.size();
		const auto left = kits.size() - imported;
		if(!imported)
		{
			say("No empty slot from " + twoDigits(m_chosen + 1) + " on");
			return false;
		}
		const auto first = slots.front().first;
		m_chosen = first;
		std::string text = imported == 1 ? "Imported 1 kit from " + fileName + " into slot " + twoDigits(first + 1)
			: "Imported " + std::to_string(imported) + " kits from " + fileName + " from slot " + twoDigits(first + 1);
		if(left)
			text = std::to_string(imported) + " imported, " + std::to_string(left) + " left out: no empty slot";
		say(text);
		return true;
	}

	bool KitsView::exportFile(const std::filesystem::path& _file)
	{
		if(!exportable())
			return false;
		const auto bytes = Bank::dump(bank()->slot(m_chosen), m_chosen);
		std::ofstream out(_file, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if(!out)
		{
			say("Cannot write " + _file.string());
			return false;
		}
		say("Exported slot " + twoDigits(m_chosen + 1) + " as " + _file.filename().string());
		return true;
	}

	void KitsView::openChooser(const bool _import)
	{
		const auto* b = bank();
		if(!b || (!_import && !exportable()))
			return;
		// The file chosen arrives from the message loop, outside an RmlUi event: its handling locks the document
		const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		if(_import)
		{
			m_chooser = std::make_unique<juce::FileChooser>("Import Machinedrum kits", folder, "*.syx");
			m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
				[this](const juce::FileChooser& _chooser)
				{
					const auto file = _chooser.getResult();
					if(file != juce::File())
						m_editor.withDocumentLocked([&] { importFile(file.getFullPathName().toStdString()); });
				});
			return;
		}
		const auto name = b->slot(m_chosen)->displayName();
		m_chooser = std::make_unique<juce::FileChooser>("Export a Machinedrum kit",
			folder.getChildFile(juce::String(name.empty() ? "Kit" : name) + ".syx"), "*.syx");
		m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
			| juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& _chooser)
			{
				const auto file = _chooser.getResult();
				if(file != juce::File())
					m_editor.withDocumentLocked([&] { exportFile(file.getFullPathName().toStdString()); });
			});
	}

	bool KitsView::key(Rml::Event& _event)
	{
		const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
		const bool enter = key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER;
		if(m_renaming)
		{
			if(!enter)
				return false;
			commitRename(true);
			return true;
		}
		if(!m_open)
			return false;
		switch(key)
		{
		case Rml::Input::KI_UP:		choose(m_chosen - 1); return true;
		case Rml::Input::KI_DOWN:	choose(m_chosen + 1); return true;
		case Rml::Input::KI_LEFT:	choose(m_chosen - 16); return true;
		case Rml::Input::KI_RIGHT:	choose(m_chosen + 16); return true;
		default:					break;
		}
		if(enter)
		{
			load();
			return true;
		}
		return false;
	}

	bool KitsView::escape()
	{
		if(m_renaming)
		{
			commitRename(false);
			return true;
		}
		if(m_confirm != Confirm::None)
		{
			cancel();
			return true;
		}
		return false;
	}

	const char* KitsView::confirming() const
	{
		return g_questions[static_cast<size_t>(m_confirm)].action;
	}

	void KitsView::ask(const Confirm _action, std::string _note)
	{
		m_confirm = _action;
		say(std::move(_note));
	}

	void KitsView::say(std::string _note)
	{
		m_note = std::move(_note);
		refresh();
	}

	bool KitsView::written(const bool _ok)
	{
		if(!_ok)
			say("The Bank cannot be written: " + bank()->file().string());
		return _ok;
	}

	bool KitsView::exportable()
	{
		const auto* b = bank();
		if(!b)
			return false;
		if(!b->isEmpty(m_chosen))
			return true;
		say("Slot " + twoDigits(m_chosen + 1) + " is empty: nothing to export");
		return false;
	}

	void KitsView::refresh()
	{
		updateHead();
		if(!m_open)
			return;
		updateSlots();
		updatePanel();
	}

	KitsView::Head KitsView::head() const
	{
		const auto& controller = m_editor.controller();
		const auto kit = controller.playedKit();
		Head result;
		result.bank = bank();
		result.slot = controller.playedSlot();
		result.name = kit.displayName();
		if(result.bank)
		{
			const auto& stored = result.bank->slot(result.slot);
			result.changed = !stored || *stored != kit;
		}
		return result;
	}

	void KitsView::updateHead()
	{
		m_head = head();
		if(auto* e = find("kit_slot"))
			e->SetInnerRML(twoDigits(m_head.slot + 1));
		if(auto* e = find("kit_name"))
			e->SetInnerRML(escaped(m_head.name));
		if(auto* e = find("kit_save"))
			e->SetClass("on", m_head.changed);
	}

	void KitsView::updateSlots() const
	{
		const auto* b = bank();
		if(!b)
			return;
		for(int slot = 0; slot < Bank::SlotCount; ++slot)
		{
			auto* element = find("slot" + std::to_string(slot + 1));
			if(!element)
				continue;
			const auto& kit = b->slot(slot);
			element->SetInnerRML("<span class=\"num\">" + twoDigits(slot + 1) + "</span>"
				+ (kit ? escaped(kit->displayName()) : std::string("Empty")));
			element->SetClass("empty", !kit);
			element->SetClass("on", slot == m_chosen);
		}
		// The chosen Slot lit from the gutters' middles (the window's edge for the first column); the played one marked
		const auto from = [](const int _slot) { return _slot < 16 ? 0 : slotLeft(_slot) - 8; };
		const auto top = [](const int _slot) { return SlotTop + RowHeight * (_slot % 16); };
		if(auto* bg = find("slot_sel_bg"))
		{
			bg->SetProperty("left", dp(from(m_chosen)));
			bg->SetProperty("top", dp(top(m_chosen) - 8));
			bg->SetProperty("width", dp(slotLeft(m_chosen) + 224 + 8 - from(m_chosen)));
		}
		const auto played = m_editor.controller().playedSlot();
		if(auto* mark = find("slot_play_mark"))
		{
			mark->SetProperty("left", dp(from(played)));
			mark->SetProperty("top", dp(top(played)));
		}
	}

	void KitsView::updatePanel() const
	{
		const auto* b = bank();
		if(!b)
			return;
		const auto& kit = b->slot(m_chosen);
		const auto playing = m_chosen == m_editor.controller().playedSlot();

		if(auto* e = find("kit_caption"))
			e->SetInnerRML("Slot " + twoDigits(m_chosen + 1) + (playing ? ", playing" : ""));
		if(auto* e = find("kit_name_big"))
		{
			e->SetInnerRML(kit ? escaped(kit->displayName()) : std::string("Empty"));
			e->SetClass("empty", !kit);
		}
		setDisplay(find("kit_name_big"), !m_renaming);
		setDisplay(find("kit_name_edit"), m_renaming);
		setDisplay(find("kit_tracks"), kit.has_value());
		for(uint8_t t = 0; t < 16; ++t)
		{
			const auto id = "kitm" + std::to_string(t + 1);
			setDisplay(find(id), kit.has_value());
			if(!kit)
				continue;
			const auto* machine = md::machines::find(md::MachineModel::Machinedrum, kit->machine(t));
			if(auto* e = find(id))
				e->SetInnerRML("<span class=\"num\">" + twoDigits(t + 1) + "</span>"
					+ (machine ? std::string(machine->name) : std::string("--")));
		}

		// Asking for a second click, the row holds that action and Cancel only
		const bool asking = m_confirm != Confirm::None;
		setDisplay(find("kit_load"), !asking && kit.has_value(), "inline");
		setDisplay(find("kit_savehere"), !asking, "inline");
		setDisplay(find("kit_rename"), !asking && kit.has_value(), "inline");
		setDisplay(find("kit_delete"), !asking && kit.has_value(), "inline");
		setDisplay(find("kit_confirm"), asking, "inline");
		setDisplay(find("kit_cancel"), asking, "inline");
		if(auto* e = find("kit_confirm"); e && asking)
			e->SetInnerRML(g_questions[static_cast<size_t>(m_confirm)].button);
		if(auto* e = find("kit_note"))
			e->SetInnerRML(escaped(m_note));
	}
}
