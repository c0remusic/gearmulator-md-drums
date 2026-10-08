// MD Drums' processor as a host drives it, at 44.1 and 48 kHz: 576 host parameters in the order and with the IDs the
// port froze, a note 36-51 sounding its Track and other notes nothing, host automation of a Track's machine reaching
// the Device, SYN1 named after the machine, the Out parameter taking a Track off Main whatever the host does with its
// bus, and the state bringing back the Device's Kit and mixer and the parameters with them; a Track played from the
// editor sounds the machine chosen just before. It reports the note-to-sound latency through the whole plug-in.

#include "mdDrumsProcessor.h"

#include "mdDrumsController.h"
#include "mdDrumsDevice.h"
#include "mdDrumsEditor.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlLookAndFeel.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "synthLib/plugin.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace juceRmlUi
{
	// The friend hook the MD editor tests use: one RmlUi frame, so that paint() has geometry
	struct RenderingTestAccess
	{
		static void update(RmlComponent& _component) { _component.update(); }
	};
}

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	constexpr int g_block = 512;

	struct Result
	{
		int first = -1;		// host samples from the note to the first nonzero sample of Main
		float peak = 0;		// Main
		double energy = 0;
		float outPeak = 0;	// Out 01, when the host enables it (channels 2 on)
	};

	void process(juce::AudioProcessor& _processor, juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		_processor.processBlock(_buffer, _midi);
		_midi.clear();
	}

	// A second of silence, then _note at sample _offset of a block, and a second after it
	Result play(juce::AudioProcessor& _processor, const int _note, const int _offset, const double _rate)
	{
		juce::AudioBuffer<float> buffer(_processor.getTotalNumOutputChannels(), g_block);
		juce::MidiBuffer midi;
		const auto blocks = static_cast<int>(_rate / g_block);
		for(int i = 0; i < blocks; ++i)
			process(_processor, buffer, midi);
		Result result;
		int position = 0;
		for(int i = 0; i < blocks; ++i)
		{
			if(i == 0)
				midi.addEvent(juce::MidiMessage::noteOn(1, _note, static_cast<juce::uint8>(100)), _offset);
			process(_processor, buffer, midi);
			for(int s = 0; s < g_block; ++s)
			{
				const auto left = buffer.getSample(0, s), right = buffer.getSample(1, s);
				const auto value = std::max(std::abs(left), std::abs(right));
				if(result.first < 0 && value != 0.0f)
					result.first = position + s - _offset;
				result.peak = std::max(result.peak, value);
				result.energy += double(value) * value;
				if(buffer.getNumChannels() > 2)
					result.outPeak = std::max(result.outPeak, std::abs(buffer.getSample(2, s)));
			}
			position += g_block;
		}
		return result;
	}

	pluginLib::Parameter& parameter(mdDrums::Processor& _processor, const std::string& _name, const uint8_t _part)
	{
		auto* p = _processor.getController().getParameter(_name, _part);
		require(p != nullptr, "no parameter " + _name);
		return *p;
	}

	// As a host automates: the value set on the parameter, then a block for the Controller to send it
	void automate(mdDrums::Processor& _processor, const std::string& _name, const uint8_t _part, const int _value)
	{
		auto& p = parameter(_processor, _name, _part);
		p.setValue(p.convertTo0to1(static_cast<float>(_value)));
		juce::AudioBuffer<float> buffer(_processor.getTotalNumOutputChannels(), g_block);
		juce::MidiBuffer midi;
		process(_processor, buffer, midi);
		_processor.getController().processPendingMidiMessages();
		// What the editor binds to follows host automation once the Controller's timer has run
		require(static_cast<int>(p.getValueObject().getValue()) == _value, "the editor did not see " + _name + " change");
	}

	const mdDrums::Device& device(mdDrums::Processor& _processor)
	{
		const auto* d = dynamic_cast<const mdDrums::Device*>(_processor.getPlugin().getDevice());
		require(d != nullptr, "the processor has no MD Drums Device");
		return *d;
	}

	// The v22 skin opens, its document 1296 x 824 (mdDrumsSkinTest checks the skin view by view and every parameter's
	// control); MD_DRUMS_EDITOR_PNG names a file for a picture of it
	void editor(mdDrums::Processor& _processor)
	{
		_processor.setForceSoftwareRendererForSession(true);
		auto& state = _processor.getOrCreateEditorState();
		require(state.getCurrentSkin().filename == "mdDrums.rml", "the default skin is " + state.getCurrentSkin().filename);
		auto* editor = dynamic_cast<mdDrums::Editor*>(state.getEditor());
		require(editor != nullptr, "the processor did not create the editor");
		auto* component = editor->getRmlComponent();
		require(component && component->getContext() && component->getDocument(), "the editor has no RmlUi document");
		juceRmlUi::RmlInterfaces::ScopedAccess access(*component);
		component->getContext()->Update();
		const auto size = component->getDocumentSize();
		require(size.x == 1296 && size.y == 824, "the document is " + std::to_string(size.x) + "x" + std::to_string(size.y));
		std::printf("editor: %dx%d, skin %s\n", static_cast<int>(size.x), static_cast<int>(size.y),
			state.getCurrentSkin().filename.c_str());

		if(const auto* png = std::getenv("MD_DRUMS_EDITOR_PNG"))
		{
			for(const auto& [index, parameters] : _processor.getController().getExposedParameters())
				for(auto* p : parameters)
				{
					p->flushRealtimeValueToUi();
					p->getValueObject().getValueSource().sendChangeMessage(true);
				}
			juceRmlUi::LookAndFeel lookAndFeel;
			component->setLookAndFeel(&lookAndFeel);
			juce::Image image;
			for(int frame = 0; frame < 6; ++frame)
			{
				juceRmlUi::RenderingTestAccess::update(*component);
				image = juce::Image(juce::Image::ARGB, component->getWidth(), component->getHeight(), true);
				lookAndFeel.getCurrentImage() = image;
				juce::Graphics g(image);
				component->paint(g);
			}
			component->setLookAndFeel(nullptr);
			juce::FileOutputStream out{juce::File(juce::String(png))};
			out.setPosition(0);
			out.truncate();
			juce::PNGImageFormat().writeImageToStream(image, out);
		}
		_processor.destroyEditorState();
	}

	void latency(mdDrums::Processor& _processor, const double _rate)
	{
		automate(_processor, "Machine", 0, 16);
		automate(_processor, "Level", 0, 100);
		uint32_t random = 0x16305eedu;
		int minimum = 1 << 30, maximum = 0;
		double sum = 0;
		for(int note = 0; note < 48; ++note)
		{
			random ^= random << 13; random ^= random >> 17; random ^= random << 5;
			const auto hit = play(_processor, 36, static_cast<int>(random % g_block), _rate);
			require(hit.first >= 0, "a note stayed silent");
			minimum = std::min(minimum, hit.first);
			maximum = std::max(maximum, hit.first);
			sum += hit.first;
		}
		std::printf("%.0f Hz: note to sound (TRXBD, 48 notes): %d-%d samples, mean %.1f (%.2f ms), jitter %d (%.2f ms),"
			" reported %d\n", _rate, minimum, maximum, sum / 48, sum / 48 * 1000.0 / _rate, maximum - minimum,
			(maximum - minimum) * 1000.0 / _rate, _processor.getLatencySamples());
	}
}

int main()
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI juce;
	try
	{
		for(const double rate : {44100.0, 48000.0})
		{
			mdDrums::Processor processor(true);
			if(!dynamic_cast<const mdDrums::Device*>(processor.getPlugin().getDevice()))
			{
				std::cout << "mdDrumsProcessorTest: SKIP (no Machinedrum UW OS 1.63 flash image found)\n";
				return 77;
			}
			juce::AudioProcessor& host = processor;
			host.setRateAndBufferSizeDetails(rate, g_block);
			host.prepareToPlay(rate, g_block);

			// The parameters, frozen from the port's first push: per Track Machine, 24, 5 LFO, Level, Mute, Solo, Out
			const auto& parameters = host.getParameters();
			std::printf("%.0f Hz: %d parameters, first %s \"%s\", last %s \"%s\"\n", rate, parameters.size(),
				dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[0])->paramID.toRawUTF8(),
				parameters[0]->getName(64).toRawUTF8(),
				dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters.getLast())->paramID.toRawUTF8(),
				parameters.getLast()->getName(64).toRawUTF8());
			require(parameters.size() == 576, "not 576 parameters");
			require(dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[0])->paramID == "5_0_0"
				&& parameters[0]->getName(64) == "Track 1 Machine", "track 1's machine is not the first parameter");
			require(dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[33])->paramID == "7_0_1"
				&& dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[34])->paramID == "5_1_0",
				"a Track does not hold 34 parameters ending with Out");
			require(dynamic_cast<juce::AudioProcessorParameterWithID*>(parameters[544])->paramID == "8_0_0"
				&& parameters[544]->getName(64) == "Master Echo TIME", "the master effects do not follow the Tracks");

			// The parameters start as the Device boots: factory Kit 1, TRX-B2 on track 1
			require(parameter(processor, "Machine", 0).getUnnormalizedValue() == 28
				&& parameter(processor, "Level", 0).getUnnormalizedValue() == device(processor).kit().levels[0],
				"the parameters do not start as the Device");

			const auto kick = play(processor, 36, 200, rate);
			const auto none = play(processor, 60, 200, rate);
			std::printf("%.0f Hz: note 36 at sample 200: first sound %d samples later, peak %.4f; note 60: peak %.4f\n",
				rate, kick.first, kick.peak, none.peak);
			require(kick.first >= 0 && kick.peak > 0.01f, "note 36 is silent");
			require(none.peak == 0.0f, "a note outside 36-51 sounded");

			// The host automates track 1's machine: TRX-B2 to EFM-BD (id 32)
			const auto before = play(processor, 36, 0, rate);
			automate(processor, "Machine", 0, 32);
			const auto after = play(processor, 36, 0, rate);
			const auto syn1 = parameter(processor, "SYN1", 0).getCurrentValueAsText();
			std::printf("%.0f Hz: track 1 TRX-B2 energy %.3f, EFM-BD energy %.3f, SYN1 reads \"%s\"\n", rate,
				before.energy, after.energy, syn1.toRawUTF8());
			require(device(processor).kit().machine(0) == 32, "the machine did not reach the Device");
			require(std::abs(after.energy - before.energy) > before.energy * 0.05, "the machine parameter did nothing");
			require(syn1.startsWith("PTCH "), "SYN1 does not show its name on EFM-BD");
			require(parameter(processor, "SYN1", 0).getUnnormalizedValue() == device(processor).kit().parameters[0][0],
				"SYN1 does not follow the machine's default");

			// The editor plays a Track (Listen while choosing): a machine chosen, the Track played at once, sounds the
			// machine chosen, its note reaching the Device after the machine in the same block
			{
				juce::AudioBuffer<float> buffer(host.getTotalNumOutputChannels(), g_block);
				juce::MidiBuffer midi;
				const auto blocks = static_cast<int>(rate / g_block);
				for(int i = 0; i < blocks; ++i)
					process(processor, buffer, midi);
				parameter(processor, "Machine", 0).setUnnormalizedValueNotifyingHost(28, pluginLib::Parameter::Origin::Ui);
				dynamic_cast<mdDrums::Controller&>(processor.getController()).audition(0, 100);
				double energy = 0;
				for(int i = 0; i < blocks; ++i)
				{
					process(processor, buffer, midi);
					for(int s = 0; s < g_block; ++s)
					{
						const auto value = std::max(std::abs(buffer.getSample(0, s)), std::abs(buffer.getSample(1, s)));
						energy += double(value) * value;
					}
				}
				// The same machine at its defaults, struck by the host
				const auto host36 = play(processor, 36, 0, rate);
				std::printf("%.0f Hz: TRX-B2 chosen and played from the editor: energy %.3f; struck by the host %.3f, EFM-BD %.3f\n",
					rate, energy, host36.energy, after.energy);
				require(device(processor).kit().machine(0) == 28, "the machine chosen did not reach the Device");
				require(std::abs(energy - host36.energy) < host36.energy * 0.02, "the editor's note did not sound the machine chosen");
				automate(processor, "Machine", 0, 32);
			}

			// Out: track 1 leaves Main whether or not the host takes its bus
			automate(processor, "Out", 0, 1);
			const auto offMain = play(processor, 36, 0, rate);
			auto layout = host.getBusesLayout();
			layout.outputBuses.getReference(1) = juce::AudioChannelSet::mono();
			require(host.setBusesLayout(layout), "the host cannot enable Out 01");
			host.prepareToPlay(rate, g_block);
			const auto onOut = play(processor, 36, 0, rate);
			std::printf("%.0f Hz: track 1 on its Out: Main %.4f without the bus, Main %.4f and Out 01 %.4f with it\n", rate,
				offMain.peak, onOut.peak, onOut.outPeak);
			require(offMain.peak == 0.0f && onOut.peak == 0.0f && onOut.outPeak > 0.01f,
				"the Out parameter did not move track 1 off Main");
			layout.outputBuses.getReference(1) = juce::AudioChannelSet::disabled();
			require(host.setBusesLayout(layout), "the host cannot disable Out 01");
			host.prepareToPlay(rate, g_block);

			// The state: the Device's Kit and mixer, and the parameters following them
			automate(processor, "LfoShape1", 3, 5);
			automate(processor, "EqGAIN", 0, 99);
			juce::MemoryBlock state;
			host.getStateInformation(state);
			mdDrums::Processor restored(true);
			juce::AudioProcessor& restoredHost = restored;
			restoredHost.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			require(device(restored).kit() == device(processor).kit() && device(restored).mixer() == device(processor).mixer(),
				"the state did not bring back the Device's Kit and mixer");
			require(parameter(restored, "Machine", 0).getUnnormalizedValue() == 32
				&& parameter(restored, "Out", 0).getUnnormalizedValue() == 1
				&& parameter(restored, "LfoShape1", 3).getUnnormalizedValue() == 5
				&& parameter(restored, "EqGAIN", 0).getUnnormalizedValue() == 99,
				"the restored parameters do not follow the state");
			std::printf("%.0f Hz: state of %zu bytes restored, parameters following\n", rate, state.getSize());

			automate(processor, "Out", 0, 0);
			if(rate == 44100.0)
				editor(processor);
			latency(processor, rate);
		}
		std::cout << "mdDrumsProcessorTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _error)
	{
		std::cerr << "mdDrumsProcessorTest: " << _error.what() << '\n';
		return 1;
	}
}
