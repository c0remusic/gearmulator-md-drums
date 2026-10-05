#pragma once

#include <vector>

namespace Rml
{
	class Element;
}

namespace mdJucePlugin
{
	// Which of the front panel and the editor the window shows. The top bar's
	// FACE AVANT and ÉDITEUR buttons switch between them, and a category button
	// opens the editor. A window tall enough for both (see g_stackedHeight)
	// shows them one above the other and hides the switch.
	class ViewLayout
	{
	public:
		// Top bar, front panel and a full editor: 36 + 570 + 570 dp.
		static constexpr float g_stackedHeight = 1176.0f;

		explicit ViewLayout(Rml::Element& _document);

		// Stacks or unstacks the views when the window height crossed
		// g_stackedHeight; runs on every resize. Returns true when it changed the DOM.
		bool update();

		void showEditor(bool _editor);
		bool isEditorShown() const { return m_editorShown; }
		bool isStacked() const { return m_stacked; }

	private:
		float documentHeight() const;
		void apply();

		Rml::Element& m_document;
		Rml::Element* m_panel = nullptr;
		Rml::Element* m_editor = nullptr;
		Rml::Element* m_switchPanel = nullptr;
		Rml::Element* m_switchEditor = nullptr;
		bool m_editorShown = false;
		bool m_stacked = false;
	};
}
