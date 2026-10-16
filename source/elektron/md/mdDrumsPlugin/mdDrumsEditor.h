#pragma once

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"

#include "baseLib/event.h"

#include "juce_events/juce_events.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace mdDrums
{
	// A file a JUCE file chooser gave, as a path: JUCE's paths are UTF-8, which a path made from a std::string would
	// read in the ANSI code page
	inline std::filesystem::path pathOf(const juce::File& _file)
	{
		return std::filesystem::u8path(_file.getFullPathName().toStdString());
	}

	class Controller;
	class FilterView;
	class HitView;
	class KitsView;
	class LfoView;
	class LinksView;
	class MeterView;
	class Processor;
	class StatusView;
	class TrackView;

	// The editor over the skin: the v22 one (skins/mdDrums, ticket 17) with its <vknob> and <vglyph> elements and the
	// Track tab's behaviour (TrackView). A skin without the Track tab (#page_track), such as a user's own, gets the
	// stack's editor alone.
	//
	// The views change the document in two places only, both under the document's access lock, which the OpenGL thread
	// renders under: an RmlUi event (a click, a key, a drag: they write at once), and the editor's frame, where each
	// view reads what it shows (the parameters, the Telemetry, the Controller, the Bank), compares it with what it drew
	// and writes only what differs. The frame runs before every RmlUi update, so that what an event changed shows in
	// the frame that draws the event; at the display's rate while the window shows, for what changes by itself (the
	// host's automation, the Telemetry, a Track shown by Push or a state restored); and at once when a click shows
	// another Track or a file has been chosen.
	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
		// A pause longer than this between two frames makes the next one Frame::resumed
		static constexpr double ResumeMs = 1000.0;

		Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~Editor() override;

		void create() override;

		std::pair<std::string, std::string> getDemoRestrictionText() const override { return {}; }

		// As often as the display refreshes (164 Hz on the reference PC), and only when something changed
		int getAcceleratedRefreshRateHz() const override;

		void onRmlContextCreated(juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context) override;

		// MD Drums' own Processor, the only one that makes this editor, and its Controller
		Processor& processor() const;
		Controller& controller() const;

		// The skin's element with this ID, or null. Found once, then kept: an element with an ID lives as long as the
		// editor, so no view may rewrite (SetInnerRML) or remove an element that holds one. A missing ID is looked for
		// again each time.
		Rml::Element* element(const std::string& _id) const;
		// The lookups that found nothing: a frame should cause none
		uint32_t elementMisses() const { return m_elementMisses; }

		// The editor's frame, now: asks RmlUi for a render when a view wrote, and is then true. Nothing happens without
		// the views, or inside a frame (a view's step never shows another Track: the next frame sees what it changed).
		bool frame();
		// The frame's clock, in ms: a test's, or the display's (an empty function)
		void setClock(std::function<double()> _clock);
		double lastFrameMs() const { return m_lastFrameMs; }

		// The Track shown, which the views draw at once: a handler that follows in the same event acts on it
		void setCurrentPart(uint8_t _part) override;

		TrackView* getTrackView() const { return m_trackView.get(); }
		MeterView* getMeterView() const { return m_meterView.get(); }
		HitView* getHitView() const { return m_hitView.get(); }
		FilterView* getFilterView() const { return m_filterView.get(); }
		LfoView* getLfoView() const { return m_lfoView.get(); }
		KitsView* getKitsView() const { return m_kitsView.get(); }
		LinksView* getLinksView() const { return m_linksView.get(); }
		StatusView* getStatusView() const { return m_statusView.get(); }

		// Esc, before the stack's own (which opens its settings): disarms ASSIGN, else leaves a Kit's renaming or a second-
		// click question, else closes the open overlay. Ctrl+S saves the Kit played. The other keys go to KitsView::key
		// (the Kits' arrows and Enter, a name typed), then TrackView::key (Space, the browser's arrows and Enter, the
		// machine's arrows)
		bool escape() const;

	private:
		// The views stepped under the lock, without asking for a render: true when one wrote
		bool step();
		void onFrameTimer();

		// Destroyed in the reverse order: the frame's listener and timer first, then TrackView (without it no frame
		// steps), then the other views
		mutable std::unordered_map<std::string, Rml::Element*> m_elements;	// element()'s, found ones only
		mutable uint32_t m_elementMisses = 0;
		std::unique_ptr<MeterView> m_meterView;
		std::unique_ptr<HitView> m_hitView;
		std::unique_ptr<FilterView> m_filterView;
		std::unique_ptr<LfoView> m_lfoView;
		std::unique_ptr<KitsView> m_kitsView;
		std::unique_ptr<LinksView> m_linksView;
		std::unique_ptr<StatusView> m_statusView;
		std::unique_ptr<TrackView> m_trackView;

		std::function<double()> m_clock;
		double m_lastFrameMs = -1.0;
		int m_lastShown = -1;
		bool m_inFrame = false;
		juce::TimedCallback m_frameTimer;
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onPreUpdate;
	};

	class EditorState final : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit EditorState(Processor& _processor);

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
		// The settings menu's "Prepare Push list" (ticket 11 of the editor map), for Live in Configure mode
		void initContextMenu(juceRmlUi::Menu& _menu) override;
	};
}
