#pragma once

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace mdDrums
{
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
	class Editor final : public jucePluginEditorLib::Editor
	{
	public:
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

		// Runs _func under the document's access lock, which the OpenGL thread renders under, for a view that changes
		// the document from outside an RmlUi event (whose dispatch holds the lock): a timer, a parameter's notification
		// from the message loop, the Track shown changed by the Controller's timer or a state restored, a file chosen.
		// The lock is recursive. Without a component nothing runs.
		template<typename T> void withDocumentLocked(const T& _func) const
		{
			auto* component = getRmlComponent();
			if(!component)
				return;
			juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
			_func();
		}

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
		std::unique_ptr<TrackView> m_trackView;
		std::unique_ptr<MeterView> m_meterView;
		std::unique_ptr<HitView> m_hitView;
		std::unique_ptr<FilterView> m_filterView;
		std::unique_ptr<LfoView> m_lfoView;
		std::unique_ptr<KitsView> m_kitsView;
		std::unique_ptr<LinksView> m_linksView;
		std::unique_ptr<StatusView> m_statusView;
		mutable std::unordered_map<std::string, Rml::Element*> m_elements;	// element()'s, found ones only
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
