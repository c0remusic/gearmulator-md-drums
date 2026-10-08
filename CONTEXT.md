# MD Drums

The Elektron Machinedrum's sixteen synthesis tracks as a plug-in, played and automated from a host, edited in one
window like Overbridge.

## Language

### Plug-ins

**MD Drums**:
The plug-in this repository builds: the Machinedrum's engines run without its OS.
_Avoid_: the plug-in (when Gearmulator MD could be meant), mdDrumsPlugin

**Gearmulator MD**:
The full-machine plug-in inherited from md-mm, which runs the whole firmware; not what this repository ships.
_Avoid_: MD plug-in, mdJucePlugin

### Sound

**Track**:
One of the sixteen voices; it plays one Machine.
_Avoid_: voice, part, channel

**Machine**:
The synthesis engine a Track plays, such as EFM-BD or TRX-SD.
_Avoid_: model, instrument

**Hit**:
One trigger of a Track and the sound it makes.
_Avoid_: note, trig (the firmware sequencer's word)

**Lock**:
A value of one Track parameter that a single Hit plays in place of the Kit's; the Track's next Hit without one plays the
Kit's value again.
_Avoid_: p-lock, parameter lock, automation

**Link**:
A Track's Hit that also hits another Track, at the same velocity; it goes one step only, so the other Track's own
Link does not fire.
_Avoid_: trig group, trig pos

**Choke**:
A Track's Hit that silences another Track at once, until that Track's next Hit.
_Avoid_: mute group, choke group (Live's choke groups work both ways; a Choke goes one way)

**Master effects**:
The four effects after the Tracks: Echo (the Rhythm Echo), Reverb (the Gate Box), EQ and Dynamix.
_Avoid_: FX, global effects

**Send**:
How much of a Track goes to the Echo (DEL) or the Reverb (REV).

### Outputs

**Main**:
The stereo output that carries the mixed Tracks and the Master effects.
_Avoid_: master out, mix bus

**Out**:
A Track's own mono output (Out 01 to Out 16); a Track on its Out leaves Main but still feeds its Sends.
_Avoid_: separate output, Track bus

**Mute**:
A Track switched off: it drops its Hits, so it plays on neither Main, its Out nor its Sends and fires no Link or Choke,
while what it already sent to the Echo and Reverb rings out.

**Solo**:
A Track singled out: while any Track has its Solo on, every Track without one is silent as if muted. Solos add up, and a
muted Track stays silent with its Solo on.

### Kits

**Kit**:
The whole sound of the sixteen Tracks: their Machines, parameters, levels, LFOs, Links and Chokes, and the Master
effects; not the mutes, solos or Outs.
_Avoid_: preset, patch

**Bank**:
The 64 Kits kept outside any host set and shared by all of them.
_Avoid_: library, BIBLIO

**Slot**:
One of the Bank's 64 places, which holds a Kit or nothing.
