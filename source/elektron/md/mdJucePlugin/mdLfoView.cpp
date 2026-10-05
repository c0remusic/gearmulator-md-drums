#include "mdLfoView.h"

#include "mdController.h"

#include "mdLib/mdmachines.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlMenu.h"

#include "RmlUi/Core/Element.h"

#include <juce_graphics/juce_graphics.h>

#include <cmath>

namespace mdJucePlugin
{
	namespace
	{
		const juce::Colour g_waveColour(0xffff6b1f);
		const juce::Colour g_iconColour(0xfff4f1ea);
		const juce::Colour g_axisColour(0xff2e3034);

		std::string number(const unsigned _value)
		{
			return (_value < 10 ? "0" : "") + std::to_string(_value);
		}

		// A Machinedrum track parameter as the editor names it; one the machine does not use by its number
		std::string parameterName(const Controller& _controller, const uint8_t _track, const uint8_t _parameter)
		{
			const auto name = md::machines::machinedrumParameterName(_controller.getTrackMachine(_track), _parameter);
			return name.empty() ? "P" + std::to_string(_parameter + 1) : std::string(name);
		}

		void setText(Rml::Element* _element, const std::string& _text)
		{
			if(_element && _element->GetInnerRML() != _text)
				_element->SetInnerRML(_text);
		}
	}

	LfoView::LfoView(Controller& _controller, Rml::Element& _document) : m_controller(_controller)
	{
		m_root = _document.GetElementById("mdEdLfo");
		m_destination = _document.GetElementById("mdEdLfoDest");
		m_info = _document.GetElementById("mdEdLfoInfo");
		if(m_destination)
		{
			juceRmlUi::EventListener::Add(m_destination, Rml::EventId::Click, [this](const Rml::Event& _event)
			{
				openDestinationMenu(_event);
			});
		}
		for(uint8_t mode = 0; mode < m_updates.size(); ++mode)
		{
			m_updates[mode] = _document.GetElementById("mdEdLfoUpdate" + std::to_string(mode));
			if(!m_updates[mode])
				continue;
			juceRmlUi::EventListener::Add(m_updates[mode], Rml::EventId::Click, [this, mode](Rml::Event&)
			{
				m_controller.setTrackLfo(static_cast<uint8_t>(m_controller.getCurrentPart()), 4, mode);
				update();
			});
		}
		for(uint8_t n = 0; n < 2; ++n)
		{
			const auto id = "mdEdLfoShape" + std::to_string(n + 1);
			m_shapeNames[n] = _document.GetElementById(id + "Name");
			if(auto* cell = _document.GetElementById(id))
			{
				juceRmlUi::EventListener::Add(cell, Rml::EventId::Click, [this, n](const Rml::Event& _event)
				{
					openShapeMenu(_event, static_cast<uint8_t>(2 + n));
				});
			}
			if(auto* icon = _document.GetElementById(id + "Icon"))
			{
				m_icons[n] = juceRmlUi::ElemCanvas::create(icon);
				m_icons[n]->setRepaintGraphicsCallback([this, n](juce::Image& _image, juce::Graphics& _g)
				{
					_image.clear(_image.getBounds());
					if(!m_shown)
						return;
					const auto shape = n ? m_shown->shape2 : m_shown->shape1;
					const bool oneShot = shape == md::LfoSettings::Ramp || shape == md::LfoSettings::Exponential;
					paintWave(_image, _g, shape, shape, 0.0f, oneShot ? 1 : 2, true);
				});
			}
		}
		if(auto* area = _document.GetElementById("mdEdLfoWave"))
		{
			m_wave = juceRmlUi::ElemCanvas::create(area);
			m_wave->setRepaintGraphicsCallback([this](juce::Image& _image, juce::Graphics& _g)
			{
				_image.clear(_image.getBounds());
				if(m_shown)
					paintWave(_image, _g, m_shown->shape1, m_shown->shape2, static_cast<float>(std::max(m_shownMix, 0)) / 127.0f, 3, false);
			});
		}
	}

	const char* LfoView::shapeName(const uint8_t _shape)
	{
		constexpr const char* names[md::LfoSettings::ShapeCount] = {"TRIANGLE", "SCIE", "CARRÉ", "RAMPE", "EXPO", "ALÉA"};
		return _shape < md::LfoSettings::ShapeCount ? names[_shape] : "—";
	}

	float LfoView::shapeLevel(const uint8_t _shape, const float _phase, const uint32_t _period)
	{
		// What mdPlayheadProbe --lfo traced: the triangle rises first, the saw falls, the square starts high,
		// the ramp and the exponential fall once, random holds a level a period
		switch(_shape)
		{
		case md::LfoSettings::Triangle:
			return _phase < 0.25f ? 0.5f + 2.0f * _phase : _phase < 0.75f ? 1.0f - 2.0f * (_phase - 0.25f) : 2.0f * (_phase - 0.75f);
		case md::LfoSettings::Saw:
			return 1.0f - _phase;
		case md::LfoSettings::Square:
			return _phase < 0.5f ? 1.0f : 0.0f;
		case md::LfoSettings::Ramp:
			return std::max(0.0f, 1.0f - _phase / 0.8f);
		case md::LfoSettings::Exponential:
			return std::exp(-5.0f * _phase);
		case md::LfoSettings::Random:
		{
			constexpr float levels[] = {0.7f, 0.15f, 0.95f, 0.4f, 0.8f, 0.05f, 0.55f, 0.3f};
			return levels[_period % 8];
		}
		default:
			return 0.5f;
		}
	}

	void LfoView::paintWave(juce::Image& _image, juce::Graphics& _g, const uint8_t _shape1, const uint8_t _shape2,
		const float _mix, const uint32_t _periods, const bool _icon) const
	{
		const auto w = static_cast<float>(_image.getWidth());
		const auto h = static_cast<float>(_image.getHeight());
		if(w < 4.0f || h < 4.0f)
			return;
		const auto margin = _icon ? 1.5f : h * 0.12f;
		if(!_icon)
		{
			_g.setColour(g_axisColour);
			_g.drawHorizontalLine(static_cast<int>(h * 0.5f), 0.0f, w);
		}
		// SHMIX blends shape 1 (0) into shape 2 (127)
		juce::Path path;
		constexpr int steps = 96;
		for(int i = 0; i <= steps * static_cast<int>(_periods); ++i)
		{
			// The last point ends the last period rather than starting another
			const auto period = std::min(static_cast<uint32_t>(i / steps), _periods - 1);
			const auto phase = static_cast<float>(i - static_cast<int>(period) * steps) / static_cast<float>(steps);
			const auto level = (1.0f - _mix) * shapeLevel(_shape1, phase, period) + _mix * shapeLevel(_shape2, phase, period);
			const auto x = w * static_cast<float>(i) / static_cast<float>(steps * _periods);
			const auto y = margin + (h - 2.0f * margin) * (1.0f - level);
			if(i == 0)
				path.startNewSubPath(x, y);
			else
				path.lineTo(x, y);
		}
		_g.setColour(_icon ? g_iconColour : g_waveColour);
		_g.strokePath(path, juce::PathStrokeType(_icon ? 1.5f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	}

	void LfoView::openDestinationMenu(const Rml::Event& _event)
	{
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto lfo = m_controller.getTrackLfo(part);
		juceRmlUi::Menu menu;
		for(uint8_t track = 0; track < md::automation::machinedrum::TrackCount; ++track)
		{
			const auto* machine = md::machines::find(md::MachineModel::Machinedrum, m_controller.getTrackMachine(track));
			juceRmlUi::Menu parameters;
			for(uint8_t parameter = 0; parameter < 24; ++parameter)
			{
				const bool chosen = lfo && lfo->track == track && lfo->parameter == parameter;
				parameters.addEntry(parameterName(m_controller, track, parameter), chosen, [this, part, track, parameter]
				{
					m_controller.setTrackLfo(part, 0, track);
					m_controller.setTrackLfo(part, 1, parameter);
					update();
				});
			}
			menu.addSubMenu("PISTE " + number(track + 1u) + (machine ? " · " + std::string(machine->name) : std::string()),
				std::move(parameters));
		}
		menu.runModal(_event);
	}

	void LfoView::openShapeMenu(const Rml::Event& _event, const uint8_t _field)
	{
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto lfo = m_controller.getTrackLfo(part);
		juceRmlUi::Menu menu;
		for(uint8_t shape = 0; shape < md::LfoSettings::ShapeCount; ++shape)
		{
			const bool chosen = lfo && (_field == 2 ? lfo->shape1 : lfo->shape2) == shape;
			menu.addEntry(shapeName(shape), chosen, [this, part, _field, shape]
			{
				m_controller.setTrackLfo(part, _field, shape);
				update();
			});
		}
		menu.runModal(_event);
	}

	bool LfoView::update()
	{
		// Hidden: drawn in full when shown
		if(m_root && !m_root->IsVisible(true))
		{
			m_shownPart = 0xff;
			return false;
		}
		const auto part = static_cast<uint8_t>(m_controller.getCurrentPart());
		const auto lfo = m_controller.getTrackLfo(part);
		const auto machines = m_controller.getMachineRevision();
		const auto* mixParameter = m_controller.getParameter("LFOShape", part);
		const int mix = mixParameter ? static_cast<int>(mixParameter->getUnnormalizedValue()) : 0;
		if(part == m_shownPart && lfo == m_shown && machines == m_shownMachines && mix == m_shownMix)
			return false;
		const bool shapesChanged = part != m_shownPart || !lfo != !m_shown
			|| (lfo && (lfo->shape1 != m_shown->shape1 || lfo->shape2 != m_shown->shape2));
		m_shownPart = part;
		m_shown = lfo;
		m_shownMachines = machines;
		m_shownMix = mix;

		if(lfo)
		{
			const auto name = parameterName(m_controller, lfo->track, lfo->parameter);
			setText(m_destination, "PISTE " + number(lfo->track + 1u) + " · " + name);
			setText(m_info, "Le LFO de la piste " + number(part + 1u) + " module " + name + " de la piste "
				+ number(lfo->track + 1u) + ". SHMIX à 0 joue la forme 1, à 127 la forme 2.");
		}
		else
		{
			setText(m_destination, "—");
			setText(m_info, "LFO inconnu : en attente du kit");
		}
		for(uint8_t mode = 0; mode < m_updates.size(); ++mode)
		{
			if(m_updates[mode])
				m_updates[mode]->SetClass("mdEdSelected", lfo && lfo->update == mode);
		}
		for(uint8_t n = 0; n < 2; ++n)
		{
			setText(m_shapeNames[n], lfo ? shapeName(n ? lfo->shape2 : lfo->shape1) : "—");
			if(shapesChanged && m_icons[n])
				m_icons[n]->repaint();
		}
		if(m_wave)
			m_wave->repaint();
		return true;
	}
}
