#pragma once

#include "baseLib/event.h"

#include <array>
#include <cstdint>
#include <string>

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;

	// The Track tab's behaviour in the static v22 skin (ticket 17): the shown Track, chosen by a click on a list row or
	// by ‹ ›, and what the head band and the bands say about it (its row lit, "Track 01", the machine's family, where it
	// plays, the machine's names for SYN1-8); and the Size control, which steps the window through 75, 100, 125 and
	// 150 % on TUS's own "scale" setting. Everything runs on the message thread, from parameter and part events.
	class TrackView
	{
	public:
		static constexpr std::array<int, 4> Scales{75, 100, 125, 150};
		static constexpr int RowHeight = 40;
		static constexpr int ListTop = 176;

		explicit TrackView(Editor& _editor);
		~TrackView();

		TrackView(const TrackView&) = delete;
		TrackView& operator=(const TrackView&) = delete;

		void showTrack(uint8_t _track) const;
		void stepTrack(int _delta) const;
		void stepSize() const;

	private:
		void onPartChanged(uint8_t _part);
		void updateMachine() const;
		void updateOutput() const;
		void updateSize() const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		uint8_t m_part = 0;
		baseLib::EventListener<uint8_t> m_onPartChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onMachineChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onOutChanged;
	};
}
