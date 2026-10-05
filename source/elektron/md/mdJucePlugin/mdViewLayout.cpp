#include "mdViewLayout.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

namespace mdJucePlugin
{
	namespace
	{
		void setDisplayed(Rml::Element* _element, const bool _displayed)
		{
			if(!_element)
				return;
			if(_displayed)
				_element->RemoveProperty(Rml::PropertyId::Display);
			else
				_element->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::None);
		}
	}

	ViewLayout::ViewLayout(Rml::Element& _document)
		: m_document(_document)
	{
		m_panel = _document.GetElementById("mdFrontPanel");
		m_editor = _document.GetElementById("mdEditor");
		m_switchPanel = _document.GetElementById("mdViewPanel");
		m_switchEditor = _document.GetElementById("mdViewEditor");

		if(m_switchPanel)
		{
			juceRmlUi::EventListener::Add(m_switchPanel, Rml::EventId::Click, [this](Rml::Event&)
			{
				showEditor(false);
			});
		}
		if(m_switchEditor)
		{
			juceRmlUi::EventListener::Add(m_switchEditor, Rml::EventId::Click, [this](Rml::Event&)
			{
				showEditor(true);
			});
		}

		// A category (JOUER, SON, MIX...) is a page of the editor.
		Rml::ElementList categories;
		_document.GetElementsByClassName(categories, "mdEdCat");
		for(auto* category : categories)
		{
			juceRmlUi::EventListener::Add(category, Rml::EventId::Click, [this](Rml::Event&)
			{
				showEditor(true);
			});
		}

		// The context is resized with the window; the body follows it (RmlComponent).
		juceRmlUi::EventListener::Add(&_document, Rml::EventId::Resize, [this](Rml::Event&)
		{
			update();
		});

		m_stacked = documentHeight() >= g_stackedHeight;
		apply();
	}

	bool ViewLayout::update()
	{
		const bool stacked = documentHeight() >= g_stackedHeight;
		if(stacked == m_stacked)
			return false;
		m_stacked = stacked;
		apply();
		return true;
	}

	void ViewLayout::showEditor(const bool _editor)
	{
		m_editorShown = _editor;
		apply();
	}

	float ViewLayout::documentHeight() const
	{
		// The window height in dp, before the body's layout catches up with it.
		const auto* context = m_document.GetContext();
		if(!context || context->GetDensityIndependentPixelRatio() <= 0.0f)
			return 0.0f;
		return static_cast<float>(context->GetDimensions().y) / context->GetDensityIndependentPixelRatio();
	}

	void ViewLayout::apply()
	{
		setDisplayed(m_panel, m_stacked || !m_editorShown);
		setDisplayed(m_editor, m_stacked || m_editorShown);
		for(auto* button : {m_switchPanel, m_switchEditor})
		{
			if(button)
				button->SetClass("mdEdHidden", m_stacked);
		}
		// Toggle buttons flip themselves on click; set both from the state.
		if(m_switchPanel)
			juceRmlUi::ElemButton::setChecked(m_switchPanel, !m_editorShown);
		if(m_switchEditor)
			juceRmlUi::ElemButton::setChecked(m_switchEditor, m_editorShown);
	}
}
