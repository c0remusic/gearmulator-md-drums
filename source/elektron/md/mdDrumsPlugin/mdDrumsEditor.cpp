#include "mdDrumsEditor.h"

#include "mdDrumsFilterView.h"
#include "mdDrumsFrame.h"
#include "mdDrumsGlyph.h"
#include "mdDrumsHitView.h"
#include "mdDrumsKitsView.h"
#include "mdDrumsKnob.h"
#include "mdDrumsLfoView.h"
#include "mdDrumsLinksView.h"
#include "mdDrumsMeterView.h"
#include "mdDrumsProcessor.h"
#include "mdDrumsStatusView.h"
#include "mdDrumsTrackView.h"
#include "mdDrumsController.h"

#include "skins.h"
#include "juceRmlUi/rmlMenu.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"

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
		, m_clock([] { return juce::Time::getMillisecondCounterHiRes(); })
		, m_frameTimer([this] { onFrameTimer(); })
	{
	}

	Editor::~Editor()
	{
		// No frame from here on (step needs TrackView): the views go first, the component with the base
		m_frameTimer.stopTimer();
		m_onPreUpdate.reset();
		m_trackView.reset();
		m_statusView.reset();
		m_linksView.reset();
		m_kitsView.reset();
		m_lfoView.reset();
		m_filterView.reset();
		m_hitView.reset();
		m_meterView.reset();
	}

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();
		// A skin without the Track tab, such as a user's own, gets no view
		if(!findChild("page_track", false))
			return;
		auto* component = getRmlComponent();
		if(!component)
			return;
		m_trackView = std::make_unique<TrackView>(*this);
		auto& telemetry = processor().getTelemetry();
		m_meterView = std::make_unique<MeterView>(*this, telemetry);
		std::vector<std::filesystem::path> romFolders;
		for(const auto& folder : processor().romFolders())
			romFolders.emplace_back(folder);
		m_hitView = std::make_unique<HitView>(*this, telemetry, std::move(romFolders));
		m_filterView = std::make_unique<FilterView>(*this);
		m_lfoView = std::make_unique<LfoView>(*this);
		m_kitsView = std::make_unique<KitsView>(*this);
		m_linksView = std::make_unique<LinksView>(*this);
		m_statusView = std::make_unique<StatusView>(*this);

		// The frame before every RmlUi update (under its lock: the views' writes are laid out and drawn in that
		// update); the first one now, so that the first render shows the Track and not the skin's defaults; then the
		// timer
		m_onPreUpdate.set(component->evPreUpdate, [this](juceRmlUi::RmlComponent* const&) { step(); });
		frame();
		const auto hz = getAcceleratedRefreshRateHz();
		m_framePeriodMs = 1000.0 / hz;
		m_frameTimer.startTimerHz(hz);

		// Esc on the context's root, in the capture phase: before the stack's listener on the document, even when the
		// document itself has the key
		if(auto* document = component->getDocument())
		{
			auto* root = document->GetParentNode() ? document->GetParentNode() : document;
			juceRmlUi::EventListener::Add(root, Rml::EventId::Keydown, [this](Rml::Event& _event)
			{
				const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
				bool handled;
				if(key == Rml::Input::KI_ESCAPE)
					handled = escape();
				else if(juceRmlUi::helper::getKeyModCommand(_event))
					handled = key == Rml::Input::KI_S && m_kitsView && (m_kitsView->save(), true);
				else
					handled = (m_kitsView && m_kitsView->key(_event)) || (m_trackView && m_trackView->key(_event));
				if(handled)
					_event.StopPropagation();
			}, true);
		}
	}

	bool Editor::frame()
	{
		if(!step())
			return false;
		// A canvas repainted asks for its render itself; the rest of the document does not
		if(auto* component = getRmlComponent())
			component->enqueueUpdateOnce();
		return true;
	}

	bool Editor::step()
	{
		JUCE_ASSERT_MESSAGE_THREAD
		auto* component = getRmlComponent();
		if(!component || !m_trackView || m_inFrame)
			return false;
		const juce::ScopedValueSetter<bool> inFrame(m_inFrame, true);
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);

		Frame f;
		f.now = m_clock();
		// The views index their Tracks by it: a part out of range (the MCP server sets any) shows the last Track
		f.shown = static_cast<uint8_t>(std::min<int>(controller().getCurrentPart(), Controller::TrackCount - 1));
		f.shownChanged = static_cast<int>(f.shown) != m_lastShown;
		f.resumed = m_lastFrameMs < 0.0 || f.now - m_lastFrameMs > ResumeMs;
		m_lastShown = f.shown;
		m_lastFrameMs = f.now;

		// Every view steps, whatever the others wrote. TrackView first: a Track shown closes the browser, which the Kits'
		// head band follows
		bool wrote = m_trackView->step(f);
		wrote |= m_linksView->step(f);
		wrote |= m_kitsView->step(f);
		wrote |= m_filterView->step(f);
		wrote |= m_lfoView->step(f);
		wrote |= m_hitView->step(f);
		wrote |= m_meterView->step(f);
		wrote |= m_statusView->step(f);

		++m_frameCount.frames;
		if(wrote)
			++m_frameCount.wrote;
		return wrote;
	}

	void Editor::onFrameTimer()
	{
		// Nothing shows (the window closed or minimised): the RmlUi updates that go on meanwhile run the frame
		const auto* component = getRmlComponent();
		if(!component || !component->isShowing())
			return;
		// An RmlUi update ran one within half a period
		if(m_clock() - m_lastFrameMs < m_framePeriodMs * 0.5)
			return;
		frame();
	}

	void Editor::setClock(std::function<double()> _clock)
	{
		m_clock = std::move(_clock);
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		jucePluginEditorLib::Editor::setCurrentPart(_part);
		frame();
	}

	bool Editor::escape() const
	{
		if(m_lfoView && m_lfoView->escape())
			return true;
		if(m_kitsView && m_kitsView->escape())
			return true;
		return m_trackView && m_trackView->escape();
	}

	Processor& Editor::processor() const
	{
		// Only an mdDrums::Processor makes this editor
		return static_cast<Processor&>(getProcessor());
	}

	Controller& Editor::controller() const
	{
		return processor().controller();
	}

	Rml::Element* Editor::element(const std::string& _id) const
	{
		if(const auto it = m_elements.find(_id); it != m_elements.end())
			return it->second;
		auto* e = findChild(_id, false);
		if(e)
			m_elements.emplace(_id, e);
		else
			++m_elementMisses;
		return e;
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

	void EditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		jucePluginEditorLib::PluginEditorState::initContextMenu(_menu);
		_menu.addSeparator();
		_menu.addEntry("Prepare Push list: a page per Track (Live in Configure mode)", [this]
		{
			static_cast<Controller&>(m_processor.getController()).preparePushList();
		});
		_menu.addEntry("Prepare Push list: Track shown and master (Live in Configure mode)", [this]
		{
			static_cast<Controller&>(m_processor.getController()).preparePushRelays();
		});
		_menu.addSeparator();
		_menu.addEntry("Load Machinedrum flash image...", [this]
		{
			if(auto* editor = dynamic_cast<Editor*>(getEditor()))
				if(auto* status = editor->getStatusView())
					status->chooseFlashImage();
		});
		auto& config = m_processor.getConfig();
		_menu.addEntry("Check for updates once a day", StatusView::checksForUpdates(config), [&config]
		{
			config.setValue(StatusView::CheckKey, !StatusView::checksForUpdates(config));
		});
	}
}
