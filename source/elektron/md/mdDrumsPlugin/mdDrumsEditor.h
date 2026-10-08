#pragma once

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginEditorState.h"

#include <memory>

namespace mdDrums
{
	class FilterView;
	class HitView;
	class LfoView;
	class MeterView;
	class Processor;
	class TrackView;

	// The editor over the skin: the v22 one (skins/mdDrums, ticket 17) with its <vknob> and <vglyph> elements and the
	// Track tab's behaviour (TrackView), or the minimal one, which binds every parameter until ticket 18
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

		TrackView* getTrackView() const { return m_trackView.get(); }
		MeterView* getMeterView() const { return m_meterView.get(); }
		HitView* getHitView() const { return m_hitView.get(); }
		FilterView* getFilterView() const { return m_filterView.get(); }
		LfoView* getLfoView() const { return m_lfoView.get(); }

		// Esc, before the stack's own (which opens its settings): disarms ASSIGN, else closes the open overlay
		bool escape() const;

	private:
		std::unique_ptr<TrackView> m_trackView;
		std::unique_ptr<MeterView> m_meterView;
		std::unique_ptr<HitView> m_hitView;
		std::unique_ptr<FilterView> m_filterView;
		std::unique_ptr<LfoView> m_lfoView;
	};

	class EditorState final : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit EditorState(Processor& _processor);

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
	};
}
