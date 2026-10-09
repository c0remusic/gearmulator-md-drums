#include "mdDrumsKitsView.h"

#include "mdDrumsBank.h"
#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "mdProtocol/mdmachines.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace mdDrums
{
	namespace
	{
		std::string twoDigits(const int _value)
		{
			char text[8];
			(void)std::snprintf(text, sizeof(text), "%02d", _value);
			return text;
		}

		std::string dp(const int _value)
		{
			return std::to_string(_value) + "dp";
		}

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
			const std::string action = m_confirm ? m_confirm : "";
			if(action == "load")
				load();
			else if(action == "savehere")
				saveHere();
			else if(action == "delete")
				remove();
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

	Bank* KitsView::bank() const
	{
		return static_cast<Processor&>(m_editor.getProcessor()).getBank();
	}

	Controller& KitsView::controller() const
	{
		return static_cast<Controller&>(m_editor.getProcessor().getController());
	}

	Rml::Element* KitsView::find(const std::string& _id) const
	{
		return m_editor.findChild(_id, false);
	}

	void KitsView::setOpen(const bool _open)
	{
		if(_open && !m_open)
		{
			if(auto* b = bank())
				b->read();
			m_chosen = controller().playedSlot();
			m_note.clear();
		}
		m_open = _open;
		m_confirm = nullptr;
		if(m_renaming)
			commitRename(false);
		refresh();
	}

	bool KitsView::isChanged() const
	{
		const auto* b = bank();
		if(!b)
			return false;
		const auto& stored = b->slot(controller().playedSlot());
		return !stored || *stored != controller().playedKit();
	}

	bool KitsView::save()
	{
		auto* b = bank();
		if(!b || !isChanged())
			return false;
		const auto slot = controller().playedSlot();
		if(!b->write(slot, controller().playedKit()))
		{
			say("The Bank cannot be written: " + b->file().string());
			return false;
		}
		m_confirm = nullptr;
		say("Saved in slot " + twoDigits(slot + 1));
		return true;
	}

	void KitsView::choose(const int _slot)
	{
		const auto slot = std::clamp(_slot, 0, Bank::SlotCount - 1);
		if(slot == m_chosen && !m_confirm)
			return;
		if(m_renaming)
			commitRename(false);
		m_chosen = slot;
		m_confirm = nullptr;
		say({});
	}

	void KitsView::load()
	{
		const auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		const auto& kit = *b->slot(m_chosen);
		if(isChanged() && (!m_confirm || std::string(m_confirm) != "load"))
		{
			ask("load", "Changes to " + controller().playedKit().displayName() + " will be lost");
			return;
		}
		controller().loadKit(kit, m_chosen);
		m_confirm = nullptr;
		say("Playing " + twoDigits(m_chosen + 1) + ", " + kit.displayName());
	}

	void KitsView::saveHere()
	{
		auto* b = bank();
		if(!b)
			return;
		const auto playing = m_chosen == controller().playedSlot();
		if(!b->isEmpty(m_chosen) && !playing && (!m_confirm || std::string(m_confirm) != "savehere"))
		{
			ask("savehere", "Replaces " + b->slot(m_chosen)->displayName() + " in slot " + twoDigits(m_chosen + 1));
			return;
		}
		if(!b->write(m_chosen, controller().playedKit()))
		{
			say("The Bank cannot be written: " + b->file().string());
			return;
		}
		controller().setPlayedSlot(m_chosen);
		m_confirm = nullptr;
		say("Saved in slot " + twoDigits(m_chosen + 1));
	}

	void KitsView::rename()
	{
		const auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		m_confirm = nullptr;
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
		auto name = edit->GetAttribute<Rml::String>("value", "");
		// The Machinedrum's names: 16 characters of 7 bits
		name.erase(std::remove_if(name.begin(), name.end(), [](const char _c) { return static_cast<unsigned char>(_c) > 0x7e
			|| static_cast<unsigned char>(_c) < 0x20; }), name.end());
		if(name.size() > Kit::NameSize)
			name.resize(Kit::NameSize);
		auto kit = *b->slot(m_chosen);
		kit.name.fill(0);
		std::copy(name.begin(), name.end(), kit.name.begin());
		if(!b->write(m_chosen, kit))
		{
			say("The Bank cannot be written: " + b->file().string());
			return;
		}
		// The Kit played keeps its Slot's name: a renaming is no change to the sound
		if(m_chosen == controller().playedSlot())
			controller().renameKit(name);
		say("Renamed slot " + twoDigits(m_chosen + 1) + " " + kit.displayName());
	}

	void KitsView::remove()
	{
		auto* b = bank();
		if(!b || b->isEmpty(m_chosen))
			return;
		if(!m_confirm || std::string(m_confirm) != "delete")
		{
			ask("delete", "Empties slot " + twoDigits(m_chosen + 1));
			return;
		}
		if(!b->write(m_chosen, std::nullopt))
		{
			say("The Bank cannot be written: " + b->file().string());
			return;
		}
		m_confirm = nullptr;
		say("Slot " + twoDigits(m_chosen + 1) + " is empty");
	}

	void KitsView::cancel()
	{
		m_confirm = nullptr;
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
		size_t imported = 0;
		int first = -1;
		for(int slot = m_chosen; slot < Bank::SlotCount && imported < kits.size(); ++slot)
		{
			if(!b->isEmpty(slot))
				continue;
			if(!b->write(slot, kits[imported]))
			{
				say("The Bank cannot be written: " + b->file().string());
				return false;
			}
			if(first < 0)
				first = slot;
			++imported;
		}
		m_confirm = nullptr;
		const auto left = kits.size() - imported;
		if(!imported)
		{
			say("No empty slot from " + twoDigits(m_chosen + 1) + " on");
			return false;
		}
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
		const auto* b = bank();
		if(!b)
			return false;
		if(b->isEmpty(m_chosen))
		{
			say("Slot " + twoDigits(m_chosen + 1) + " is empty: nothing to export");
			return false;
		}
		const auto bytes = Bank::dump(b->slot(m_chosen), m_chosen);
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
		if(!b)
			return;
		if(!_import && b->isEmpty(m_chosen))
		{
			say("Slot " + twoDigits(m_chosen + 1) + " is empty: nothing to export");
			return;
		}
		const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
		if(_import)
		{
			m_chooser = std::make_unique<juce::FileChooser>("Import Machinedrum kits", folder, "*.syx");
			m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
				[this](const juce::FileChooser& _chooser)
				{
					const auto file = _chooser.getResult();
					if(file != juce::File())
						importFile(file.getFullPathName().toStdString());
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
					exportFile(file.getFullPathName().toStdString());
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
		if(m_confirm)
		{
			cancel();
			return true;
		}
		return false;
	}

	void KitsView::ask(const char* _action, std::string _note)
	{
		m_confirm = _action;
		say(std::move(_note));
	}

	void KitsView::say(std::string _note)
	{
		m_note = std::move(_note);
		refresh();
	}

	void KitsView::refresh()
	{
		updateHead();
		if(!m_open)
			return;
		updateSlots();
		updatePanel();
	}

	void KitsView::updateHead() const
	{
		const auto slot = controller().playedSlot();
		if(auto* e = find("kit_slot"))
			e->SetInnerRML(twoDigits(slot + 1));
		if(auto* e = find("kit_name"))
			e->SetInnerRML(escaped(controller().playedKit().displayName()));
		if(auto* e = find("kit_save"))
			e->SetClass("on", isChanged());
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
		const auto played = controller().playedSlot();
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
		const auto playing = m_chosen == controller().playedSlot();
		const auto show = [this](const std::string& _id, const bool _visible)
		{
			if(auto* e = find(_id))
				e->SetProperty("display", _visible ? "block" : "none");
		};
		const auto showInline = [this](const std::string& _id, const bool _visible)
		{
			if(auto* e = find(_id))
				e->SetProperty("display", _visible ? "inline" : "none");
		};

		if(auto* e = find("kit_caption"))
			e->SetInnerRML("Slot " + twoDigits(m_chosen + 1) + (playing ? ", playing" : ""));
		if(auto* e = find("kit_name_big"))
		{
			e->SetInnerRML(kit ? escaped(kit->displayName()) : std::string("Empty"));
			e->SetClass("empty", !kit);
		}
		show("kit_name_big", !m_renaming);
		show("kit_name_edit", m_renaming);
		show("kit_tracks", kit.has_value());
		for(uint8_t t = 0; t < 16; ++t)
		{
			const auto id = "kitm" + std::to_string(t + 1);
			show(id, kit.has_value());
			if(!kit)
				continue;
			const auto* machine = md::machines::find(md::MachineModel::Machinedrum, kit->machine(t));
			if(auto* e = find(id))
				e->SetInnerRML("<span class=\"num\">" + twoDigits(t + 1) + "</span>"
					+ (machine ? std::string(machine->name) : std::string("--")));
		}

		// Asking for a second click, the row holds that action and Cancel only
		const std::string confirm = m_confirm ? m_confirm : "";
		showInline("kit_load", !m_confirm && kit.has_value());
		showInline("kit_savehere", !m_confirm);
		showInline("kit_rename", !m_confirm && kit.has_value());
		showInline("kit_delete", !m_confirm && kit.has_value());
		showInline("kit_confirm", m_confirm != nullptr);
		showInline("kit_cancel", m_confirm != nullptr);
		if(auto* e = find("kit_confirm"))
			e->SetInnerRML(confirm == "load" ? "Load anyway" : confirm == "savehere" ? "Replace it" : "Delete it");
		if(auto* e = find("kit_note"))
			e->SetInnerRML(escaped(m_note));
	}
}
