#pragma once

#include "baseLib/event.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pluginLib
{
	class Parameter;
}

namespace Rml
{
	class Element;
	class Event;
}

namespace mdDrums
{
	class Editor;

	// The v22 skin's behaviour until the slices that give it more (tickets 17 and 18): the shown Track, chosen by a click
	// on a list row, a Mix strip, a send or by ‹ ›, and what the views say about it (its row and strip lit, "Track 01",
	// the machine's family, where it plays, the machine's names for SYN1-8 and in the LFO's menu); the overlays opened
	// and closed (the machine browser, the LFO's target menu, the Kits; Esc closes them, the browser as Cancel), and the
	// browser's "Listen while choosing", with which a machine chosen plays the Track; playing the Track (the play key and
	// Space, at the head band's velocity, which a drag sets) and stepping its machine (the wheel or the arrows on its
	// name, the arrows in the browser: ticket 23); and the Size control, which steps the window through 75, 100, 125 and
	// 150 % on TUS's own "scale" setting. Everything runs on the message thread.
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
		static constexpr uint8_t ListenVelocity = 100;

		explicit TrackView(Editor& _editor);
		~TrackView();

		TrackView(const TrackView&) = delete;
		TrackView& operator=(const TrackView&) = delete;

		void showTrack(uint8_t _track) const;
		void stepTrack(int _delta) const;
		void stepSize() const;

		Overlay getOverlay() const { return m_overlay; }
		void setOverlay(Overlay _overlay);
		// Esc: closes the open overlay, the browser as Cancel does; false when none is open
		bool escape();

		// The browser's "Listen while choosing", kept in the plug-in's settings: on, a machine chosen plays the Track
		bool isListening() const;
		void setListening(bool _on) const;

		// The head band's velocity, 1-127, kept in the plug-in's settings: what the play key and Space play with
		static constexpr float PixelsPerVelocityStep = 1.5f;
		int getPlayVelocity() const;
		void setPlayVelocity(int _velocity) const;
		void play() const;

		// The shown Track's machine, the next or previous in the browser's order (wrapping), or the first of the next or
		// previous family; listening, the Track plays
		void stepMachine(int _delta) const;
		void stepFamily(int _delta) const;

		// A key, before the stack's handling: Space plays (unless a text field has the focus); with the browser open the
		// arrows step its machines and families and Enter keeps it; on the machine's name ↑ ↓ step it. False: not handled
		bool key(Rml::Event& _event);

	private:
		void choose(int _machine) const;
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
		std::vector<int> m_machines;	// the browser's, in its order
		float m_dragY = 0.0f;
		int m_dragFrom = 0;
		baseLib::EventListener<uint8_t> m_onPartChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onMachineChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onOutChanged;
		baseLib::EventListener<pluginLib::Parameter*> m_onLfoTrackChanged;
	};
}
