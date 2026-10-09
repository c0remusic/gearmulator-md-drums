#pragma once

#include "juce_events/juce_events.h"

#include <cstdint>
#include <string>

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;

	// The shown Track's Link and Choke (ticket 29 of the editor map): two lines in the head band, "Also strikes track 05"
	// and "Silences track 10" (or "Strikes no other track", "Silences no track"), either of which opens the "Links and
	// Chokes" menu over the Hit screen (TrackView::Overlay::Links). Its cells set the Kit played's values through the
	// Controller; the Track itself is dimmed and does nothing; a choice keeps the menu open, Done and Esc close it. A timer
	// at 10 Hz follows what a Kit loaded or a state brought back changes, on the message thread.
	class LinksView : juce::Timer
	{
	public:
		explicit LinksView(Editor& _editor);
		~LinksView() override;

		LinksView(const LinksView&) = delete;
		LinksView& operator=(const LinksView&) = delete;

		// The lines and the menu as the Controller holds them now
		void refresh();

	private:
		void timerCallback() override;
		void choose(bool _choke, uint8_t _target);
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		int m_shownTrack = -1;
		int m_shownLink = -1;
		int m_shownChoke = -1;
	};
}
