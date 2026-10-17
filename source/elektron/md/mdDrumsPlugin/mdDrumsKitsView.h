#pragma once

#include "mdProtocol/mdkit.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Rml
{
	class Element;
	class Event;
}

namespace mdDrums
{
	class Bank;
	class Editor;
	struct Frame;

	// The Kits (ticket 26 of the editor map, as ticket 10 settled them): the head band's Kit row and Save, and the Kits
	// overlay over the Bank's 64 Slots. The Kit played is the Controller's, its Slot the one it came from; Save is lit
	// while it differs from that Slot, whatever changed it. The overlay chooses a Slot, loads it, saves the Kit played
	// into it, renames it in place or empties it, a click that would lose something asking for a second one; it imports
	// .syx files into the empty Slots from the chosen one on and exports the chosen Slot. The overlay changes only through
	// this view, which draws it then; the editor's frame looks at the head band RefreshHz times a second (a state
	// restored, the parameters, a Bank made once a flash image is chosen change it too). The Bank is read again whenever
	// the overlay opens.
	class KitsView
	{
	public:
		using Kit = md::automation::sysex::MdKit;
		static constexpr int RowHeight = 40;
		static constexpr int SlotTop = 184;
		static constexpr int RefreshHz = 10;

		explicit KitsView(Editor& _editor);
		~KitsView();

		KitsView(const KitsView&) = delete;
		KitsView& operator=(const KitsView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

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

		// A file the chooser gave Import or Export: imported or exported by a frame at once
		void fileChosen(bool _import, std::filesystem::path _file);

		// Keys while the overlay is open: the arrows choose, Enter loads; while renaming, Enter keeps the name. False:
		// not handled
		bool key(Rml::Event& _event);
		// Esc: leaves a renaming or a second-click question; false when there is neither
		bool escape();

		const std::string& note() const { return m_note; }
		// The action a second click is asked for: "load", "savehere" or "delete"; null when none is
		const char* confirming() const;
		bool isRenaming() const { return m_renaming; }

	private:
		// The actions that ask for a second click, in the order of g_questions in the .cpp
		enum class Confirm
		{
			None,
			Load,
			SaveHere,
			Delete
		};

		// What the head band shows: the timer draws it again only when it changed
		struct Head
		{
			const Bank* bank = nullptr;
			int slot = -1;
			std::string name;
			bool changed = false;

			bool operator==(const Head& _other) const
			{
				return bank == _other.bank && slot == _other.slot && name == _other.name && changed == _other.changed;
			}
		};

		// What Import and Export do with a file once chosen; false and a note when it cannot be read or written
		bool importFile(const std::filesystem::path& _file);
		bool exportFile(const std::filesystem::path& _file);
		// The head band and the overlay as they are now
		void refresh();
		void refresh(Head _head);
		void ask(Confirm _action, std::string _note);
		void say(std::string _note);
		// A Bank write's outcome, with a note when it failed
		bool written(bool _ok);
		// Whether the chosen Slot holds a Kit to export, with a note when it does not
		bool exportable();
		void commitRename(bool _keep);
		void openChooser(bool _import);
		Head head() const;
		void updateHead(Head _head);
		void updateSlots() const;
		void updatePanel() const;
		Bank* bank() const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		bool m_open = false;
		int m_chosen = 0;
		Confirm m_confirm = Confirm::None;
		bool m_renaming = false;
		std::string m_note;
		Head m_head;	// as last drawn
		double m_nextHeadMs = 0.0;	// when the frame looks at the head band again
		std::unique_ptr<juce::FileChooser> m_chooser;
		std::optional<std::pair<bool, std::filesystem::path>> m_chosenFile;	// Import (true) or Export, for the frame
	};
}
