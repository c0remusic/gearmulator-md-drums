#pragma once

#include "mdProtocol/mdkit.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace Rml
{
	class Element;
	class Event;
}

namespace mdDrums
{
	class Bank;
	class Controller;
	class Editor;

	// The Kits (ticket 26 of the editor map, as ticket 10 settled them): the head band's Kit row and Save, and the Kits
	// overlay over the Bank's 64 Slots. The Kit played is the Controller's, its Slot the one it came from; Save is lit
	// while it differs from that Slot, whatever changed it. The overlay chooses a Slot, loads it, saves the Kit played
	// into it, renames it in place or empties it, a click that would lose something asking for a second one; it imports
	// .syx files into the empty Slots from the chosen one on and exports the chosen Slot. Everything runs on the message
	// thread; the Bank is read again whenever the overlay opens.
	class KitsView : juce::Timer
	{
	public:
		using Kit = md::automation::sysex::MdKit;
		static constexpr int RowHeight = 40;
		static constexpr int SlotTop = 184;
		static constexpr int RefreshHz = 10;

		explicit KitsView(Editor& _editor);
		~KitsView() override;

		KitsView(const KitsView&) = delete;
		KitsView& operator=(const KitsView&) = delete;

		// The overlay opened (the Bank read again, the Slot played chosen) or closed
		void setOpen(bool _open);
		bool isOpen() const { return m_open; }

		// Whether the Kit played differs from its Slot (Save lit)
		bool isChanged() const;
		// Save and Ctrl+S: the Kit played into its Slot, when it differs from it
		bool save();

		int chosen() const { return m_chosen; }
		void choose(int _slot);
		// Load (Enter, a double-click): a second click when the Kit played has changed
		void load();
		void saveHere();
		void rename();
		void remove();
		void cancel();

		// What Import and Export do with a file once chosen; false and a note when it cannot be read or written
		bool importFile(const std::filesystem::path& _file);
		bool exportFile(const std::filesystem::path& _file);

		// The head band and the overlay as they are now (the timer does it RefreshHz times a second)
		void refresh();

		// Keys while the overlay is open: the arrows choose, Enter loads; while renaming, Enter keeps the name. False:
		// not handled
		bool key(Rml::Event& _event);
		// Esc: leaves a renaming or a second-click question; false when there is neither
		bool escape();

		const std::string& note() const { return m_note; }
		const char* confirming() const { return m_confirm; }
		bool isRenaming() const { return m_renaming; }
		void setRenameText(const std::string& _text);

	private:
		void timerCallback() override { refresh(); }
		void ask(const char* _action, std::string _note);
		void say(std::string _note);
		void commitRename(bool _keep);
		void openChooser(bool _import);
		void updateHead() const;
		void updateSlots() const;
		void updatePanel() const;
		Bank* bank() const;
		Controller& controller() const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		bool m_open = false;
		int m_chosen = 0;
		const char* m_confirm = nullptr;	// "load", "savehere" or "delete" while a second click is asked for
		bool m_renaming = false;
		std::string m_note;
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
