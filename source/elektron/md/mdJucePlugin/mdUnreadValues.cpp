#include "mdUnreadValues.h"

#include "mdController.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <sstream>

namespace mdJucePlugin
{
	namespace
	{
		// The part an element edits, from the data model it sits in (as RmlParameterBinding finds it)
		uint8_t partOf(Rml::Element& _element, const uint8_t _currentPart)
		{
			for(auto* element = &_element; element; element = element->GetParentNode())
			{
				const auto model = element->GetAttribute("data-model", std::string());
				if(model.empty())
					continue;
				if(model.size() > 4 && model.compare(0, 4, "part") == 0 && model != "partCurrent")
				{
					try
					{
						return static_cast<uint8_t>(std::stoul(model.substr(4)));
					}
					catch(...)
					{
						return _currentPart;
					}
				}
				return _currentPart;
			}
			return _currentPart;
		}
	}

	UnreadValues::UnreadValues(Controller& _controller, Rml::Element& _document)
		: m_controller(_controller)
	{
		if(auto* editor = _document.GetElementById("mdEditor"))
			collect(*editor);
	}

	bool UnreadValues::addValue(Control& _control, const std::string& _parameter) const
	{
		const auto* parameter = m_controller.getParameter(_parameter, 0);
		if(!parameter)
			return false;
		const auto& description = parameter->getDescription();
		_control.values.emplace_back(description.page, description.index);
		return true;
	}

	void UnreadValues::collect(Rml::Element& _element)
	{
		const auto name = _element.GetAttribute("param", std::string());
		if(!name.empty())
		{
			Control control{&_element, {}, partOf(_element, CurrentPart)};
			if(addValue(control, name))
			{
				m_controls.push_back(control);
				// The SON view's value line under a control
				if(_element.GetId() == "mdEdCtl_" + name)
				{
					if(auto* document = _element.GetOwnerDocument())
					{
						if(auto* value = document->GetElementById("mdEdVal_" + name))
						{
							control.element = value;
							m_controls.push_back(control);
						}
					}
				}
			}
		}

		const auto shown = _element.GetAttribute("data-unread", std::string());
		if(!shown.empty())
		{
			Control control{&_element, {}, partOf(_element, CurrentPart)};
			std::istringstream names(shown);
			std::string parameter;
			while(names >> parameter)
				addValue(control, parameter);
			if(!control.values.empty())
				m_controls.push_back(control);
		}

		for(int i = 0; i < _element.GetNumChildren(); ++i)
			collect(*_element.GetChild(i));
	}

	bool UnreadValues::update()
	{
		// Read the revision first: a change made while reading bumps it again
		const auto revision = m_controller.getValueStateRevision();
		const auto currentPart = m_controller.getCurrentPart();
		if(revision == m_shownRevision && currentPart == m_shownPart)
			return false;
		m_shownRevision = revision;
		m_shownPart = currentPart;
		bool changed = false;
		for(auto& control : m_controls)
		{
			const auto part = control.part == CurrentPart ? currentPart : control.part;
			bool unread = false;
			for(const auto& [page, index] : control.values)
				unread |= !m_controller.isValueKnown(page, part, index);
			if(unread == control.unread)
				continue;
			control.unread = unread;
			control.element->SetClass("mdEdUnread", unread);
			changed = true;
		}
		return changed;
	}
}
