# MD Drums moves onto the TUS plug-in stack

MD Drums started on 2026-10-06 as a plain `juce::AudioProcessor` with an `AudioProcessorValueTreeState` and JUCE's
generic editor. For its RmlUi editor (the approved v22 design), we move it onto the stack the other Gearmulator
plug-ins share: `pluginLib::Processor`, a `synthLib::Device` over `mdDrums::Engine`, a `pluginLib::Controller` built
from a parameter-description JSON, and a skin whose controls bind to parameters through `param=`. The Usual Suspects'
architecture is this repository's reference (OsTIrus), and the stack brings what a hand-made layer over the APVTS would
have to rebuild: parameter binding, MIDI Learn, skin loading, settings and the editor's scale.

## Considered Options

- **`juceRmlUi` alone over the APVTS**, with a small binding layer of our own. Cheaper to start (the processor stays
  as it is, and `juceRmlUi` needs no Device or Controller), but it forks MD Drums from every other Gearmulator
  plug-in and leaves MIDI Learn, settings and scaling to be written again.

## Consequences

- The JUCE parameter IDs change from `t<n>_<name>` to pluginLib's `page_part_index` (`Parameter::genId`). No Live set
  saved with MD Drums needed keeping on 2026-10-08, so the old IDs are dropped, not migrated; the new IDs are frozen
  from the first push of the port.
- `mdLib` cannot be linked into MD Drums (Musashi's entry points serve one CPU class per executable, and `mdLib` brings
  the Microcontroller), so the Kit dump codec it holds moves to a library of its own.
