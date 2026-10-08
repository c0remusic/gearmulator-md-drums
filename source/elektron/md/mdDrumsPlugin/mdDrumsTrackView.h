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

	// The v22 skin's behaviour until the slices that give it more (tickets 17 and 18): the shown Track, chosen by a click
	// on a list row, a Mix strip, a send or by ‹ ›, and what the views say about it (its row and strip lit, "Track 01",
	// the machine's family, where it plays, the machine's names for SYN1-8 and in the LFO's menu); the overlays opened
	// and closed (the machine browser, the LFO's target menu, the Kits); and the Size control, which steps the window
	// through 75, 100, 125 and 150 % on TUS's own "scale" setting. Everything runs on the message thread.
	class TrackView
	{
	public:
		enum class Overlay
		{
			None,
			Browser,
			LfoMenu,
			Kits
		};

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

		Overlay getOverlay() const { return m_overlay; }
		void setOverlay(Overlay _overlay);

	private:
		void onPartChanged(uint8_t _part);
		void updateMachine() const;
		void updateOutput() const;
		void updateLfoMenu() const;
		void updateSize() const;
		void show(const std::string& _id, bool _visible) const;
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		uint8_t m_part = 0;
		Overlay m_overlay = Overlay::None;
		int m_machineBeforeBrowser = -1;
		baseLib::EventListener<uint8_t> m_onPartChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onMachineChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onOutChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onLfoTrackChanged;
	};
}
