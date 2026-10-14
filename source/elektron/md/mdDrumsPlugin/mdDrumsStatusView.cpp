#include "mdDrumsStatusView.h"

#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"
#include "mdDrumsFilterView.h"
#include "mdDrumsProcessor.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

namespace mdDrums
{
	namespace
	{
		constexpr const char* g_timeKey = "updateCheckTime";
		constexpr const char* g_latestKey = "updateLatest";
		constexpr const char* g_urlKey = "updateUrl";
		constexpr juce::int64 g_checkSeconds = 24 * 60 * 60;
		constexpr double g_unplayedSeconds = 5.0;
		constexpr double g_flashErrorSeconds = 8.0;

		double seconds()
		{
			return juce::Time::getMillisecondCounterHiRes() / 1000.0;
		}

		// A note's name as Live writes it: 60 is C3
		std::string noteName(const int _note)
		{
			return juce::MidiMessage::getMidiNoteName(_note, true, true, 3).toStdString();
		}
	}

	StatusView::StatusView(Editor& _editor) : m_editor(_editor)
	{
		m_editor.addClick("status", [this](Rml::Event&) { click(); });
		if(auto* version = find("version"))
			version->SetInnerRML(MdDrumsVersion);

		// Notes played before the editor opened stay unsaid
		m_unplayedSeen = m_editor.processor().getTelemetry().unplayedCount();

		// The update check, at most once a day; meanwhile what the last one found
		auto& config = m_editor.processor().getConfig();
		if(checksForUpdates(config))
		{
			offer({config.getValue(g_latestKey).toStdString(), config.getValue(g_urlKey).toStdString()});
			const auto now = juce::Time::currentTimeMillis() / 1000;
			if(now - config.getValue(g_timeKey).getLargeIntValue() >= g_checkSeconds)
			{
				// When it tried, whatever it finds: offline or refused, the next editor waits a day too
				config.setValue(g_timeKey, now);
				m_check->start();
			}
		}
		refresh();
		startTimerHz(10);
	}

	StatusView::~StatusView()
	{
		stopTimer();
	}

	bool StatusView::checksForUpdates(const juce::PropertiesFile& _config)
	{
		return _config.getBoolValue(CheckKey, true);
	}

	Rml::Element* StatusView::find(const std::string& _id) const
	{
		return m_editor.element(_id);
	}

	std::string StatusView::text() const
	{
		return m_shown;
	}

	void StatusView::offer(const updates::Release& _release)
	{
		if(updates::compare(_release.version, MdDrumsVersion) > 0)
			m_newer = _release;
	}

	void StatusView::timerCallback()
	{
		if(const auto release = updates::Check::latest(); release && release->version != m_latestSeen)
		{
			m_latestSeen = release->version;
			auto& config = m_editor.processor().getConfig();
			config.setValue(g_latestKey, juce::String(release->version));
			config.setValue(g_urlKey, juce::String(release->url));
			offer(*release);
		}
		refresh();
	}

	std::pair<std::string, StatusView::Action> StatusView::pressing()
	{
		const auto now = seconds();
		const bool device = m_editor.processor().hasDevice();
		if(now < m_flashErrorUntil)
			return {device ? m_flashError : m_flashError + ": choose another", Action::ChooseFlash};
		if(!device)
			return {"No Machinedrum flash image: choose the file", Action::ChooseFlash};

		const auto& telemetry = m_editor.processor().getTelemetry();
		if(const auto count = telemetry.unplayedCount(); count != m_unplayedSeen)
		{
			m_unplayedSeen = count;
			m_unplayedUntil = now + g_unplayedSeconds;
		}
		if(now < m_unplayedUntil)
		{
			return {noteName(telemetry.lastUnplayed()) + " plays nothing: Tracks are " + noteName(Device::FirstNote) + " to "
				+ noteName(Device::FirstNote + Engine::TrackCount - 1), Action::None};
		}

		if(m_newer)
			return {"MD Drums " + m_newer->version + " is out", Action::OpenRelease};
		return {{}, Action::None};
	}

	void StatusView::refresh()
	{
		const auto [text, action] = pressing();
		if(text == m_shown && action == m_action)
			return;
		auto* component = m_editor.getRmlComponent();
		if(!component)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		auto* status = find("status");
		if(!status)
			return;
		m_shown = text;
		m_action = action;
		status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		status->SetClass("shown", !text.empty());
		status->SetClass("action", action != Action::None);
	}

	void StatusView::click()
	{
		switch(m_action)
		{
		case Action::ChooseFlash:
			chooseFlashImage();
			break;
		case Action::OpenRelease:
			if(m_newer)
				juce::URL(m_newer->url).launchInDefaultBrowser();
			break;
		case Action::None:
			break;
		}
	}

	void StatusView::chooseFlashImage()
	{
		// A FileChooser deleted drops its callback: the one this replaces, or this view's when the editor closes
		m_chooser = std::make_unique<juce::FileChooser>("Choose the Machinedrum UW flash image (OS 1.63, 8 MB)",
			juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _chooser)
			{
				const auto file = _chooser.getResult();
				if(file == juce::File())
					return;
				// JUCE's paths are UTF-8, which a path made from a std::string would read in the ANSI code page
				const auto path = std::filesystem::u8path(file.getFullPathName().toStdString());
				m_flashError = m_editor.processor().loadFlashImage(path);
				m_flashErrorUntil = m_flashError.empty() ? 0.0 : seconds() + g_flashErrorSeconds;
				refresh();
				// A new Device brings the OS's tables, which the Filter and EQ screen waited for
				auto* filter = m_editor.getFilterView();
				auto* component = m_editor.getRmlComponent();
				if(!m_flashError.empty() || !filter || !component)
					return;
				juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
				filter->refresh();
			});
	}
}
