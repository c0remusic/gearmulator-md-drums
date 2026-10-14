# MD Drums on Push 2

How MD Drums shows on Push 2's encoders and pads in Live 12 (tickets 02, 11, 27 and 37 of the editor map,
`.scratch/md-drums-editor/`). Push 2 shows a plug-in's Configure list, in unnamed banks of 8, at most 128 parameters;
the plug-in cannot name the banks nor choose one. MD Drums has 608 host parameters, so Live opens it with an empty
panel until a list is configured. MD Drums prepares one of two lists.

## A page per Track: 16 banks, 128 parameters

| Bank | Encoders |
|---|---|
| 1 | T01 Level, FLTF, FLTW, FLTQ, DIST, SRR, DEL, REV |
| 2 | T02, the same eight |
| ... | ... |
| 16 | T16, the same eight |

Each encoder is the Track's own parameter, so what Push records plays back on the Track it was recorded on, whatever
the editor shows then. This is the list for recording automation live from Push (ticket 37).

## The Track shown and the master effects: 8 banks, 64 parameters

| Bank | Encoders |
|---|---|
| 1 | Track, Machine, Level, Mute, Solo, Out, LFO SHP1, LFO SHP2 |
| 2 | SYN1-8 |
| 3 | AMD AMF EQF EQG FLTF FLTW FLTQ SRR (the Machinedrum's EFFECTS page) |
| 4 | DIST VOL PAN DEL REV LFOS LFOD LFOM (its ROUTING page) |
| 5 | Echo: TIME MOD MFRQ FB FLTF FLTW MONO LEV |
| 6 | Gate Box: DVOL PRED DEC DAMP HP LP GATE LEV |
| 7 | EQ: LF LG HF HG PF PG PQ GAIN |
| 8 | Dynamix: ATCK REL TRHD RTIO KNEE HP OUTG MIX |

Banks 1 to 4 are relays: 32 parameters (IDs `9_0_0` to `9_0_31`) that aim at the Track the editor shows. Turning one
sets that Track's own parameter; the Track encoder (1-16) shows another Track, in the editor too, and the relays then
hold its values. Banks 5 to 8 are the master effects themselves. This is the list for playing a whole Track from Push.

## Setting it up in Live

1. Open MD Drums' device and turn on Configure (the device title bar's Configure button).
2. Right-click the editor and choose **Prepare Push list: a page per Track** or **Prepare Push list: Track shown and
   master** (both "Live in Configure mode"): MD Drums moves the parameters of the list in order, each to its own value,
   and Live adds them in that order. Nothing in the sound changes.
3. Turn Configure off, then in the device's context menu choose **Save as Default Configuration**: every new MD Drums
   has the list from then on.

If Live does not take the parameters this way (to be confirmed in Live), add them by hand in Configure mode, in the
order above, from the automation choosers.

## Automation

With the page per Track, Push records each Track's own parameter. A relay's automation means "the Track the editor
shows", whichever it is when the automation plays: to automate a given Track with the relays' list, record from the
editor or draw on that Track's own parameter ("T03 FLTF").

## Pads

Push 2 plays drum pads only when the Live track holds a Drum Rack. Put MD Drums in a Drum Rack, in one chain whose
Receive is "All Notes": the 16 pads at the bottom left then send notes 36 to 51, which play Tracks 1 to 16, with the
pads' velocity. Alone on a MIDI track, MD Drums gets Push's melodic layout, where 36 to 51 are not under one hand.
Whether MD Drums' Outs stay reachable from inside a Drum Rack is to be confirmed in Live.

## Limits

- Push shows names in capitals, about 10 characters: a Track's parameters are named "T03 FLTF" so that the parameter
  fits ("Track 3 FLTF" showed "TRACK 3 FL"). Push reduces a value text that holds digits to the number: "TRX-B2" shows
  "-2", "ROM-01" shows "-1".
- The bank names are Live's ("Bank 1" ...), and the list holds 128 parameters at most per instance.
- A knob turned in the editor, or automation of the Track shown, moves its relay without telling Live, so that Live
  keeps "T01 FLTF" as the parameter last touched and records only that one: Push shows the relay's former value until
  the Track shown changes, and turning that encoder starts from it (ticket 34).
