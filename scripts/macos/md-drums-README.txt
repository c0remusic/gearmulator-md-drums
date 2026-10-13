MD Drums (preview) - macOS universal (Apple Silicon and Intel), VST3 and AU
==========================================================================

The Elektron Machinedrum's synthesis engines as a VST3 and AU instrument,
edited like Overbridge and sequenced by your DAW. The machines' own code from
OS 1.63 runs on emulated DSPs (the voice DSP and the mixer DSP's master
effects), without the Machinedrum's OS or sequencer.

THIS MAC BUILD IS UNTESTED. It was built and signed ad hoc by GitHub's macOS
runner, which has no Machinedrum flash image: nobody has heard it play on a
Mac yet. Reports are welcome, working or not:
https://github.com/c0remusic/gearmulator-md-drums/issues

Source: https://github.com/c0remusic/gearmulator-md-drums
Not affiliated with Elektron or with The Usual Suspects.


Requirements
------------

- macOS 11 or later, and a VST3 or AU host.
- Your own Machinedrum UW flash image with OS 1.63, the 8 MB file named
  elektron_sps1-1uw_os1.63.bin (the same file Gearmulator MD uses). It is
  Elektron's copyright and is not included: this is for people who own the
  hardware. Please do not ask for it.


Install
-------

The installer package (MD-Drums-...-macOS-universal.pkg, beside this zip on
the release page) puts both into /Library/Audio/Plug-Ins/. It is not notarized:
macOS refuses the first time; then open System Settings > Privacy & Security
and choose "Open Anyway", or right-click the package and choose Open.

Or by hand, from this zip:

1. Copy "MD Drums.vst3" to ~/Library/Audio/Plug-Ins/VST3/ and, for AU hosts,
   "MD Drums.component" to ~/Library/Audio/Plug-Ins/Components/
2. The bundles are signed ad hoc, not notarized: macOS blocks them until you
   remove the quarantine flag, in Terminal:
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MD Drums.vst3"
     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MD Drums.component"

Then, either way:

3. Put elektron_sps1-1uw_os1.63.bin in ~/Documents/c0remusic/MD Drums/roms/
   (create the folder). MD Drums also looks in
   ~/Documents/Gearmulator Preview/Machinedrum/
4. Rescan plug-ins in your host and load "MD Drums" (c0remusic).


Playing
-------

- Notes 36-51 (C1 to D#2 in Live) on any channel play Tracks 1-16; velocity
  scales the level.
- Locks: notes 64-87 set SYN1 ... LFOM for one Hit, the velocity carrying the
  value: on the Track's channel, or on channel 1 right after the Hit note.
- 608 host parameters. Live shows none until you add them (Configure mode).

See the Windows build's README and docs/md-drums-push2.md in the source for the
rest: the editor, the Kits, the Outs, Push 2.


Licence
-------

The plug-in combines Gearmulator (GNU GPL v3) and Machinemodule's engine
(sd88me, GNU AGPL v3), and is distributed under the GNU AGPL v3. See NOTICE.md,
LICENSE.md (GPL v3), LICENSE-AGPL.txt (AGPL v3) and Inter-OFL.txt (the Inter
font, SIL Open Font License 1.1).

"Elektron" and "Machinedrum" are trademarks of Elektron Music Machines MAQ AB,
used only to name the hardware.
