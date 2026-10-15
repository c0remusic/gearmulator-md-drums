#pragma once

#include "mdDrumsUpdates.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Rml
{
	class Element;
}

namespace mdDrums
{
	class Editor;
	struct Frame;

	// The top bar's version and status line (ticket 35 of the editor map). The version stays beside the logo; the status
	// line says the most pressing of these, or nothing:
	// - a flash image refused, for a few seconds; no flash image: MD Drums plays nothing until one is chosen. A click
	//   opens a file chooser (chooseFlashImage);
	// - a note that plays nothing, for a few seconds: Tracks are C1 to D#2 in Live (notes 36-51);
	// - a newer release on GitHub: a click opens its page. The check runs when an editor opens, at most once a day,
	//   unless the settings menu turns it off; the config keeps when it last tried and what it found.
	// The editor's frame follows them RefreshHz times a second, on its clock, and touches the document only when the line
	// changes.
	class StatusView
	{
	public:
		static constexpr const char* CheckKey = "checkForUpdates";
		static constexpr int RefreshHz = 10;

		explicit StatusView(Editor& _editor);
		~StatusView();

		StatusView(const StatusView&) = delete;
		StatusView& operator=(const StatusView&) = delete;

		// The editor's frame: true when it changed the document
		bool step(const Frame& _frame);

		// Opens a file chooser for the flash image, then gives the file to Processor::loadFlashImage
		void chooseFlashImage();
		// The line as it stands, for the tests
		std::string text() const;

		// Whether the update check runs, and turning it on or off (the settings menu)
		static bool checksForUpdates(const juce::PropertiesFile& _config);

	private:
		enum class Action
		{
			None,
			ChooseFlash,
			OpenRelease
		};

		// The most pressing thing to say at _seconds (the frame's clock), and what a click on it does
		std::pair<std::string, Action> pressing(double _seconds);
		// A release the check found or the config kept, said when it is newer than this build
		void offer(const updates::Release& _release);
		void click();
		Rml::Element* find(const std::string& _id) const;

		Editor& m_editor;
		juce::SharedResourcePointer<updates::Check> m_check;
		Action m_action = Action::None;
		std::string m_shown;
		uint32_t m_unplayedSeen = 0;
		double m_unplayedUntil = 0.0;
		std::string m_flashError;
		bool m_flashErrorNew = false;	// said from the next frame on, for a few seconds
		double m_flashErrorUntil = 0.0;
		double m_nextMs = 0.0;			// when the frame looks again
		std::string m_latestSeen;					// the check's last release this view took
		std::optional<updates::Release> m_newer;	// newer than this build
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
