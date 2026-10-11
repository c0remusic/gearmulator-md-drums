# Locks travel as notes, on their Track's MIDI channel

MD Drums plays Locks from Live clips, so that a sequencer in Max for Live can print them into clips and Live can play
them back without it. A Lock is a note on its Track's MIDI channel (channel 1-16 for Track 1-16): its pitch names the
parameter (64 to 87 for SYN1 to LFOM, the engine's 24), its velocity carries the value. It goes with the Hit of its
Track that reaches the engine in the same 32-sample block, and sets the parameter at once, without the OS's glide, as
the trigger path already sets a pending Machine. The Track's next Hit without a Lock on that parameter plays the Kit's
value again, at once too. A Lock with no Hit of its Track in its block is dropped. The Kit never changes. In Live, each
Track gets a MIDI track whose External Instrument sends to MD Drums on the Track's channel and returns the Track's Out.

Notes, because a Lock belongs to one Hit: a clip keeps notes as events, at the sample, LOM writes them exactly
(`Clip.add_new_notes`), and quantize, groove and copy move a Lock's note with its Hit's when both share a position. A
clip also gives every Lock's note Live's chance and velocity deviation: conditional and random Locks, with no sequencer
running.

## Considered Options

- **MIDI CC, the Machinedrum's own map.** A clip keeps CC as an envelope and Live sends only its changes: a second Hit
  locked to the same value sends nothing and plays the Kit. Only the 24 parameters have CCs, and CC reaches the engine
  through `HostModel::setParam`, which glides over the OS's smoothing, run once a tick (11 blocks of 32).
- **Host parameter automation.** JUCE's VST3 wrapper keeps the last point of each buffer and the Controller sends it
  at offset 0: a Lock lands up to a buffer early, two Locks in one buffer keep only the last, and it glides too. Live
  exposes only configured parameters to LOM, not 576.
- **Max for Live setting parameters through LOM.** On Live's main thread, with tens of milliseconds of jitter.
- **MPE note expressions.** Attached to the Hit itself, but three or four per note, and writing them through LOM is
  unverified.

## Consequences

- Amends ADR 0002: the Device takes Hits (notes 36-51, any channel, as before) and Locks (notes 64-87, on the Track's
  channel) from the host, still no CC or SysEx.
- A Live clip carries no MIDI channel, so Locks for Track n come from a Live track that sends on channel n. One MIDI
  track on one channel locks one Track.
- Velocity 0 is a note-off, so velocity 1-127 stretches onto values 0-127, rounded: 1-63 give 0-62, 64-127 give
  themselves (`messages::lockValue`). Both ends and the centre can be locked, 63 cannot. Every clip printed from the
  first push on depends on this scale.
- `HostModel` models Locks (its header listed them as not modelled): a local change to mdEngine, listed in its README.
- Locks are invisible to the host parameters and the editor, whose knobs show the Kit.
- A Hit whose note has a chance of not playing leaves its Locks without a Hit, and so dropped.

## Amendment (2026-10-11): on channel 1, a Lock goes with the Hit just before it

Live does not keep the MIDI channel a Max for Live device puts on its notes: md-drums-seq measured channel 1 for notes
it sent on channels 3 and 7, right after the device and on another track taking its MIDI. A sequencer playing in Live
could then lock Track 1 only.

A Lock on channel 1 now goes with the Hit note received just before it at the same sample, whatever that Hit's Track;
with none, it is Track 1's, as before (`Device::apply`). Channels 2-16 still name their Track, before or after its Hit.
So a sender that cannot keep channels sends each Hit followed by its Locks (Hit A, A's Locks, Hit B, B's Locks), and
one that keeps them, such as a clip through an External Instrument on channel n, works as before. A lone Track 1 Hit
plays its Locks in either order, as before.

- The order of two notes at one sample now matters on channel 1. MD Drums keeps it (`synthLib::Plugin` inserts an
  event after those at the same offset, JUCE's MidiBuffer too). Whether Live plays a clip's simultaneous notes in a set
  order is not measured: until it is, a clip on channel 1 carrying another Track's Locks may bind them to the wrong
  Hit, and a clip on its Track's channel is the safe way.
- `mdDrumsDeviceTest` and `mdDrumsVst3HostTest` check both rules, the second with md-drums-seq's notes.
