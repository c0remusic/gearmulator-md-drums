# Dragging your own audio from Live onto MD Drums' ROM machines

Research for the question: can a user drag an audio file from Ableton Live onto MD Drums' window so that the ROM
machines (ROM-01 to ROM-48, which play the UW sample bank) play it? Written 2026-10-16 against `main` at adb536eb, with
a dirty tree; nothing was built, run in a host or changed in the repository. Paths are relative to the repository root.

Each fact carries a mark:

- **[V: where]**: verified, read at the place named (a numbered source below, or a file and line in this tree).
- **[M]**: measured here, by a read-only script run on the user's flash image (see "The factory bank", section 3).
- **[I]**: inferred, from the verified facts around it; not read anywhere.

## In short

- Live does drag files out to other programs and plug-in windows at the OS level: its release notes name drag and drop
  "between Live, plug-ins and Max and the Explorer" [V: S1], and plug-in developers report a drag from Live's browser
  reaching a JUCE `FileDragAndDropTarget` [V: S6]. Ableton documents none of this for plug-ins; the manual describes
  drags into Live's own devices only [V: S3, S4].
- A drag from **Explorer or Finder** reaches a plug-in window in Live the same way it reaches any window [V: S6, S8].
  On Windows it fails when Live runs elevated, which Windows blocks by design [V: S9, S10].
- A drag of a **clip** from Session or Arrangement, or of a **Simpler / Drum Rack pad**, is not documented by Ableton
  for third-party plug-ins, and the user reports conflict or are years old [V: S7, S11, S12]. Whether Live hands over
  the clip's source file, a temporary file or nothing has to be measured in Live 12 on both systems [I].
- Some of Live's own Pack samples are reported unreadable outside Live (protected AIFF) [V: S13, S14 (user reports);
  Ableton has a support article titled "The license to use certain protected samples is missing", S15].
- This repository's stack already routes OS file drops to the RmlUi element under the cursor (`juceRmlUi`); an element
  takes files by being a `juceRmlUi::DragTarget`. MD Drums has no such element today [V: code, section 2].
- JUCE here is **7.0.10** (fork `joelanders/JUCE-md-mm`), and the build turns FLAC, Ogg Vorbis, MP3 and Windows Media
  off: `AudioFormatManager::registerBasicFormats()` reads **WAV and AIFF only on Windows**, plus whatever Core Audio
  reads on macOS [V: `source/juce.cmake:84-87`, JUCE source].
- The Machinedrum UW MKII has 48 ROM slots, 4 RAM machines and 2.5 MB of sample memory shared with them; it takes
  4-48 kHz mono over MIDI SDS and plays everything at 12 bits [V: S16].
- In this engine the factory bank fills P:$150000-$18FC12 (261,138 words, 32 samples); below $200000, the end of the
  voice DSP's P memory here, **459,758 words remain: 919,516 twelve-bit samples, 20.85 s at 44.1 kHz** [M, V: code].

## 1. What Live hands a plug-in window

### The OS layer a plug-in sees

A plug-in window receives a drop only if the drag source puts an OS-level drag on screen. On Windows that is an OLE
drag (`DoDragDrop`), which goes to "the window under the current cursor location" if it is a registered drop target
[V: S17]; a file list travels as `CF_HDROP`. On macOS it is an `NSDraggingSession` whose pasteboard carries file URLs.
A host can also drag data only it understands: Xfer's developer, about dragging a host's region onto Serum, wrote that
"Serum needs an actual file drag" and otherwise "just gets handed a bunch of meaningless data" [V: S11, 2016].

### (a) A sample from Live's browser

- Live 10.0.2 (Windows) release notes: "On Windows 10 (version 1803) with HiDPI enabled, drag and drop now works as
  expected between Live, plug-ins and Max and the Explorer." The same notes warn that drag and drop "from Live to
  other programs or to HiDPI-aware plug-ins may still cause issues when HiDPI Mode is active" [V: S1].
- A JUCE developer, macOS, 2018: "on Ableton Live, I can drag and drop audio file from host browser into my plugin";
  the same drag from FL Studio's browser never called `isInterestedInFileDrag` [V: S6].
- History: in 2008 a sampler's developer found Live's browser drags never reached his plug-in ("Live doesn't use
  regular drag&drop handling") [V: S18]; by Live 8.0.2 (2009) users dropped browser samples into Battery and Kontakt
  [V: S12]. Treat anything older than Live 10 as history [I].
- Sunhouse forum, May 2026: a user dragging an `.aif` "from the Ableton library or directly from File Explorer" into
  Sitala got a pad with "the sample is always missing"; it worked once the folders were added to Live's browser as
  custom folders instead of the built-in categories. Sunhouse staff suspected Ableton's DRM [V: S13]. Live's
  Max for Live users report the same for Pack AIFF files ("WAV files work") [V: S14]. These are user reports; Ableton
  has published no format for protected samples [I].
- What path arrives: for a plain file in a user folder, the file's own path [V: S6, S8, the plug-in loads it]. For a
  file Live decodes (MP3, FLAC), Max's `live.drop` documents that the path "refers to a temporary decoded copy" unless
  its `decodemode` is off [V: S19]; whether a VST3 window gets the original or that copy is not documented [I].

### (b) A file from Explorer or Finder

- Works in Live as in any host: a 2023 JUCE thread: "while running the plugin via Ableton I can drag/drop MIDI files
  from my hard drive" [V: S7]; the Sitala report above dropped from Explorer too [V: S13].
- Windows: an elevated process does not receive drops from the unelevated Explorer. Microsoft: dragging from Explorer
  at medium integrity to an elevated window "has its Windows messages blocked" (UIPI) [V: S9]. Live users hit it when
  Live runs "as administrator" [V: S10]. A plug-in inside an elevated Live is in the same process, so it is blocked
  too [I].

### (c) An audio clip from a Session or Arrangement track

- Ableton documents clip drags only inside Live: onto the browser (a Live Clip, `.alc`, which "only contain[s]
  references to samples on disk") [V: S3], onto Simpler or Sampler ("bringing a warped clip into Simpler from an
  audio track ... preserves your manual warp settings", so Live's internal drag carries clip settings) [V: S4]. The
  manual exports MIDI clips through File > Export MIDI Clip, not by dragging [V: S3]. No release note from Live 9 to
  12.4 says clips can be dragged to other applications [V: S1, searched "drag clips to Finder/Explorer", "drag MIDI
  clip out", "plug-in drag and drop"].
- User reports, none from Ableton:
  - 2009, Live 8: dragging "from say, a track in Arrange" into a third-party plug-in "still can't be done" [V: S12].
  - 2018 and 2021, Windows: recorded clips could not be dragged into a third-party VST; "windows says file is locked
    cause opened in live" [V: S20].
  - 2023, JUCE forum: MIDI clips from Live's timeline do not "activate the FileDragAndDropTarget component in any
    way"; the poster believed audio clips can be referenced "with a drag-and-drop operation", untested in the thread
    [V: S7].
  - Bitwig (not Live), 2015: arrangement clips arrive as a `.bwclip` container that samplers cannot read [V: S21].
- So: whether Live 12 offers a clip as a file, and which one (the source file, whole and unwarped; a consolidated
  temporary file; or Live-only data) is **unknown** [I]. If it offers the source file, the clip's start, end, warp
  and gain are not in it, since the clip only references the sample [V: S5: a clip holds "what part of the sample to
  play and how"; I: they would be lost].

### (d) A Simpler or Drum Rack pad's sample

The manual documents drags *onto* pads and into Simpler ("Almost any object from Live's browser ... can be dragged
onto a pad") and nothing about dragging a pad's or Simpler's sample out to another device [V: S4]. No report found of
a third-party plug-in receiving one [I: not found, not proven absent].

### Third-party plug-ins on record

| Plug-in | From Live's browser | From a clip | Source |
|---|---|---|---|
| A JUCE plug-in (macOS) | file path received | not tried | [V: S6] |
| Sitala / Sunhouse SP2 | pad made, sample "missing" for Pack `.aif`; custom folders work | not tried | [V: S13] |
| Battery, Kontakt (Live 8) | loads | "can't be done" from Arrange | [V: S12] |
| ShortCircuit, Morgana (2008) | no drag notification | not tried | [V: S18] |
| Serum (any host) | needs an OS file drag | region drags give "meaningless data" | [V: S11] |
| Unnamed third-party VST (Windows) | not tried | recorded clip fails, file locked | [V: S20] |

No vendor of TAL-Sampler, XO, Serato Sample, Kontakt or Sitala documents Live clip drags; searches for each found
nothing first-party [I: absence of evidence].

### To measure, since nothing documents it

For Live 12 on Windows and on macOS, with a probe that logs every drag a JUCE window receives (files, text, and the
JUCE `DragAndDropTarget::SourceDetails` description): a browser sample from Places, from a Pack, from Core Library,
an MP3 and a FLAC; an audio clip from Session and from Arrangement, warped and trimmed; a freshly recorded clip; a
Simpler and a Drum Rack pad [I].

## 2. JUCE and this repository's stack

### JUCE version

`source/JUCE` is the fork `https://github.com/joelanders/JUCE-md-mm.git` at f96dadf (2026-09-14), version
**7.0.10** [V: `.gitmodules`, `source/JUCE/modules/juce_core/system/juce_StandardHeader.h:30-32`].

### Windows: how a drop reaches the editor

- Every `HWNDComponentPeer`, the plug-in editor's included, creates a `FileDropTarget` and calls
  `RegisterDragDrop(hwnd, dropTarget)` when its window is created; a non-repainting child window (the OpenGL one)
  hands drops to the peer that owns its parent [V: `juce_gui_basics/native/juce_Windowing_windows.cpp:2648-2662`].
  `juce_OpenGL_windows.h:289` makes that child with `createNonRepaintingEmbeddedWindowsPeer` [V]. JUCE uses OLE, so
  `DragAcceptFiles` is not involved [V: same file, no call to it].
- `FileDropTarget` reads `CF_HDROP` first (each file through `DragQueryFile`) and falls back to `CF_UNICODETEXT` as
  text; it answers `DROPEFFECT_COPY` when a component took the drag [V: same file, 2173-2304].
- Drop positions go through `convertPhysicalScreenPointToLogical` [V: same file, 2224-2229]. JUCE 6 had a bug where
  scaled Windows displays sent drops to the wrong component, in Ableton among others; JUCE's Ed fixed `getMousePos`
  in July 2020 [V: S22]. This build sets `JUCE_WIN_PER_MONITOR_DPI_AWARE=1` [V: `source/juce.cmake:83`].

### macOS

`NSViewComponentPeer` registers its view for `NSPasteboardTypeFileURL`, `kPasteboardTypeFileURLPromise` and
`NSPasteboardTypeString` [V: `juce_NSViewComponentPeer_mac.mm:187, 1451-1462`]. It reads file URLs from the pasteboard
as `NSURL` objects; a file promise is resolved only for iTunes' own type [V: same file, 1489-1530], so a drag that
offers only a promise arrives with no files [I]. With the Metal renderer, `juceRmlUi` adds a plain `NSView` over the
component through `NSViewComponent::attachViewToComponent` [V: `source/juceRmlUi/MetalContext.mm:194-205`]; that view
registers no drag types, so drags fall through to JUCE's view [I: AppKit's lookup, not verified here].

### JUCE problems on record

- A target needs `public` inheritance; a private base compiles and never fires [V: S23, 2018].
- An attached OpenGL context once stopped file drops (JUCE 2.0.36, fixed in a later version) [V: S24, 2013].
- An elevated process (Projucer run as admin, 2024) receives no drops [V: S25].
- Dragging *out* of a plug-in into Live: `performExternalDragDropOfFiles` failed in Ableton on both systems until the
  temporary file's delete callback was removed: it fired "before Ableton completes the operation" [V: S26, 2020].

### What the stack does with a drop

- `juceRmlUi::RmlComponent` is a `juce::FileDragAndDropTarget` (and `DragAndDropTarget`, `DragAndDropContainer`), not
  a `TextDragAndDropTarget`: text drags are not taken [V: `source/juceRmlUi/juceRmlComponent.h:32`]. Its five file
  callbacks lock the RmlUi document and forward to `RmlDrag` [V: `juceRmlComponent.cpp:1477-1505`].
- `RmlDrag::isInterestedInFileDrag` returns true for any file list ("individual elements need to decide")
  [V: `juceRmlDrag.cpp:49-54`], so the window takes every file drag wherever the cursor is [I].
- `fileDragEnter` creates an invisible 100x100 `div` carrying a `FileDragSource` with the paths, appends it to the
  document and injects a mouse press and moves so that RmlUi starts its own drag; `fileDragMove` moves the mouse;
  `filesDropped` injects the release; `fileDragExit` removes the `div` [V: `juceRmlDrag.cpp:56-187`].
- `juceRmlUi::DragTarget` listens for RmlUi's `Dragover`/`Dragdrop` on its element; for a `FileDragData` it calls
  `canDropFiles(event, files)` while hovering and `dropFiles(event, data, files)` on release; the base `canDropFiles`
  returns false [V: `source/juceRmlUi/rmlDragTarget.cpp:47-155`].
- **How an element takes files:** a class derives from `juceRmlUi::DragTarget`, calls `DragTarget::init(element)`
  (which tags the element with the `dragTarget` attribute and the `:dragtarget` pseudo-class), and overrides
  `canDropFiles` and `dropFiles` [V: `rmlDragTarget.h:56-74`, `rmlDragTarget.cpp:47-65`].
- **Who uses it:** the Patch Manager (list entries and data-source tree nodes take files for local storage and call
  `loadPatchesFromFiles`) [V: `jucePluginEditorLib/patchmanagerUiRml/listitem.cpp:190-275`,
  `datasourceTreeNode.cpp:249-252`]; part buttons (one file, `activatePatch`) [V: `jucePluginEditorLib/partbutton.cpp:94-176`];
  Xenia's wave editor, which imports a `.wav` dropped on a wave slot or the graph through `synthLib::WavReader`,
  8-bit mono, 64 or 128 samples only [V: `xtJucePlugin/weWaveTreeItem.cpp:130-170`, `weGraph.cpp:226-239`,
  `xtWaveEditor.cpp:742-800`]. **MD Drums uses none of it**: no `DragTarget`, `DragSource` or Patch Manager in
  `source/elektron/md/mdDrumsPlugin/` [V: grep].
- The stack also drags *out*: an RmlUi drag source whose data `canExportAsFiles()` becomes a JUCE external drag when it
  leaves the window [V: `juceRmlDrag.cpp:104-138`].

### Audio formats this build can read

- `AudioFormatManager::registerBasicFormats()` registers WAV and AIFF always, FLAC if `JUCE_USE_FLAC`, Ogg Vorbis if
  `JUCE_USE_OGGVORBIS`, `CoreAudioFormat` on macOS and iOS, MP3 if `JUCE_USE_MP3AUDIOFORMAT`, Windows Media if
  `JUCE_USE_WINDOWS_MEDIA_FORMAT` [V: `juce_audio_formats/format/juce_AudioFormatManager.cpp:54-78`]. JUCE's own
  defaults are FLAC on, Ogg on, MP3 off, Windows Media on (Windows only) [V: `juce_audio_formats.h:65-111`].
- **This repository turns them off:** `juce_plugin_modules` is compiled with `JUCE_USE_OGGVORBIS=0`,
  `JUCE_USE_MP3AUDIOFORMAT=0`, `JUCE_USE_FLAC=0`, `JUCE_USE_WINDOWS_MEDIA_FORMAT=0` [V: `source/juce.cmake:84-87`].
  `juce_audio_formats` is compiled in through `juce_audio_utils`' dependencies [V: `source/juce.cmake:69-77`,
  `juce_audio_utils.h:45`]. So: **Windows reads WAV and AIFF; macOS reads WAV, AIFF and what Core Audio reads**
  (`kAudioFileGlobalInfo_AllExtensions`, which on current macOS includes MP3, M4A/AAC, CAF and FLAC) [V: code; I: the
  Core Audio list, which depends on the macOS version].
- JUCE's WAV reader takes PCM 8/16/24/32-bit, 32-bit float and `WAVE_FORMAT_EXTENSIBLE` with PCM or float sub-format
  [V: `juce_WavAudioFormat.cpp:1270-1330, 1521-1524`]; its AIFF reader takes AIFF and AIFC with `NONE`, `twos`,
  `sowt`, `fl32` [V: `juce_AiffAudioFormat.cpp:410-470`]. A protected AIFF with another compression type would be
  refused [I].
- `synthLib::WavReader`, which Xenia uses, takes format tag 1 (PCM) or 3 (float) only, not `0xFFFE`
  (`WAVE_FORMAT_EXTENSIBLE`) [V: `source/synthLib/wavReader.cpp:126-127`].
- Nothing outside `source/JUCE` uses `AudioFormatManager` today [V: grep].

## 3. The Machinedrum UW's samples and this engine

### The machine (Elektron's manual, OS 1.63)

- MKII: "48 ROM slots, 4 RAM machines and 2.5MB of sampling memory"; MKI: 32 ROM slots, 2 RAM machines, 2.0 MB.
  A +Drive holds 128 Sample banks of 48 ROM slots on a MKII (6144 slots) [V: S16, introduction and pp. 86-87].
- Rates and depth: "All sample speeds from 4 kHz to 48 kHz are accepted ... Higher rates are accepted but the samples
  are downsampled to half the sampling speed. The Machinedrum UW does not handle stereo samples. All bit depths are
  accepted but will be played back at 12 bit." [V: S16, p. 71]
- Sharing: "Some DSP memory is always reserved for the RAM machines. The memory is shared with the ROM machines"
  [V: S16, A-15]. The sample manager shows "what percentage of the DSP memory remains unused" [V: S16, p. 71].
- Loops: ROM-25 to ROM-48 "are optimized for loops" (linear STRT and END, HOLD up to 2 bars); on every ROM machine,
  "If the sample is looped, a setting of 127 [DEC] will make it loop infinitely"; END below STRT plays reversed
  [V: S16, A-15].
- Loading: "MIDI sample Dump Standard (SDS) is used to transfer samples", closed loop (both ports, with resend) or open
  loop; Elektron recommends its C6; samples can arrive at any time, the sequencer halts meanwhile; SDS's sample number
  picks the slot; the UW is always SysEx device 0 [V: S16, pp. 70-74]. Samples are not part of an ALL DATA SysEx send
  [V: S16, p. 65]. C6 is 32-bit, unsupported from macOS 10.15, "will not be updated further" [V: S27].
- SDS's dump header carries resolution, a sample period in nanoseconds, length and sustain loop start/end in words,
  and a loop type (00 forward, 01 alternating, 7F off) [V: S28].
- The manual gives no longest sample [V: S16, searched]. The OS's rules found in code follow.

### What the OS does with a sample, as this engine redoes it

`md::fw::loadRomBankFromFlash` (`source/elektron/md/mdEngine/tools/mdfw/Firmware.cpp:188-259`) redoes the OS's boot
transfer (`$20d20e`) [V], and matches the firmware's boot word for word on the factory bank
[V: `source/elektron/md/mdEngine/README.md:81-84`, test `mdEngineFirmwareRomBank`]. Its rules [V: Firmware.cpp]:

- The flash bank at $100000: `0xabcd`, a count (at most 48), then per sample 12 words that are SDS's header fields
  (tag `0x1a00 + slot`, bits, period in ns, length, loop start, loop end, loop type with `0x7f` for none, gain) and
  16-bit samples [V: lines 190-212; I: the SDS correspondence].
- A gain at or below `$74ff` scales the samples; then each 16-bit value becomes a 12-bit code through the OS's table
  at flash $7a0000 [V: 214-224].
- A slot stays empty when its period is above 500 us (below 2 kHz) [V: 230-231]. (The manual's floor is 4 kHz.)
- A loop is kept only if its type is not `0x7f`, it lies inside the sample and spans more than 150 samples; a kept loop
  ends the sample at loop end + 1. Forward and alternating are not told apart [V: 233-237; I: the last point, from the
  test `loopType != 0x7f`].
- Directory entry, four words: start address, length used, loop start or `$ffffff`, and the rate as
  `(0x16250000 / period) << 4`, a fraction of 44.1 kHz with 2^18 = 1 [V: 238-241, Firmware.h:76-83]. Samples keep their
  own rate; the voice DSP resamples by that ratio [I].
- Packing: two 12-bit codes per 24-bit word, first in the high half, in chunks of `0x7ff4` samples then `0x7ffe`
  (the OS's flash blocks) [V: 242-253].
- Directory at P:$147E00, ROM slots 1-32 at entries 0-31, slots 33-48 at entries 48-63; entries 32-47 belong to the
  RAM machines [V: Firmware.h:76-80, Firmware.cpp:228].

### The factory bank

A Python transcription of `loadRomBankFromFlash`, run read-only on
`%LOCALAPPDATA%\Programs\Gearmulator-Elektron\elektron_sps1-1uw_os1.63.bin` (script kept outside the repository) [M]:

- 32 samples, ROM-01 to ROM-32; ROM-33 to -48 empty. All stored 16-bit, gain `0x8000`.
- Rates: 44.1 kHz (12 samples), 32 kHz (14), 22.05 kHz (4: ROM-17, -19, -21, -27), 22.5 kHz (ROM-29), 11.025 kHz
  (ROM-32).
- Four looped, all type 0 (forward): ROM-20, -22, -26, -28.
- Longest: **ROM-28, 76,801 samples at 32 kHz, 2.40 s**, looped over its whole length; next ROM-27 (1.38 s at
  22.05 kHz) and ROM-32 (1.20 s at 11.025 kHz).
- Data from P:$150000 to **P:$18FC12, 261,138 words** (`0x3FC12`), 522,276 codes, 11.84 s if counted at 44.1 kHz.
  The flash bank itself ends at $1FF578.

### Room in this engine's voice DSP

`VoiceEngine` gives the voice DSP P, X and Y of `0x200000` words each, external memory shared from `$020000`
(`kSizeP`, `kSizeXY`, `kBridge`); the comment: "P is 2M words, not the DSP's 8M: the MD's map stays below $200000
(samples end at $18fc12), and the interpreter's per-word opcode cache made the full size cost ~300 MB of RAM in MPC"
[V: `source/elektron/md/mdEngine/engine/VoiceEngine.cpp:24-27`]. The hardware's four external areas end at $1FFFFF
(AAR values `0x100539 ... 0x1c0639`) [V: same file, 33; S29].

| From P:$18FC12 to | Words | 12-bit samples | Seconds at 44.1 kHz | At 22.05 kHz |
|---|---|---|---|---|
| $200000, the end of P here | **459,758** | **919,516** | **20.85** | 41.70 |
| $1FFA00, OS 1.63's sample budget end (48-ROM) | 458,222 | 916,444 | 20.78 | 41.56 |

[V: the addresses; M: the arithmetic]. The whole region $150000-$200000 is 720,896 words, 32.69 s at 44.1 kHz [M].

### OS 1.63's own partition (public reverse engineering)

- machinedrum-kit (GPL-3.0, copied into `.scratch/uc-less-engines/machinedrum-kit/06-coldfire-os.md:152-180`):
  sample base `0x150000` and budget `0x15F400`, "halved to get word offsets", in the 48-ROM configuration; RAM slot
  base = sample base + running offset, stride `floor((budget/2 - end_of_ROM_words) / RAM_slot_count)`; the RAM
  recorder writes into staging areas at `0x135206 + 0x80*slot` first [V: S29 local copy].
- Upstream `docs/02-memory-maps.md`: "Stock sample memory extends to about `0x1FFA00` (48-ROM)", the base is
  `0x180000` on 32-ROM, and "the ROM budget (29.7 s on 48-ROM)"; area 3 (`0x1C0000-0x1FFFFF`) is "the slowest memory
  DSP2 has", about 5 wait states measured, against about 1 below it [V: S29]. This repository's DSP emulation charges
  no wait states either [I: the machinedrum-kit notes say so of theirs; not checked in `source/dsp56300`].
- 29.7 s is what 2.5 MiB holds as 16-bit samples at 44.1 kHz (2,621,440 / 2 / 44,100 = 29.72), and the budget less
  that ROM share leaves 128,000 samples, 32,000 per RAM machine: so the manual's 2.5 MB likely counts 16-bit samples,
  as the flash stores them [I]. With the factory bank, OS 1.63 would accept about 17.9 s more of ROM samples at
  44.1 kHz, and give each RAM machine about 114,555 words (5.2 s) [I: the formula above applied].
- In MD Drums the RAM machines do not play [V: `CLAUDE.md`, "RAM machines (RAM-R/P) do not play"], so nothing in this
  engine claims the RAM slots' share [I].

### Where the bank enters the engine today

`EngineT::loadRomBank` writes the directory entries and the data with `VoiceEngine::writeP` after construction
[V: `source/elektron/md/mdEngine/engine/MdEngine.h:65-72`]; `mdDrums::Engine` takes the bank from the flash image
when it is built [V: `source/elektron/md/mdDrums/mdDrumsEngine.cpp:268-281`], and the Hit preview reads its own copy
from the flash for each engine it makes [V: `source/elektron/md/mdDrums/mdDrumsHitPreview.cpp:143`]. `warmUpVoices`
runs after `loadRomBank` and saves and restores the DSP's memory [V: `MdEngine.h:74-80`].

## What this means for the decisions

1. A drop from Explorer or Finder onto MD Drums' window will reach `RmlComponent`; MD Drums needs only an element that
   is a `juceRmlUi::DragTarget`. The routing exists and is exercised by the Patch Manager and Xenia.
2. A drop from Live's browser reaches plug-in windows as an OS file drag on both systems, except for some Pack
   samples that third-party code cannot read.
3. A drop of a clip, a Simpler or a Drum Rack pad is undocumented and reported failing; nothing can be promised
   about it before it is measured in Live 12 on Windows and macOS. If a clip does arrive as a path, it is the source
   file, without the clip's region, warp or gain.
4. As built, the plug-in reads only WAV and AIFF on Windows; FLAC, Ogg or MP3 need the `juce.cmake` flags changed or
   another decoder, and the two systems would differ through Core Audio.
5. Whatever arrives must become mono, 12-bit codes, at a rate the directory expresses as a ratio of 44.1 kHz; the OS
   takes 4-48 kHz and halves higher rates; loops shorter than 151 samples are dropped.
6. Room beyond the factory bank, as the engine stands: 459,758 words, 20.85 s at 44.1 kHz in all, for every user
   sample together (twice that at 22.05 kHz). More means raising `kSizeP` above the hardware's map, which the engine's
   comment ties to memory cost.
7. Slots: the factory bank leaves ROM-33 to ROM-48 empty (directory entries 48-63); ROM-01 to -32 hold the factory
   samples that factory Kits play.
8. The bank enters the engine once, at construction, from the flash image, in two places (the Device's engine and the
   Hit preview's); a user sample has to reach both.

## Sources

- S1. Ableton, Live 10 release notes (10.0.2): https://www.ableton.com/en/release-notes/live-10/ ; Live 11 and 12:
  https://www.ableton.com/en/release-notes/live-11/ , https://www.ableton.com/en/release-notes/live-12/ (read through
  Ableton's documentation search, 2026-10-16)
- S3. Ableton Live 12 manual, Managing Files and Sets (Live Clips, Exporting MIDI Files):
  https://www.ableton.com/en/manual/managing-files-and-sets/
- S4. Ableton Live 12 manual, Live Instrument Reference (Simpler) and Drum Racks (Pad View):
  https://www.ableton.com/en/manual/live-instrument-reference/ , https://www.ableton.com/en/manual/instrument-drum-and-effect-racks/
- S5. Ableton Live 12 manual, Live Concepts (Audio Clips and Samples): https://www.ableton.com/en/manual/live-concepts/
- S6. JUCE forum, "Drag and drop audio file from FL Studio into plugin" (2018):
  https://forum.juce.com/t/drag-and-drop-audio-file-from-fl-studio-into-plugin/28086
- S7. JUCE forum, "Dragging MIDI from DAW to Plugin" (2023): https://forum.juce.com/t/dragging-midi-from-daw-to-plugin/55152
- S8. Ableton forum, 2008 report that files from any folder drop into Morgana inside Live (same thread as S18).
- S9. Microsoft (archived blog), "Why doesn't drag and drop work when my application is running elevated?":
  https://learn.microsoft.com/en-us/archive/blogs/patricka/q-why-doesnt-drag-and-drop-work-when-my-application-is-running-elevated-a-mandatory-integrity-control-and-uipi
- S10. KVR, "Windows 10 Drag and Drop [SOLVED]": https://www.kvraudio.com/forum/viewtopic.php?t=513710
- S11. Xfer Records forum, "Drag and Drop audio issue with Mixcraft Pro Studio 7" (2016):
  https://xferrecords.com/forums/general/drag-and-drop-audio-issue-with-mixcraft-pro-studio-7
- S12. KVR, "Finally, drag and drop with ableton live!!!!" (2009): https://www.kvraudio.com/forum/viewtopic.php?t=250717
- S13. Sunhouse forum, "Drag and drop samples from Ableton not working" (2026):
  https://forum.sunhou.se/t/drag-and-drop-samples-from-ableton-not-working/2493
- S14. Ableton forum, "aif file in abelton" (2022): https://forum.ableton.com/viewtopic.php?p=1805780
- S15. Ableton help, "The license to use certain protected samples is missing" (title read in Ableton's documentation
  search; the page refuses automated reads): https://help.ableton.com/hc/en-us/articles/115000247610
- S16. Elektron, Machinedrum User's Manual, OS 1.63 (EN, 2016-05-23), text extracted locally:
  https://www.elektron.se/wp-content/uploads/2024/09/machinedrum_manual_OS1.63_1.pdf
- S17. Microsoft Learn, `DoDragDrop`: https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-dodragdrop
- S18. Ableton forum, "Is there a fix? Internal drag and drop to 3rd part samplers?" (2008):
  https://forum.ableton.com/viewtopic.php?p=685464
- S19. Cycling '74, `live.drop` reference: https://docs.cycling74.com/reference/live.drop/
- S20. Ableton forum, "Recorded clip audio files are locked, can't copy or drag" (2018, 2021):
  https://forum.ableton.com/viewtopic.php?p=1785925
- S21. KVR, "bug: drag audio clip into Xfer Serum ONLY possible from the browser" (Bitwig, 2015):
  https://www.kvraudio.com/forum/viewtopic.php?p=6085393
- S22. JUCE forum, "In Juce 6 isInterestedInFileDrag not called anymore when in FL Studio" (2020):
  https://forum.juce.com/t/in-juce-6-isinterestedinfiledrag-not-called-anymore-when-in-fl-studio-but-was-before/40538
- S23. JUCE forum, "FileDragAndDropTarget will not work" (2018): https://forum.juce.com/t/filedraganddroptarget-will-not-work/26329
- S24. JUCE forum, "Drag files on OpenGL" (2013): https://forum.juce.com/t/drag-files-on-opengl/10271
- S25. JUCE forum, "Drag and drop doesn't work anymore" (2024): https://forum.juce.com/t/drag-and-drop-doesnt-work-anymore/62765
- S26. JUCE forum, "performExternalDragDropOfFiles not working in Ableton" (2020):
  https://forum.juce.com/t/performexternaldragdropoffiles-not-working-in-ableton/42675
- S27. Elektron, Machinedrum support and downloads (C6 note): https://www.elektron.se/products/machinedrum
- S28. MIDI Sample Dump Standard, dump header (transcription): https://www.lim.di.unimi.it/IEEE/MIDI/SOT4.HTM
- S29. janne808/machinedrum-kit, `docs/02-memory-maps.md`:
  https://github.com/janne808/machinedrum-kit/blob/main/docs/02-memory-maps.md , and its `06-coldfire-os.md`, copied
  in `.scratch/uc-less-engines/machinedrum-kit/`
