# The TUS stack for an engine without firmware

Research for ticket 01 (`issues/01-pile-tus-moteur-sans-firmware.md`). Every path is relative to the repository root;
`file:line` points at the code as it stands on 2026-10-08 (`main` at 21fefc70). Nothing here was built or run.

## In short

- A `synthLib::Device` does not have to wrap a DSP56300. The stack links no emulator: `synthLib` links `resample`,
  `dsp56kBase` and `baseLib` (`source/synthLib/CMakeLists.txt:56`), and `dsp56kBase` holds only ring buffers, logging,
  thread and memory helpers (`source/dsp56300/source/dsp56kBase/CMakeLists.txt:6-26`). Three Devices in the tree run no
  DSP56300: `pluginLib::DummyDevice` (`source/jucePluginLib/dummydevice.h:7-29`), the audio probe's `ProbeDevice`
  (`source/elektron/md/mdJucePlugin/mdAudioProbePlugin.cpp:10-42`) and the JE-8086's `jeLib::Device`, an H8S
  (`source/ronaldo/je8086/jeLib/je8086.h:3`). Nothing in the stack links `mc68k` or Musashi either (no match for
  `dsp56kEmu|mc68k|Musashi` in the CMakeLists of `synthLib`, `jucePluginLib`, `jucePluginEditorLib`, `juceRmlPlugin`,
  `juceRmlUi`, `juceUiLib`, `bridge`, `mcpServerLib`, `baseLib`).
- **The stack carries 12 output channels at most.** `TAudioOutputs` is `std::array<T*, 12>`
  (`source/synthLib/audioTypes.h:9`). MD Drums renders 18 (`Engine::OutputCount = 2 + TrackCount`,
  `source/elektron/md/mdDrums/mdDrumsEngine.h:58`). This is the one hard misfit; see "Outputs" below.
- The stack adds no latency of its own at 44.1 kHz with zero latency blocks, but **its default is one block**, reported
  to the host whether or not the Device delays its audio by it. At other host rates it adds the resampler's delay. It
  hands the Device every MIDI event of a block before the block's audio, so sample accuracy is the Device's job.
- Sixteen Tracks fit exactly: sixteen is the Controller's default part count and the hard ceiling of both the
  Controller and the skin binding.
- The editor's scale and per-user settings exist: a `scale` value in a per-user properties file, restored when the
  window opens, settable from the context menu, the Settings page or any control that fires `evSetGuiScale`.

## `pluginLib::Processor`

`pluginLib::Processor` is a `juce::AudioProcessor` (`source/jucePluginLib/processor.h:44`). A product supplies:

- `createDevice()` and `createController()`, both pure virtual (`processor.h:92`, `processor.h:240`).
- `Properties`: name, vendor, synth/MIDI flags, the 4CC, the LV2 URI, the BinaryData table, an optional data folder
  name, and optional logical bus offsets for products whose buses are enabled independently (`processor.h:55-72`).
  `pluginLib::initProcessorProperties()` fills them from JUCE's `JucePlugin_*` macros and the `Plugin4CC` define
  (`source/jucePluginLib/processorPropertiesInit.h:7-26`).

What it does for the product:

- **ROM:** the constructor adds the public ROM folder and the module's folders to `synthLib::RomLoader`'s search paths
  (`processor.cpp:52-56`). It loads nothing itself: the product's `createDevice()` finds the ROM, and the JE does it
  with its own loader (`source/ronaldo/je8086/jeJucePlugin/jePluginProcessor.cpp:24,58-77`). A `DeviceException` with
  `FirmwareMissing` makes `getPlugin()` show a dialog naming `<data folder>/roms/` and fall back to a `DummyDevice`
  (`processor.cpp:192-239`). A Device that turns invalid later is replaced on the message thread
  (`processor.cpp:1121-1167`), triggered from `Plugin::process` (`source/synthLib/plugin.cpp:132-139`).
- **Folders:** the data folder is `Documents/<vendor>/<product>/` (`processor.cpp:554-559`,
  `source/jucePluginLib/tools.cpp:26-39`, overridable by `GEARMULATOR_DATA_ROOT`); the config file is
  `<data folder>/config/<product>.xml` (`processor.cpp:566-579`).
- **Buses:** `processBlock` maps each JUCE bus to logical device channels, by `logicalOutputBusOffsets` when given,
  and drops any logical channel at or past 12 (`processor.cpp:810-826`). The base `isBusesLayoutSupported` accepts only
  a stereo main output **and a stereo main input** (`processor.cpp:695-709`); a product overrides it, as the probe does
  (`mdAudioProbePlugin.cpp:78-89`) and Gearmulator MD does (`mdPluginProcessor.cpp:556-578`).
- **Latency:** `updateLatencySamples()` reports `Plugin::getLatencyMidiToOutput()` for a synth without inputs
  (`processor.cpp:290-299`), on `prepareToPlay` (`processor.cpp:686`) and on every latency change.
- **State:** `getStateInformation` writes the magic string `"DSP56300"` (only a tag, `processor.cpp:36`), a version,
  then chunks: `MIDI` holds the Device's own state from `Plugin::getState(StateTypeGlobal)`, then `GAIN`, `DSPC`,
  `DSSR`, `RSMP`, MIDI ports, the routing matrix, MIDI Learn and the program name (`processor.cpp:345-399`,
  `processor.cpp:712-730`). **No parameter value is saved.** After a restore the Controller's `onStateLoaded()` runs
  (`processor.cpp:1064-1065`), and the parameters are rebuilt from what the Device answers: the JE asks its Device for
  dumps there (`source/ronaldo/je8086/jeJucePlugin/jeController.cpp:78-82`). The Device owns the truth; the parameters
  mirror it.
- **processBlock** (`processor.cpp:777-938`) is protected so that a product can wrap it (`processor.h:206-207`);
  Gearmulator MD meters its outputs after it (`mdPluginProcessor.cpp:460-473`). Bypassed, a synth outputs silence
  (`processor.cpp:972-976`).

## `jucePluginEditorLib::Processor`

It derives from `pluginLib::Processor` (`source/jucePluginEditorLib/pluginProcessor.h:13`) and adds:

- a `juce::PropertiesFile` config, persistent or ephemeral, given its `Options` at construction
  (`pluginProcessor.h:16-23`, `pluginProcessor.cpp:74-81`); the persistent file is the processor's config file above
  (`pluginProcessor.cpp:225-241`);
- `createEditorState()`, pure virtual (`pluginProcessor.h:30`), and an `EditorWindow` over it
  (`pluginProcessor.cpp:115-149`);
- `latencyBlocks` saved to the config whenever it changes (`pluginProcessor.cpp:99-108`); the JE reads it back in its
  constructor (`jePluginProcessor.cpp:43-45`), Gearmulator MD too, with its own default of 2
  (`mdPluginProcessor.cpp:414-417`);
- the editor's per-instance state in an `EDST` chunk, then the Controller's chunk (parameter links)
  (`pluginProcessor.cpp:167-184`);
- an MCP server, started only when the config holds `enableMcpServer` (`pluginProcessor.cpp:88-90`).

The editor side needs a `PluginEditorState` subclass that loads a skin and creates the `Editor`
(`source/ronaldo/je8086/jeJucePlugin/jePluginEditorState.cpp:10-18`; `pluginEditorState.h:101`). `Editor` asks for
`getDemoRestrictionText()` (pure virtual, `pluginEditor.h:117`); the Patch Manager is optional:
`createPatchManager()` returns `nullptr` by default (`pluginEditor.h:106`).

## `synthLib::Device`: the contract

Pure virtual (`source/synthLib/device.h`): `getSamplerate()` (69), `isValid()` (85), `getState`/`setState` (88-89),
`getChannelCountIn`/`getChannelCountOut` (100-101), `setDspClockPercent`/`getDspClockPercent`/`getDspClockHz`
(103-105), and the protected `readMidiOut`, `processAudio`, `sendMidi` (136-138). Construction takes
`DeviceCreateParams`: preferred and host rate, ROM name, bytes and hash, a custom word, a home path (`device.h:20-29`).

Virtual with defaults: `process()` (56), `getDefaultLatencyBlocks()` = 1 (60), `getInternalLatencyMidiToOutput()` and
`...InputToOutput()` = 0 (62-63), the supported/preferred rates = `{getSamplerate()}` (65-73), `setSamplerate()`
(77), `canModifyDspClock()` = false (106), `setStateFromUnknownCustomData()` (90), state transactions, off by default
(94-97), `finishRendering`/`pauseRendering`/`resumeRendering`, no-ops for a Device that renders in `process()`
(110-119), `extraLatencyChanged()` (135).

- **`process()`** (`device.cpp:35-52`) runs each MIDI event through the `MidiTranslator`, calls `sendMidi()` for all of
  them, then `processAudio()` once for the whole block, then `readMidiOut()`. Events keep their sample offset
  (`SMidiEvent::offset`, `source/synthLib/midiTypes.h:303`), but nothing in the base class splits the block at it: a
  Device that ignores offsets plays every note at the block's start. Gearmulator MD overrides `process()`
  (`mddevice.h:100-102`); the JE queues events by offset on its own thread (`jeThread.cpp:34-38`).
- **Threads:** none required. A Device may render on a thread of its own and must then implement the three rendering
  hooks (`device.h:110-119`); the JE runs one (`jeThread.cpp:9-12,87-104`), Gearmulator MD runs `md::AsyncRender`
  when latency is non-zero (`mddevice.h:97-102`).
- **Rate:** `Device::getDeviceSamplerate` keeps the preferred rate if supported, else the only preferred one, else the
  lowest at or above the host's (`device.cpp:90-117`). A Device with one rate runs at that rate whatever the host does.
- **Latency:** `setExtraLatencySamples()` only stores the value, capped at 16384 (`device.cpp:54-71`), and calls
  `extraLatencyChanged()`. Delaying the audio by it is the Device's job: the JE renders that many samples ahead
  (`jeThread.cpp:46-60`, fed `getExtraLatencySamples()` at `jeLib/device.cpp:161`).
- **MIDI from the Device** (`readMidiOut`) goes to the Controller and the physical ports (`processor.cpp:907-919`);
  SysEx in it counts as a realtime allocation (`plugin.cpp:170-172`).

## `synthLib::Plugin`: what sits between the Processor and the Device

- **One lock for audio and control.** `Plugin::process` holds `m_lock` for the whole render, Device included
  (`plugin.cpp:113-124,155-163`). Every control access takes the same lock and pauses the Device:
  `withDeviceLocked` (`plugin.h:81-87`), `getState`/`setState` (`plugin.cpp:245-314`), the rate and block-size setters
  (`plugin.cpp:49-100,448-458`). A state save during playback holds the audio thread for the time `getState` takes.
- **Inputs and outputs:** missing inputs become a silent buffer (`plugin.cpp:128-130`); for every channel up to
  `getChannelCountOut()` a missing output becomes a discard buffer (`plugin.cpp:141-143`). The Device therefore never
  sees which host buses are enabled. The discard buffers are an array of 12 (`plugin.h:144-145`): a Device declaring
  more than 12 outputs would make that loop index past both arrays.
- **Resampling:** `ResamplerInOut` calls the Device directly when host and device rates are equal
  (`source/synthLib/resamplerInOut.cpp:174-178`). Otherwise it rescales event offsets to device samples
  (`resamplerInOut.cpp:180-182,227-237`) and resamples every output channel, in one of three modes, `Legacy`
  (libresample, the default), `MameHq`, `MameLofi` (`resampler.h:20-26`, `processor.h:266`); the mode is saved in the
  plug-in state (`processor.cpp:382-386`). The output delay it adds is measured at creation and reported
  (`resamplerInOut.cpp:138-157`). Its prewarm uses 12 channels and `Resampler::process` asserts at most 12
  (`resamplerInOut.cpp:118`, `resampler.cpp:53`).
- **Latency:** `m_extraLatencyBlocks` starts at the Device's `getDefaultLatencyBlocks()` (`plugin.cpp:31`, default 1 at
  `device.h:60`). `updateDeviceLatency()` hands the Device `blockSize * blocks` samples
  (`plugin.cpp:381-397`), and the reported latency is that plus the Device's internal MIDI-to-output latency plus the
  resampler's (`plugin.cpp:460-465`). The Settings page offers 0, 1, 2, 4 and 8 blocks when the skin has the buttons
  (`source/jucePluginEditorLib/settingsDspAudio.cpp:18,61-83`).
- **MIDI clock:** while the host plays, `MidiClock` inserts `F8` clocks at their sample offsets, and `FA`, `F2`+`FB`
  or `FC` on transport edges, into the Device's input (`midiClock.cpp:17-21,41-50,59-80`). `Processor::processBpm` is a
  hook for the tempo (`processor.h:169`).

## The parameter JSON and the Controller

The Controller loads the JSON from the module's folder or from BinaryData (`controller.cpp:748-764`). Keys
(`source/jucePluginLib/parameterdescriptions.cpp`):

- `parameterdescriptiondefaults` (optional, 150-153): defaults for any property.
- `valuelists` (**required**, "value lists are not defined" otherwise, 164-169): named lists, as an array or a
  value-to-text map (500-573).
- `parameterdescriptions` (required, 154-159): each needs `name`, `min`, `max`, `isPublic`, `isDiscrete`, `isBool`,
  `isBipolar`, `toText`, `page`, `index` and `class`, from itself or the defaults (207-216, 262-343, 379-411);
  optional `displayName`, `default`, `step`, `version`, soft-knob fields. `toText` names a value list or synthesises
  one from `{"format", "scale", "offset"}` (278-326). An `index` of 128 or more rolls over into the next page
  (354-359). `class` takes `Global`, `MultiOrSingle`, `NonPartSensitive` (400-410).
- `midipackets` (optional, 479-480): SysEx layouts of `byte`, `param` (with `mask`, `shift`, `shiftL`, `part` 0-15),
  `checksum`, `bank`, `program`, `deviceid`, `page`, `part`, `paramindex`, `paramvalue`, `null` (591-770).
- `regions` (optional, 482-483): named sets of parameters or regions (781-883), used by the context menu's "Lock
  Regions" (`pluginEditorState.cpp:362-404`).
- `controllerMap` (optional, 485-486): `cc`, `pp` or `nrpn` to a parameter (894-963), for incoming MIDI only
  (`controller.cpp:567-593`).

**There is no `parts` key.** The part count is a Controller virtual, `getPartCount()`, 16 by default
(`controller.h:69`). `registerParams` instantiates every description once per part, in a group per part, except
`NonPartSensitive` ones, registered on part 0 and shared by the other parts (`controller.cpp:66-176`, 100-107); later
`version`s go into groups of their own so that older automation keeps its place (`controller.cpp:62-64,127-145`).

**IDs:** `page_part_index`, with `_uid` appended when several parameters share an address, and the JUCE version set
from `version` (`parameter.cpp:373-385`). The host shows `partFormatter(part) + " " + displayName`
(`parameter.cpp:35`); Gearmulator MD's formatter gives "Track n" or "Global" (`mdController.cpp:62-66`). A shared
(NonPartSensitive) parameter has part 0 in its ID.

**What `part` gives for 16 Tracks:** each Track is a part; 16 is also the ceiling: the Controller's
`m_paramsByParamType` is a `std::array` of 16 (`controller.h:225`), the skin binding creates data models `part0` to
`part15` plus `partCurrent` (`source/juceRmlPlugin/rmlParameterBinding.cpp:28,44-49,71-77`), and a `midipackets`
part is 0-15 (`parameterdescriptions.cpp:692-700`). In the skin, an element with `param="name"` binds to the part of
its nearest ancestor's `data-model` (`rmlParameterBinding.cpp:84-102`, `rmlPluginContext.cpp:44-56`): `partCurrent`
follows `Controller::setCurrentPart` (`rmlParameterBinding.cpp:268-286`), so a Track tab binds `partCurrent` and a
Mix strip binds `partN`. Master effects and other machine-wide values are `NonPartSensitive`. Gearmulator MD's JSON
has 26 descriptions over pages 0-4, no packets, no regions (`source/elektron/md/mdJucePlugin/parameterDescriptions_md.json:1-48`).

**How a change becomes MIDI.** The JSON does not map a parameter to a message. A change reaches the Controller's
`sendParameterChange()` (pure virtual, `controller.h:38`; called from `Parameter::sendParameterChangeNow`,
`parameter.cpp:102-109`), and the product builds the message itself: the JE builds SysEx in C++ with an empty
`midipackets` (`jeController.cpp:84-124`, `parameterDescriptions_je.json:800-802`); `createMidiDataFromPacket` and
`combineParameterChange` help those who describe packets (`controller.h:54-56,165`). The message then takes one of two
roads to the Device:

1. `Controller::sendMidiEvent` → `Processor::addMidiEvent` → `Plugin::addMidiEvent`, a ring buffer behind a mutex,
   from any thread (`controller.cpp:213-216`, `processor.cpp:70-97`, `plugin.cpp:37-47`), read at the next
   `Plugin::process` (`plugin.cpp:146,399-407`).
2. On the audio thread, `Controller::processRealtimeParameterChanges`, called by `processBlock` before the render
   (`controller.h:79`, `processor.cpp:871-872`), → `Processor::tryAddRealtimeMidiEvent` → `Plugin::insertMidiEvent`,
   with no lock (`processor.cpp:99-115`, `plugin.cpp:316-334`). Gearmulator MD uses this one
   (`mdController.cpp:1450-1468,1623`).

Host automation arrives in `Parameter::setValue`. By default it writes the `juce::Value` on the host's thread, which may
allocate and notify (`parameter.cpp:300,303-315`); a Parameter whose `shouldSendRepeatedHostValues()` is true takes a
lock-free path instead (`parameter.cpp:285-299`, `parameter.h:120`), which Gearmulator MD's `AutomationParameter`
opts into (`mdController.cpp:35-42,113-120`).

**Display text** comes from the description's static value list (`parameter.cpp:402-406`). A text that depends on
another value, like today's "PTCH 64" for SYN1 (`mdDrumsProcessor.cpp:102-109`), needs a `Parameter` subclass,
created through `Controller::createParameter` (`controller.h:156`), that overrides `getText`.

## Host MIDI to the Device, and what the stack adds to note-to-sound

The path within one `processBlock`: JUCE's `MidiBuffer` becomes `SMidiEvent`s with their sample position as offset
(`processor.cpp:846-867`); `addMidiEvent` lets MIDI Learn consume a learned message (`processor.cpp:73-79`), lets the
Program Change router take program changes (`processor.cpp:81-88`), gives the Controller a copy for the editor
(`processor.cpp:91-92`, try-lock, dropped on contention: `controller.cpp:641-669`), and queues it for the Device
(`processor.cpp:93-94`) under `Plugin`'s small mutex (`plugin.cpp:37-47`). The same call then runs `Plugin::process`
(`processor.cpp:903`): the queue is drained (`plugin.cpp:399-407`), clocks are added, the resampler passes events on,
and `Device::process` translates channels (identity by default, `midiTranslator.cpp:82-86`) and calls `sendMidi()`.
Host → Device routing is on by default (`midiRoutingMatrix.cpp:51`).

What it adds:

- **No block of delay**: a note reaches the Device in the block it arrived in.
- **Sample position**: kept as `offset`, applied only if the Device splits its block (`device.cpp:39-49`). MD Drums does
  that today (`mdDrumsProcessor.cpp:265-280`); the engine itself starts a trigger at its next 32-sample block
  (`mdDrumsEngine.h:17`).
- **Extra latency**: `blockSize * latencyBlocks` reported, with 1 block unless the Device returns 0 from
  `getDefaultLatencyBlocks()` (`plugin.cpp:31`, `device.h:60`); a Device that does not delay by that amount makes the
  host compensate for latency that is not there.
- **Resampler delay** when the host is not at 44.1 kHz, measured and reported (`resamplerInOut.cpp:138-157`). Today's
  MD Drums uses `juce::LagrangeInterpolator` (`mdDrumsProcessor.h:85-89`, `mdDrumsProcessor.cpp:195-230`) and reports
  no latency at all (it never calls `setLatencySamples`).
- **Jitter**: none added at equal rates; at other rates offsets are rounded up to device samples
  (`resamplerInOut.cpp:181`).

## The closest model

- **JE-8086** (`source/ronaldo/je8086/`) is the only shipped TUS plug-in whose Device runs no DSP56300. Device:
  `jeLib/device.h`, `jeLib/device.cpp` (one rate, 88.2 kHz, 2 channels, fixed clock, 4.5 ms internal latency:
  `device.cpp:55-58,119-147`); render thread and latency: `jeLib/jeThread.cpp`; state as SysEx dumps:
  `device.cpp:65-117`; values without a hardware message (master volume) through a private SysEx protocol,
  `jeLib::SysexRemoteControl` over `synthLib::SysexRemoteControl` (`jeLib/sysexRemoteControl.h:25-59`,
  `synthLib/sysexRemoteControl.h:9-17`). Plug-in: `jeJucePlugin/jePluginProcessor.*` (114 lines),
  `jeController.*` (2 parts: `jeController.h:27-30`), `jePluginEditorState.*`, `jeEditor.*`,
  `parameterDescriptions_je.json`, `CMakeLists.txt` (`addSkin`, `createJucePlugin`: lines 22-28),
  `serverPlugin.cpp` (the bridge entry, `createBridgeDevice`, declared in `source/bridge/client/plugin.h:28-35`).
  Unlike MD Drums, it still boots a ROM on an emulated CPU.
- **Thinnest complete stack:** `mdAudioProbePlugin.cpp`, 96 lines: a `pluginLib::Processor` with a 6-output Device, an
  empty Controller, three output buses with logical offsets `{0, 2, 4}` and its own bus check
  (`mdAudioProbePlugin.cpp:10-90`). No editor.
- **Thinnest Device:** `pluginLib::DummyDevice` (`dummydevice.h:7-29`, `dummydevice.cpp:9-11`).
- **MD-specific reference:** Gearmulator MD's `mdController.cpp` (realtime automation, "Track n" parts) and
  `mdPluginProcessor.cpp` (bus check, metering wrap). `md::AsyncRender` is fixed at 6 output channels
  (`mdLib/mdasyncrender.h:46-47`) and lives in `mdLib`, which MD Drums cannot link (ADR 0001).

## Editor scale and per-user settings

- **Scale:** the window stores the percentage as `scale` in the per-user config whenever it is set or the user drags the
  window (`pluginEditorWindow.cpp:60-67,122-141`) and reapplies it when the skin attaches
  (`pluginEditorWindow.cpp:161-165`). The size is `skin width * percent / 100 * rootScale`, where `rootScale` is an
  attribute of the skin's document (`pluginEditorWindow.cpp:127-135`, `pluginEditor.cpp:121`); the window may be dragged
  from a tenth to four times the skin's size, at the skin's aspect ratio unless its height is free
  (`pluginEditorWindow.cpp:154-159`). Ways to set
  it: the context menu, 50-300 % (`pluginEditorState.cpp:346-358`); Settings buttons `btScaleNN` the skin provides,
  50-400 % (`settingsGui.cpp:13,30-58`); any control that fires the public `evSetGuiScale`
  (`pluginEditorState.h:70`; `setGuiScale` itself is private, `pluginEditorState.h:106`). Nothing restricts it to
  75-150 %: the Size control's steps are ours to choose.
- **Per user:** the `juce::PropertiesFile` config (`pluginProcessor.h:22-23`) at `<data folder>/config/<product>.xml`
  holds `scale`, `latencyBlocks`, the skin, and flags such as `enableMcpServer` (`pluginEditorState.cpp:452-492`,
  `pluginProcessor.cpp:88,104`). **Per instance:** the editor's `getPerInstanceConfig` bytes, saved in the plug-in state
  (`pluginEditorState.cpp:115-133`, `pluginProcessor.cpp:167-184`).

## How `mdDrums::Engine` fits

| Engine (today) | Stack | Fit |
|---|---|---|
| No ROM to boot; an 8 MB flash image found by `Engine::findFlashImage` (`mdDrumsEngine.cpp:68-88`); the constructor throws without a usable OS (`mdDrumsEngine.h:40-41`) | `createDevice()` loads what it wants; `DeviceException(FirmwareMissing)` gives the dialog and a DummyDevice (`processor.cpp:192-239`); `romData` carries bytes to a remote Device (`device.h:24-25`) | Fits. The search order and the dialog's folder (`Documents/<vendor>/<product>/roms/`) differ; `RomLoader` is not needed. |
| 44.1 kHz only (`mdDrumsEngine.h:21`) | One supported rate; `Plugin` resamples (`device.cpp:103-104`, `resamplerInOut.cpp:174-178`) | Fits; Lagrange is replaced by Legacy/MameHq/MameLofi. |
| Renders any count, in 32-sample blocks inside (`mdDrumsEngine.cpp:154-187`), on the caller's thread | `processAudio(…, _samples)` with the host's block | Fits, synchronous: no rendering hooks needed. |
| A trigger at a sample position (`mdDrumsProcessor.cpp:265-280`) | All events before the block (`device.cpp:39-49`) | The Device must split the block at offsets (override `process()` or queue in `sendMidi()`). |
| 2 + 16 outputs (`mdDrumsEngine.h:58`) | 12 at most (`audioTypes.h:9`, `processor.cpp:822`, `plugin.h:144-145`, `resampler.cpp:53`, bridge `audioBuffers.h:44`) | **Does not fit.** Main + Out 01-10 at most, unless `TAudioOutputsT` widens (every user of the array: synthLib, the resampler, the bridge, every Device's `dummyProcess`, `device.cpp:27-28`) or the product's `processBlock` wrapper writes the Outs itself. |
| Tracks leave Main when the host enables their bus (`mdDrumsProcessor.cpp:243-256`) | The Device sees no bus state (`plugin.cpp:141-143`) | The handoff's `t<n>_out` parameter (`HANDOFF.md:252,283`) is the stack's way. |
| No input bus (`mdDrumsProcessor.cpp:137`) | Base layout check wants stereo in (`processor.cpp:701-706`) | Override `isBusesLayoutSupported`; `getChannelCountIn()` 0 is handled (`resamplerInOut.cpp:218-257`). |
| No latency reported | 1 block by default (`device.h:60`) | Return 0 from `getDefaultLatencyBlocks()`, or delay by `getExtraLatencySamples()`. The engine's own 0.48 ms can go in `getInternalLatencyMidiToOutput()`. |
| APVTS state as XML (`mdDrumsProcessor.cpp:288-298`) | Device state in the `MIDI` chunk; parameters rebuilt by the Controller (`processor.cpp:356-362,1064-1065`) | The Device must hold the Kit and the mutes, solos, Outs, and answer the Controller's dump requests. |
| `render()` throws on an engine fault (`mdDrumsEngine.cpp:163-164`) | `isValid()` false → recovery (`plugin.cpp:132-139`) | Catch in `processAudio`, then report invalid. |
| Tempo from the playhead (`mdDrumsProcessor.cpp:259-263`) | `processBpm` hook (`processor.h:169`), MIDI clock into the Device (`midiClock.cpp:41-50`) | Fits; the Device must ignore or use `F8`/`FA`/`FC`. |
| 432 parameters `t<n>_<name>` (16 × machine, 24, level, mute: `mdDrumsProcessor.cpp:74-120`) | `page_part_index`, ≤ 16 parts, globals on part 0 | Fits; the IDs change (ADR 0001). |

## Product identity

`createJucePlugin` hard-codes company "Gearmulator Preview", manufacturer `GmPv` and a `local.gearmulator.preview.*`
bundle ID, and adds `serverPlugin.cpp` and `jucePluginEditorLib` (`source/juce.cmake:125-183`, lines 131, 141, 149,
161, 177-183). MD Drums is "c0remusic", `C0rm`, `Mddr`, `local.c0remusic.mddrums`, with its own `juce_add_plugin`
(`source/elektron/md/mdDrumsPlugin/CMakeLists.txt:5-20`). Keeping that identity means keeping the plug-in's own
`juce_add_plugin` and adding the defines `createJucePlugin` sets (`juce.cmake:167-175`), or giving the macro
parameters. The vendor also names the data folder (`processorPropertiesInit.h:12`, `tools.cpp:37-38`).

## What does not exist

- A `parts` key in the parameter JSON; a JSON mapping from a parameter to a MIDI message (the Controller does it in C++).
- A way to carry more than 12 output channels through `synthLib`.
- Any sample-accurate splitting in `synthLib::Device` itself.
- A Device in the tree that renders without booting firmware: the JE boots its ROM on an emulated H8S, and DummyDevice
  and the probe Device render nothing. MD Drums' Device would be the first of its kind.
- A scale limited to 75-150 % or a Size control in the stack; only the stored percentage and the event.
- A dynamic value list (machine names from the flash image, per-machine SYN names) in `ParameterDescriptions`.

## Against ADR 0001's assumptions

- "A `synthLib::Device` over `mdDrums::Engine`": nothing in the contract requires a DSP56300 or a ROM boot; confirmed.
- The ADR does not say the stack limits outputs; it does, to 12, and MD Drums needs 18. The ADR's list of what the stack
  brings should gain this cost.
- The ADR does not discuss latency. The stack itself adds none at 44.1 kHz, but its default reports one block, and it
  moves sample accuracy and the extra delay into the Device.
- "Settings and the editor's scale": confirmed, per user, in the config file.
- "`page_part_index` (`Parameter::genId`)": confirmed, with a `_uid` suffix when addresses repeat and the page bumped
  when an index passes 127.
