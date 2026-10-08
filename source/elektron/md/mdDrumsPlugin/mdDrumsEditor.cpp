#include "mdDrumsEditor.h"

#include "mdDrumsFilterView.h"
#include "mdDrumsGlyph.h"
#include "mdDrumsHitView.h"
#include "mdDrumsKnob.h"
#include "mdDrumsLfoView.h"
#include "mdDrumsMeterView.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsTrackView.h"

#include "skins.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
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
		m_lfoView.reset();
		m_filterView.reset();
		m_hitView.reset();
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
		auto& processor = static_cast<Processor&>(getProcessor());
		auto& telemetry = processor.getTelemetry();
		m_meterView = std::make_unique<MeterView>(*this, telemetry, getAcceleratedRefreshRateHz());
		m_hitView = std::make_unique<HitView>(*this, telemetry, getAcceleratedRefreshRateHz());
		m_filterView = std::make_unique<FilterView>(*this, processor.getFxTables());
		m_lfoView = std::make_unique<LfoView>(*this);

		// Esc on the context's root, in the capture phase: before the stack's listener on the document, even when the
		// document itself has the key
		if(auto* component = getRmlComponent())
			if(auto* document = component->getDocument())
			{
				auto* root = document->GetParentNode() ? document->GetParentNode() : document;
				juceRmlUi::EventListener::Add(root, Rml::EventId::Keydown, [this](Rml::Event& _event)
				{
					if(juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_ESCAPE && escape())
						_event.StopPropagation();
				}, true);
			}
	}

	bool Editor::escape() const
	{
		if(m_lfoView && m_lfoView->escape())
			return true;
		return m_trackView && m_trackView->escape();
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
