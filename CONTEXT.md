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

### Kits

**Kit**:
The whole sound of the sixteen Tracks: their Machines, parameters, levels and LFOs, and the Master effects; not the
mutes, solos or Outs.
_Avoid_: preset, patch

**Bank**:
The 64 Kits kept outside any host set and shared by all of them.
_Avoid_: library, BIBLIO

**Slot**:
One of the Bank's 64 places, which holds a Kit or nothing.
