# Push 2 and a VST3's parameters in Live 12

Research for ticket 02 (`issues/02-push2-parametres-vst3.md`), written 2026-10-08 for the Push 2 bank design of
ticket 11. "Track" means one of MD Drums' sixteen voices (`CONTEXT.md`); Ableton's tracks are called "Live tracks".

The user's machine runs Live 12 Suite 12.2.1 (executable version and `%AppData%\Ableton\Live 12.2.1`), so three
local primary sources were read besides the documentation: the Push 2 display code that ships with Live (QML, plain
text), the bytecode of Live's MIDI Remote Scripts (compared with a public decompilation), and the user's own Live
12.2.1 sets, whose gzipped XML shows how Live stores a plug-in's configured parameters. Source keys are listed at the
end; every claim names its source and date.

## What a plug-in can and cannot control

A plug-in controls: which parameters it publishes as automatable, their order in its own list, their titles, their
value text and units, and the stability of their VST3 IDs. It does not control: which parameters Live puts in the
device panel beyond the first 64 at instantiation, the order of that panel once the user edits it, how many fit
(128), the grouping into banks (consecutive runs of eight), the bank names ("Bank 1", "Bank 2", ...), which bank Push
shows, or whether Push uses the drum layout. Live ignores, as far as any source shows, VST3 units. The levers that
remain are outside the plug-in: a saved Configure list (per instance in the set, or as a default per plug-in), a Rack
preset with up to 16 named Macros, or a Max for Live device whose own banks Push does name.

## 1. Which parameters Live exposes, and how many

- At instantiation, Live fills the device panel only when the plug-in has at most 64 modifiable parameters; above
  that "Plug-ins that contain more than 64 parameters will open with an empty panel" [M24, Live 12 manual]. Same
  figure in [KB-cfg] (edited 2021-03-03, updated 2024-08-29) and, for VST3, in the Live 10.1 release notes ("For VST3
  plug-ins with less than 65 parameters, the parameters are now auto-assigned after instantiation") [RN10, 10.1,
  2019]. MD Drums today has 16 x 27 = 432 parameters (Machine, 24 parameters, Level, Mute per Track,
  `mdDrumsPlugin/mdDrumsProcessor.cpp:74-121`), and more than 500 planned: it opens with an empty panel.
- With an empty panel, Push 2 shows "No parameter mapped" for a plug-in device (`ParameterView.qml:122`, Push 2 QML
  of the installed Live 12.2.1 [QML]).
- The threshold is a hidden option, `-_PluginAutoPopulateThreshold=X` in `Options.txt`: default 64, minimum 1,
  maximum 128, and "128 will always populate the list with max. 128 parameters, regardless of how many parameters the
  plug-in has" [KB-opt, edited 2026-03-18]. Ableton calls these options experimental and unsupported [KB-opt], and
  Live 12.4.3 fixed "a crash that occurred when using certain VST3 plug-ins with the debug option
  -_PluginAutoPopulateThreshold=128" [RN12, 12.4.3, 2026-07-14]. It is set per user machine, not by the plug-in. A
  forum user reported in May 2026 that the 128 setting "does not work reliably" [F-api, 2026-05-30]; which 128
  parameters it picks is not documented.
- The Configure list holds at most 128 parameters per plug-in instance. No manual page states the number, but Live
  12.2.1 stores the list as a fixed array: every one of the 168 VST3 plug-in devices in the user's sets has exactly
  128 `PluginFloatParameter` slots in its `ParameterList`, unused slots with `ParameterId` -1 [ALS, scanned
  2026-10-08]; one plug-in (Minimonsta2) has all 128 slots used and none more. Forum users have reported the same cap
  since at least 2012 ("only allows 128 parameters to be added from VST plugins") [F-128, 2012-04-20].
- Live can only configure parameters the plug-in publishes: "certain plug-ins do not 'publish' all of their
  parameters to Live. These parameters cannot be added to Live's panel" [M24]. JUCE publishes a parameter as
  automatable when `isAutomatable()` is true (`kCanAutomate`), read-only for meters
  (`juce_audio_plugin_client_VST3.cpp:877-881`, JUCE 7.0.10 in this repository [JUCE]). The 16 x 128 "MIDI CC" and 16
  "MIDI PC" parameters JUCE adds for MIDI CC emulation carry no flags (`:1695-1714`), so they are not automatable.
- Parameters enter the panel only by Configure (click or move the control in the plug-in window), by recording or
  editing automation, or by MIDI/key/Macro mapping [M24]. The Live API cannot configure them: a Device's
  `parameters` holds "only automatable parameters" [LOM, (c) 2024], which for a plug-in are the configured ones, and a
  May 2026 feature request asks Ableton for exactly that missing configure API [F-api, 2026-05-29].

## 2. How Push 2 turns the panel into banks

- In Device Mode the upper display buttons select a device of the selected Live track; pressing it again enters
  Edit Mode, where "the lower display buttons select additional pages of parameters"; with more than eight pages the
  rightmost lower button shows an arrow to scroll [M36, Live 12 manual, 36.10].
- A plug-in gets the generic bank: no bank definition exists for class `PluginDevice` and a plug-in has no
  `get_bank_count`, so `create_device_bank` falls back to `DeviceParameterBank`, which takes `device.parameters[1:]`
  (skipping "Device On") in runs of eight and names bank n "Bank %d" (`ableton/v2/control_surface/
  device_parameter_bank.py:12-80,170-181`, `banking_util.py:14,59-95`) [SCR, decompiled Live 12 scripts, commit
  2026-05-27]. The installed Live 12.2.1 bytecode of `banking_util.pyc` (compiled 2025-04-14) contains the same
  `"Bank %d"` format and `get_bank_count` call [SCR-local]. The Push 2 QML gives plug-ins their own view state
  ("vst", `deviceType` "PluginDevice" or "AuPluginDevice") that shows eight parameters
  [QML, `Push/View/Device/Parameters.qml:17-18,100-123`].
- The bank content is the Live panel in its displayed order: the Live 9.6.1 notes fixed that "When adding plug-in
  parameters to the parameter list in Device View via 'Configure', the device bank in Push Device mode would only
  update after switching back" [RN9, 9.6.1, 2016]; Ableton's KB says the configured controls "will appear in Push's
  LCD display" [KB-p1, edited 2021-03-05]. The panel order is Live's, stored per slot as `VisualIndex` next to the
  VST3 `ParameterId` [ALS]; the user reorders it by drag and drop in Configure Mode [M24].
- So bank k is panel positions 8k-7 to 8k, the 128-slot cap gives at most 16 banks, and bank names cannot be changed:
  "Nope" from a forum veteran and only workarounds from an Ableton moderator (Racks with named Macros) [F-banks,
  2021-10-15/18]. Ubermap patched Live's Push scripts to rename plug-in banks and parameters; it has been unmaintained
  since 2023 [Ubermap, last push 2023-02-05].
- Devices that do name their banks are Live's own (bank definitions keyed by class name, e.g.
  `Push2/custom_bank_definitions.py`) and Max for Live devices (`MaxDeviceParameterBank`, names from
  `get_bank_name`) [SCR]. A Max for Live device declares them with `live.banks`: named banks of up to 8 parameters,
  editable at run time, "as displayed on Ableton's Push controllers" [BANKS, (c) 2024].
- Racks: when a Rack is selected the encoders control its Macros [M36, 36.10.2]; Racks have up to 16 Macros with
  custom names and colors [M25, 25.7 Using the Macro Controls]; Macros 9-16 appear on Push 2 as an extra device
  inside the open Rack, marked with a bullet [RN11, 11.0, 2021].

## 3. What the Push 2 display shows

- Name: Live uses the VST3 title, not the 8-character `shortTitle`. JUCE fills `title` with `getName(128)` and
  `shortTitle` with `getName(8)` (`juce_audio_plugin_client_VST3.cpp:900-902`) [JUCE], and the names Live stores for
  JUCE plug-ins are longer than 8 ("A FLT ENV DECAY" for JE8086) [ALS].
- The label is drawn upper-case in 14 px Ableton Sans Light (`alluppercase nohinting 14px`) across the whole width
  of the parameter's widget, clipped with an 18 px fade on the right when the text is wider; there is no ellipsis
  [QML: `Push/View/ParameterView.qml:64-70`, `Widgets/EncoderDisplay.qml:76-86`, `Widgets/FadableLabel.qml`,
  `Push/Assets/css/dark.css:135-142,186-191`]. The widget runs from the encoder column's light guide to the next
  column's button guide; with Push 2's geometry (960 px screen, 121 px per encoder, light guide at +14 px, button
  guide at +4 px, `Push2/visualisation_settings.py:20-37` [SCR]) that is 111 px. That leaves about 10 capitals fully
  legible and one or two more inside the fade (an estimate from the font size, not measured on the device). The large
  value below uses the same width in 28 px Ableton Sans ExtraLight, also faded [QML: `Widgets/EncoderDisplay.qml:88-112`,
  `dark.css:127-129`].
- Value: Push extracts the digits and the symbols ° / % . : - + from the value text; if they parse as a number, that
  number is the large value, otherwise the whole text is shown; the unit is the trailing word after a final number
  (`Push2/model/repr.py:46-75`, `get_parameter_display_large/small`) [SCR]. The installed 12.2.1 `repr.pyc` contains
  the same pattern [SCR-local]. Consequences for MD Drums' value texts (Machine names from `mdLib/mdmachines.cpp`):
  "PTCH 64" shows "64", "ROM-01" and "MID-01" show "-1", "TRX-B2" shows "-2", while "EFM-BD" and "E12-BD" ("12-" is
  not a number) stay whole. A 2018 forum report saw exactly this ("Push2 converts the parameter
  text values containing numbers into just the numbers") and worked around it by replacing digits with letters
  [F-value, 2018-03-30].
- The value text is re-read when the parameter's value changes: the adapter notifies `displayValue` on the "value"
  event only (`repr.py:155-156`) [SCR]. A text that changes because another parameter changed (SYN1 renamed by a new
  Machine) is not refreshed on Push until SYN1 itself moves (inference from the code, not tested).
- A list of choices (instead of a dial) appears only for quantized parameters with value items (`repr.py:164-168`,
  `ParameterDisplay.qml:77-84`) [SCR, QML]. Live stores every configured plug-in parameter as `PluginFloatParameter`
  (14,987 configured VST3 slots, 12 VST2, no other type) [ALS]; JUCE never sets the VST3 `kIsList` flag [JUCE].
  Whether any VST3 parameter shows as a list on Push 2 is unconfirmed; expect dials with numbers.
- Values and units of plug-in parameters have been shown on Push since Live 9.0.2, "provided that the VST plug-in
  transmits these information" [RN9, 9.0.2, 2013].

## 4. VST3 units, names that change, parameters added later

- JUCE turns each `AudioProcessorParameterGroup` into a VST3 unit (`getUnitInfo`, unit ID = hash of the group ID,
  `:436-479,598-613`) and sets each parameter's `unitId` (`:1654-1658`) [JUCE]. MD Drums already groups by Track
  ("Track 1" ... "Track 16", `mdDrumsProcessor.cpp:79-80`).
- No source shows Live using units. The Live 12 manual's plug-in chapter, the Live 9 to 12 release notes and the
  Ableton KB never mention VST3 units or parameter groups [M24, RN9-RN12, KB]; Push 2's bank code never sees them
  (banks are positional slices of the panel) [SCR]; searches found no report of units in Live's automation choosers
  (2026-10-08). Treat units as ignored by Live and Push 2; they remain useful in hosts that show them.
- Names that change: the Live 9.6 notes say "Push 2 now also updates such plugin parameter names dynamically" [RN9,
  9.6, 2016], three years before Live supported VST3 (10.1, 2019 [RN10]). For VST3 the mechanism is
  `restartComponent(kParamTitlesChanged)`, after which "The host invalidates all caches of parameter infos"
  (`ivsteditcontroller.h:112-115`, VST3 SDK in this repository); JUCE sends it from
  `updateHostDisplay(ChangeDetails().withParameterInfoChanged(true))` and adds `kLatencyChanged` only when the latency
  really changed (`juce_audio_plugin_client_VST3.cpp:1408-1470`) [JUCE]. Whether Live 12 re-reads VST3 titles on that flag is unconfirmed: no Ableton source says so, and other
  hosts are reported to ignore it (search summaries of JUCE and Renoise forum threads, 2018-2022, not read in full).
  Live stores each configured parameter's name in the set [ALS], and Live 12.4 fixed names of automated plug-in
  parameters "not restored as expected when loading a Live Set" [RN12, 12.4, 2026-05-05].
- Parameters added later: Live auto-fills the panel only at instantiation [M24], so new parameters never appear by
  themselves in an existing instance; they must be configured. JUCE's VST3 ID is the hash of the string parameter ID
  unless `JUCE_FORCE_USE_LEGACY_PARAM_IDS` (`:574-581,733-749`) [JUCE], and Live identifies configured slots by that
  ID (`ParameterId` 1463456674 and neighbours for JE8086, a JUCE plug-in) [ALS]. Appending a parameter therefore
  leaves existing slots, automation and order intact; changing a string ID, or removing a parameter, orphans its slot:
  "Plug-in parameters that are no longer available will be displayed as deactivated" [RN11, 11.1, 2021]. JUCE has no
  path to add parameters at run time (no `kReloadComponent` in the wrapper [JUCE]).

## 5. Can a plug-in define its banks or follow a selection?

- No. Push 2 has three bank sources: Live's built-in definitions per device class, Max for Live banks, and the
  generic slicing of the panel [SCR]. A VST3 always gets the last; VST3 has no bank interface, and the plug-in cannot
  edit Live's panel (section 1). The bank shown is chosen by the control surface script (`DeviceParameterBank.index`)
  [SCR], not by the device.
- What remains, all outside the plug-in:
  - **A prepared Configure list**: up to 128 parameters in a chosen order, i.e. 16 unnamed banks. It is saved with
    the set per instance [M24], or as a default for every new instance with "Save as Default Configuration", stored in
    `User Library/Defaults/Plug-In Configurations` (VSTs folder, `Default.appc`) [M23, 23.2.6.2; RN9, 9.5, 2015;
    KB-cfg]. The user's library has no such file yet (checked 2026-10-08).
  - **A Rack preset** containing the configured plug-in, saved to the User Library [M24; KB-p1]: Push 2 then shows the
    Rack's Macros, up to 16, named and colored [M25; RN11].
  - **A Max for Live device** next to MD Drums, with named `live.banks` whose parameters drive MD Drums' configured
    parameters through the Live API [BANKS, LOM]. It can rename and reorder banks at run time, but still reaches only
    the 128 configured parameters [LOM, F-api]. The user has Live 12 Suite, which includes Max for Live. Its effect on
    automation and latency is untested.
  - **Relay parameters inside MD Drums**: a fixed set of host parameters whose target follows the Track shown in the
    editor. Push 2 would show them as ordinary parameters; their names can only follow the selection if Live honours
    `kParamTitlesChanged` (unconfirmed, section 4), and their automation means "whatever Track is selected", which
    Live cannot know.
- Following Live's own selection works as usual: Push 2 follows the selected Live track and device [M36].

## 6. Pads with a plug-in instrument

- Push 2 uses the drum layout only when the Live track holds a Drum Rack (`can_have_drum_pads`) or a Simpler in
  Slicing mode (`ableton/v2/control_surface/percussion_instrument_finder.py`, `find_drum_group_device`,
  `find_sliced_simpler`) [SCR; M36, 36.3]. The installed 12.2.1 bytecode references `can_have_drum_pads` there
  [SCR-local].
- With MD Drums alone on a MIDI track, Push 2 plays melodic: the bottom-left pad plays C1 (MIDI 36), each pad up is
  a fourth, each pad right the next scale note, C major In Key by default [M36, 36.5]. Notes 36-51 are therefore not
  under one hand by default. Layout "Sequent" with Chromatic puts all notes in order without repeats [M36, 36.5.1],
  which should give 36-43 on the bottom row and 44-51 on the second (derived, not tested). Scale settings are saved
  with the set [M36, 36.5.1].
- In a Drum Rack, a chain whose Receive is "All Notes" "simply passes the note that it receives to its devices"
  (Play and Choke disabled) [M25, 25.6 Drum Racks, I/O section]; Live 12.3 added `DrumChain.in_note`, -1 meaning
  "All Notes" [RN12, 12.3, 2025-11-25]. Push 2's drum pads play the Drum Rack's visible pads, base note 36
  (`ableton/v2/control_surface/components/drum_group.py:13,76-85`) [SCR], so the default lower-left 4 x 4 pads send
  36-51, MD Drums' trigger notes, in the classic 4 x 4 layout [M36, 36.3.1]. Practitioners use this wrapper for
  third-party drum plug-ins (search summary of forum and vendor guides, 2016-2024); one 2016 thread confirms only a
  Max for Live MIDI send/receive variant [F-drum, 2016-07-14].
- Pad colors in the drum layout: a pad with a chain takes its first chain's color, otherwise the Live track's color
  (`Push2/drum_group_component.py:108-128`) [SCR]; the manual lists track color for "contains a sound", lighter for
  empty, green for playing, white for selected [M36, 36.3.1]. With only an All Notes chain, pads 36-51 may light as
  empty; adding one empty chain per Track note, named and colored per Track, might fix the colors (unconfirmed).
- Velocity: pads are velocity sensitive, shaped by Pad Sensitivity, Gain and Dynamics in Setup (linear curve: Gain 4,
  Dynamics 7); Accent plays 127; 16 Velocities mode enters steps at chosen velocities [M36, 36.3.2, 36.16].
- Multi-out: Live gives access to "up to 16 stereo outputs of one plug-in" via other tracks' Audio From [KB-multi,
  edited 2024-07-23]; whether MD Drums' Outs stay reachable when it sits inside a Drum Rack chain is unconfirmed.

## 7. What a set keeps, what is lost

- Kept in the set, per instance: the Configure list ("unique for each instance ... saved with the Set" [M24]), each
  slot as VST3 `ParameterId`, `VisualIndex`, `ParameterName` and `ParameterValue` [ALS]; Rack Macros and their names;
  shown/hidden Macros [M25]; Push scale settings [M36].
- Kept outside the set: the default configuration per plug-in in the User Library [M23]; Rack presets [M24].
- Lost or degraded: a configured parameter whose VST3 ID no longer exists becomes deactivated [RN11, 11.1]; JUCE
  derives that ID from the string ID, so a renamed string ID counts as removed [JUCE]. Inserting parameters in the
  middle of the plug-in's list changes nothing for an existing instance (slots are matched by ID), but would with
  `JUCE_FORCE_USE_LEGACY_PARAM_IDS` (index IDs) [JUCE]. A parameter whose title changes keeps its slot; which name Live
  then shows (stored or new) is unconfirmed [RN12, 12.4].

## 8. What ticket 11 has to work with

1. At most 128 MD Drums parameters on Push per instance, as 16 unnamed banks of 8 in Live's panel order; Push
   numbers them "Bank 1" to "Bank 16".
2. Nothing appears on Push until the panel is configured (more than 64 parameters): the design must ship the list,
   as a default configuration or a Rack preset, or explain the Configure step.
3. Names: the full title, upper-cased, about 10 capitals legible in 111 px; put the distinguishing part first
   (Track number, then function). Value texts with digits are reduced to the number: "PTCH 64" reads "64".
4. Units and parameter groups do not shape Push banks; stable string IDs, appended at the end, keep configured
   slots valid.
5. Banks that follow the editor's Track need relay parameters, a Max for Live device, or Rack Macros; none is free.
6. Drum layout needs a Drum Rack around MD Drums (All Notes chain); notes 36-51 then match the default 4 x 4 pads.

## Unconfirmed, to test in Live 12.2.1 with Push 2

- Whether Live re-reads VST3 parameter titles after `kParamTitlesChanged`, in the panel and on Push 2.
- Which parameters `-_PluginAutoPopulateThreshold=128` picks for a plug-in with more than 128 (first 128 by index?).
- Whether a JUCE `AudioParameterInt` or `AudioParameterBool` shows as a dial or a list on Push 2.
- Drum Rack wrapper: pad colors and names with an All Notes chain, with or without per-note empty chains; latency
  added (none expected); access to MD Drums' Outs from inside a Rack chain.
- Exact number of characters legible in a Push 2 parameter label.

## Sources

Ableton documentation (fetched 2026-10-08):
- [M23] Live 12 manual, ch. 23 "Working with Instruments and Effects" (defaults, Plug-In Configurations folder):
  https://www.ableton.com/en/manual/working-with-instruments-and-effects/
- [M24] Live 12 manual, ch. 24 "Using Plug-Ins", 24.1 and 24.1.2 Configure Mode:
  https://www.ableton.com/en/manual/using-plug-ins/
- [M25] Live 12 manual, ch. 25 "Instrument, Drum and Effect Racks" (Drum Rack I/O, Macros):
  https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/
- [M36] Live 12 manual, ch. 36 "Using Push 2": https://www.ableton.com/en/manual/using-push-2/
- [RN9] [RN10] [RN11] [RN12] Live release notes: https://www.ableton.com/en/release-notes/live-9/ (and live-10,
  live-11, live-12). Pages for 9 to 11.1 carry no dates; years given are the releases' years. Live 12 items carry
  dates.
- [KB-opt] "Options.txt file", help.ableton.com 6003224107292, edited 2026-03-18.
- [KB-cfg] "Saving plug-in parameter configurations", 209073089, edited 2021-03-03, updated 2024-08-29.
- [KB-p1] "Push 1: Using Third Party Plug-Ins", 209071489, edited 2021-03-05 ("Live Versions: All").
- [KB-multi] "Using multi-out plug-ins", 209773065, edited 2024-07-23.
- Also read: "Push 1 & 2: Browsing plug-in presets" 209776485 (edited 2021-03-05: VST3 presets cannot be browsed from
  Push 2), "Automating plug-in parameters that can't be configured" 209067009 (edited 2021-03-03), "Using Push 2 FAQ"
  209072549 (edited 2024-01-11). KB pages were read through the Zendesk API
  (`help.ableton.com/api/v2/help_center/en-us/articles/<id>.json`); the HTML pages refuse scripted access.

Local primary sources (read 2026-10-08):
- [QML] Push 2 display code of the installed Live 12.2.1, `C:\ProgramData\Ableton\Live 12 Suite\Program\Push2\qml\
  Ableton\` (`Push/View/ParameterView.qml`, `Push/View/Device/Parameters.qml`, `Push/View/Device/Banks.qml`,
  `Push/Widget/ParameterDisplay.qml`, `Push/Widget/DialDisplay.qml`, `Push/Widget/BankListView.qml`,
  `Push/View/Layout/Guides.qml`, `Push/Assets/css/dark.css`, `Widgets/EncoderDisplay.qml`,
  `Widgets/FadableLabel.qml`), files marked "Copyright: 2025, Ableton AG".
- [SCR] Live 12 MIDI Remote Scripts decompiled by Julien Bayle, https://github.com/gluon/AbletonLive12_MIDIRemoteScripts,
  commit e83d5192 (2026-05-27, README: Live 12.4 sources); the files read were compiled 2024-03-09.
- [SCR-local] Bytecode of the same scripts in the installed Live 12.2.1, `C:\ProgramData\Ableton\Live 12 Suite\
  Resources\MIDI Remote Scripts\`, compiled 2025-04-14, checked for the strings cited.
- [ALS] The user's Live 12.2.1 sets under `Documents\Ableton\User Library` (168 VST3 plug-in devices), read for the
  structure of `PluginDevice/ParameterList` only.
- [JUCE] `source/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp` (JUCE 7.0.10 with
  Gearmulator's changes) and `source/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/vst/
  ivsteditcontroller.h` in this repository.

Cycling '74 (fetched 2026-10-08):
- [LOM] Live Object Model, Device: https://docs.cycling74.com/apiref/lom/device/ (page (c) 2024; mentions Live 12.3).
- [BANKS] `live.banks` reference: https://docs.cycling74.com/reference/live.banks/ ((c) 2024).

Practitioner sources:
- [F-api] Ableton forum, "[Feature Request] Live API: auto-discover all plugin parameters names + configure devices
  parameters", 2026-05-29/30: https://forum.ableton.com/viewtopic.php?p=1837238
- [F-128] Ableton forum, "Unlimited Vst Parameter 'Configure'", 2012-04-20 to 2020-01-13:
  https://forum.ableton.com/viewtopic.php?t=179472
- [F-banks] Ableton forum, "naming parameter banks of 3rd party plugins", 2021-10-15 to 2021-10-25:
  https://forum.ableton.com/viewtopic.php?t=243933
- [F-value] Ableton forum, "Push 2 displaying wrong VST parameter value", 2018-03-24 to 2018-03-30:
  https://forum.ableton.com/viewtopic.php?t=229927
- [F-drum] Ableton forum, "force drum rack layout on push", 2016-06-29 to 2016-07-14:
  https://forum.ableton.com/viewtopic.php?t=222205
- Ableton forum, "Push2 shows all NI Massive parameters in 9.7.5!!!", 2017-10-25 to 2018-11-13 (16 banks of 8 with
  the threshold option): https://forum.ableton.com/viewtopic.php?f=55&t=227967
- [Ubermap] https://github.com/tomduncalf/ubermap, "No longer maintained", last push 2023-02-05.
