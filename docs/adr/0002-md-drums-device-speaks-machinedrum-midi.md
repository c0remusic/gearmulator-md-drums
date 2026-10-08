# MD Drums' Device speaks the Machinedrum's MIDI protocol

MD Drums runs the Machinedrum's engines without its OS, so nothing inside it needs MIDI; yet its `synthLib::Device`
takes everything as the Machinedrum's own messages: notes, CCs for the 24 Track parameters, level and mute, `$5B` for
the Machine, `$5D-$60` for the Master effects, `$62` for the LFO and `$52` Kit dumps. What the Machinedrum has no
message for (Solo, Out 01-16, the host's tempo) travels as private SysEx under the non-commercial header `F0 7D`. We
chose this so that one format serves everywhere: the plug-in state, the Bank file (64 `$52` dumps), Kit import and
export, and the Controller's rebuild of the host parameters all read and write the same Kit dump, and the Device can
be tested with bytes alone. Every message but a Kit dump fits `SMidiEvent`'s 64 inline SysEx bytes, so host
automation reaches the engine on the audio thread without allocating.

## Considered Options

- **A lock-free mirror of the host parameters**, read by the Device each block, with MIDI for notes only. Less code
  per parameter, but a second way into the Device outside the TUS contract, a Device no longer testable by its MIDI
  input, and the Kit dump codec still needed for the state, the Bank and import.
- **One private SysEx message per parameter** (`page, part, index, value`), as the JE-8086 does for values its
  hardware has no message for. Simpler to encode, but two formats side by side, since Kits still come and go as
  Machinedrum dumps.

## Consequences

- The parameter JSON keeps `mdautomation`'s pages 0-4 (SYN1-8, effects, routing, level, mute), so that a parameter's
  `(page, part, index)` is its CC; MD Drums' own parameters take pages 5 to 8.
- The Machinedrum's codecs leave `mdLib` for `mdProtocol`, a library without an emulator that both `mdLib` and MD
  Drums link.
- The Device takes only notes from the host; CCs and SysEx count only when the Controller sends them.
