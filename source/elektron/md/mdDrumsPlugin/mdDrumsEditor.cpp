#include "mdDrumsEditor.h"

#include "mdDrumsGlyph.h"
#include "mdDrumsKnob.h"
#include "mdDrumsMeterView.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "skins.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/CoreInstance.h"
#include "RmlUi/Core/ElementInstancer.h"
#include "RmlUi/Core/Factory.h"

#include <algorithm>

namespace mdDrums
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	Editor::~Editor()
	{
		m_meterView.reset();
		m_trackView.reset();
	}

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();
		if(!findChild("page_track", false))
			return;
		m_trackView = std::make_unique<TrackView>(*this);
		// Only an mdDrums::Processor makes this editor
		m_meterView = std::make_unique<MeterView>(*this, static_cast<Processor&>(getProcessor()).getTelemetry(),
			getAcceleratedRefreshRateHz());
	}

	int Editor::getAcceleratedRefreshRateHz() const
	{
		const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
		if(!display || !display->verticalFrequencyHz)
			return 60;
		return std::clamp(juce::roundToInt(*display->verticalFrequencyHz), 30, 300);
	}

	void Editor::onRmlContextCreated(juceRmlUi::RmlComponent& _rmlComponent, Rml::Context& _context)
	{
		// Before the document loads: its <vknob> and <vglyph> elements
		static Rml::ElementInstancerGeneric<Knob> knobs;
		static Rml::ElementInstancerGeneric<Glyph> glyphs;
		auto& factory = *_context.GetCoreInstance().factory;
		factory.RegisterElementInstancer("vknob", &knobs);
		factory.RegisterElementInstancer("vglyph", &glyphs);

		jucePluginEditorLib::Editor::onRmlContextCreated(_rmlComponent, _context);
	}

	EditorState::EditorState(Processor& _processor)
		: jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
	{
		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* EditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new Editor(m_processor, _skin);
	}
}
