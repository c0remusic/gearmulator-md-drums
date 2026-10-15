#pragma once

#include <cstdint>
#include <string>

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;
	struct Frame;

	// The shown Track's Link and Choke (ticket 29 of the editor map): two lines in the head band, "Also strikes track 05"
	// and "Silences track 10" (or "Strikes no other track", "Silences no track"), either of which opens the "Links and
	// Chokes" menu over the Hit screen (TrackView::Overlay::Links). Its cells set the Kit played's values through the
	// Controller; the Track itself is dimmed and does nothing; a choice keeps the menu open, Done and Esc close it. The
	// editor's frame follows what a Kit loaded, a state brought back or another Track shown changes.
	class LinksView
	{
	public:
		explicit LinksView(Editor& _editor);
		~LinksView();

		LinksView(const LinksView&) = delete;
		LinksView& operator=(const LinksView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

	private:
		void choose(bool _choke, uint8_t _target);
		// The lines and the menu for _track as the Controller holds them now
		void refresh(uint8_t _track);
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		int m_shownTrack = -1;
		int m_shownLink = -1;
		int m_shownChoke = -1;
	};
}
