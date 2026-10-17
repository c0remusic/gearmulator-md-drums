#pragma once

#include "mdDrumsController.h"
#include "mdDrumsEditor.h"
#include "mdDrumsHitView.h"
#include "mdDrumsHostDriver.h"
#include "mdDrumsProcessor.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

// What the tests that link the plug-in share (mdDrumsProcessorTest, mdDrumsSkinTest, mdDrumsInstancesTest): a
// Processor prepared as a host prepares it, its editor opened, its parameters by name, a value set as the host and as
// the editor set it, the Hit preview waited for, and the time things take.
namespace mdDrums::testSupport
{
	using hostDriver::require;
	using Clock = std::chrono::steady_clock;

	// Milliseconds since _start
	inline double msSince(const Clock::time_point _start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - _start).count();
	}

	// _processor prepared as a host prepares it, at _rate in blocks of _block. False, and nothing prepared, without a
	// Device (no Machinedrum UW OS 1.63 flash image found), which a test skips
	inline bool prepare(Processor& _processor, const double _rate, const int _block)
	{
		if(!_processor.hasDevice())
			return false;
		auto& host = static_cast<juce::AudioProcessor&>(_processor);
		host.setRateAndBufferSizeDetails(_rate, _block);
		host.prepareToPlay(_rate, _block);
		return true;
	}

	// The editor as the host opens it, with the software renderer, and its RmlUi document
	inline Editor& openEditor(Processor& _processor)
	{
		_processor.setForceSoftwareRendererForSession(true);
		auto* editor = dynamic_cast<Editor*>(_processor.getOrCreateEditorState().getEditor());
		require(editor != nullptr, "the processor did not create the editor");
		auto* component = editor->getRmlComponent();
		require(component && component->getContext() && component->getDocument(), "the editor has no RmlUi document");
		return *editor;
	}

	// A parameter by its JSON name and part
	inline pluginLib::Parameter& parameter(Processor& _processor, const std::string& _name, const uint8_t _part = 0)
	{
		auto* p = _processor.getController().getParameter(_name, _part);
		require(p != nullptr, "no parameter " + _name);
		return *p;
	}

	// A value set as a host sets it, normalised
	inline void hostSets(pluginLib::Parameter& _p, const int _value)
	{
		_p.setValue(_p.convertTo0to1(static_cast<float>(_value)));
	}

	// A value set as the editor's controls set it
	inline void uiSets(pluginLib::Parameter& _p, const int _value)
	{
		_p.setUnnormalizedValueNotifyingHost(_value, pluginLib::Parameter::Origin::Ui);
	}

	// The Hit preview, rendered on its own thread, waited for as the Hit screen reads it at each frame: _tick is a
	// display tick (the editor's clock barely moving, so that a machine chosen keeps its own parameters meanwhile).
	// The milliseconds from _start until it shows; _unseen, if any, says why before the test fails at 10 s
	inline double untilPreview(const HitView& _view, const std::function<void()>& _tick, const Clock::time_point _start,
		const std::function<void()>& _unseen = {})
	{
		while(_view.previewPending() || !_view.previewing())
		{
			const bool late = Clock::now() - _start >= std::chrono::seconds(10);
			if(late && _unseen)
				_unseen();
			require(!late, "no preview within 10 s");
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			_tick();
		}
		return msSince(_start);
	}
}
