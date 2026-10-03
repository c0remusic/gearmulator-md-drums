# Generates the editor part of skins/mdDefault/mdDefault.rml (md) and skins/mmSfx60/mmSfx60.rml (mm).
# Compact layout: the window is 1100 x 606 dp, a 36 dp bar on top of either the front panel (570 dp)
# or the editor (a 32 dp track strip and a 538 dp page). Every SON, MASTER and MIX view fits the page
# without scrolling. When the window is made taller, the front panel and the editor are stacked.
# Grid: 16 dp margins, 12 columns of 78 dp, 12 dp gutters (16 + 12*78 + 11*12 + 16 = 1100 dp).
import sys
MODEL = sys.argv[1]
MD = MODEL == "md"
COL, GUT, M = 78, 12, 16
def colx(c): return M + c * (COL + GUT)       # absolute x of column c
def bx(c): return c * (COL + GUT)             # x of column c relative to a block starting at column 0
def span(n): return n * COL + (n - 1) * GUT
KNOB, ROW, TOP = 46, 84, 32
PAGE_H = 538

out = []
w = out.append
def ind(n): return "\t" * n

# ---------- controls ----------

def knob(d, x, y, param, name, off=False, cw=COL):
    cls = "jucePos juceRotary elektronKnob elektronMasterKnob mdEdKnob" + (" mdEdOff" if off else "")
    p = f' param="{param}"' if param and not off else ""
    cid = f' id="mdEdCtl_{param}"' if p else ""
    vid = f' id="mdEdVal_{param}"' if p else ""
    nid = f' id="mdEdName_{param}"' if p else ""
    w(f'{ind(d)}<knob{cid} class="{cls}"{p} style="left: {x + (cw - KNOB) // 2}dp; top: {y}dp;"/>')
    value = f"{{{{{param}_text}}}}" if param and not off else "—"
    w(f'{ind(d)}<div{vid} class="jucePos juceLabel mdEdValue{" mdEdOff" if off else ""}" style="left: {x}dp; top: {y + KNOB}dp; width: {cw}dp;">{value}</div>')
    w(f'{ind(d)}<div{nid} class="jucePos juceLabel mdEdName{" mdEdOff" if off else ""}" style="left: {x}dp; top: {y + KNOB + 14}dp; width: {cw}dp;">{name}</div>')

def fader(d, x, y, param, name, off=False, fh=KNOB, cw=COL):
    # vertical fader in a knob cell; orientation="vertical" puts the maximum at the top
    cls = "jucePos mdEdFader" + (" mdEdOff" if off else "")
    p = f' param="{param}"' if param and not off else ""
    # an unbound fader rests at the bottom (value 100 of the default 0-100 range)
    rest = "" if p else ' value="100"'
    cid = f' id="mdEdCtl_{param}"' if p else ""
    vid = f' id="mdEdVal_{param}"' if p else ""
    nid = f' id="mdEdName_{param}"' if p else ""
    tall = f" height: {fh}dp;" if fh != KNOB else ""
    w(f'{ind(d)}<input{cid} type="range" orientation="vertical" class="{cls}"{p}{rest} style="left: {x + (cw - 12) // 2}dp; top: {y}dp;{tall}"/>')
    value = f"{{{{{param}_text}}}}" if param and not off else "—"
    w(f'{ind(d)}<div{vid} class="jucePos juceLabel mdEdValue{" mdEdOff" if off else ""}" style="left: {x}dp; top: {y + fh}dp; width: {cw}dp;">{value}</div>')
    w(f'{ind(d)}<div{nid} class="jucePos juceLabel mdEdName{" mdEdOff" if off else ""}" style="left: {x}dp; top: {y + fh + 14}dp; width: {cw}dp;">{name}</div>')

def control(d, x, y, item):
    kind, param, name = item
    (fader if kind == "f" else knob)(d, x, y, param, name, kind == "x")

def selector(d, x, y, labels, widths, ids=None):
    # segmented selector; without ids it is drawn inactive (nothing behind it yet)
    for i, (label, sw) in enumerate(zip(labels, widths)):
        sid = f' id="{ids[i]}"' if ids else ""
        cls = "mdEdSeg mdEdSegChoice" if ids else "mdEdSeg mdEdOff"
        w(f'{ind(d)}<div{sid} class="jucePos {cls}" style="left: {x}dp; top: {y}dp; width: {sw}dp;">{label}</div>')
        x += sw

def block_open(d, bid, title, sub, c0, ncols, y, h, model=None):
    m = f' data-model="{model}"' if model else ""
    w(f'{ind(d)}<div id="{bid}" class="jucePos mdEdBlock"{m} style="left: {colx(c0)}dp; top: {y}dp; width: {span(ncols)}dp; height: {h}dp;">')
    w(f'{ind(d + 1)}<div class="jucePos juceLabel mdEdTitle" style="left: 16dp; top: 4dp; width: {span(ncols) - 32}dp;">{title}</div>')
    if sub:
        w(f'{ind(d + 1)}<div class="jucePos juceLabel mdEdSub mdEdRight" style="left: 16dp; top: 4dp; width: {span(ncols) - 32}dp;">{sub}</div>')

def block_close(d):
    w(f'{ind(d)}</div>')

def rows_height(rows):
    return TOP + rows * ROW + 8

def controls_block(d, bid, title, sub, c0, ncols, y, items, model="partCurrent", height=None):
    rows = (len(items) + ncols - 1) // ncols
    h = height if height else rows_height(rows)
    block_open(d, bid, title, sub, c0, ncols, y, h, model)
    for i, item in enumerate(items):
        control(d + 1, bx(i % ncols), TOP + (i // ncols) * ROW, item)
    return h

# ---------- blocks shared by the SON pages ----------

ROW_A = TOP + 24 + 8

def steps_block(d, y, ncols):
    # StepGrid (mdStepGrid.cpp) fills the steps, 32 at a time (33 to 64 for a pattern over 32 steps),
    # and the line on the right from the current pattern.
    width = span(ncols)
    pitch = (width - 32 + 2) // 32
    block_open(d, "mdEdSteps", "PAS", "", 0, ncols, y, ROW_A)
    selector(d + 1, 52, 4, ["1–32", "33–64"], [48, 48], ["mdEdStepPage0", "mdEdStepPage1"])
    info_x = 52 + 2 * 48 + 12
    w(f'{ind(d + 1)}<div id="mdEdStepsInfo" class="jucePos juceLabel mdEdSub mdEdRight" style="left: {info_x}dp; top: 4dp; width: {width - info_x - 16 - 70}dp;">pattern : en attente</div>')
    w(f'{ind(d + 1)}<button id="mdEdStepsRefresh" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {width - 16 - 62}dp; top: 4dp; width: 62dp;">RELIRE</button>')
    for s in range(32):
        beat = " mdEdStepBeat" if s % 4 == 0 else ""
        w(f'{ind(d + 1)}<div id="mdEdStep{s}" class="jucePos mdEdStep{beat}" style="left: {16 + pitch * s}dp; top: {TOP}dp; width: {pitch - 2}dp;">{s + 1}</div>')
    block_close(d)

def machine_block(d, y, c0, ncols):
    width = span(ncols)
    block_open(d, "mdEdMachine", "MACHINE", "", c0, ncols, y, ROW_A)
    # Filled by MachinePicker (mdMachinePicker.cpp): family tag, machine name, synthesis type.
    w(f'{ind(d + 1)}<button id="mdEdMachineChange" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {width - 16 - 150}dp; top: 4dp; width: 150dp;">CHANGER DE MACHINE</button>')
    w(f'{ind(d + 1)}<div id="mdEdMachineFamily" class="jucePos mdEdPicto" style="left: 16dp; top: {TOP}dp;">—</div>')
    w(f'{ind(d + 1)}<div id="mdEdMachineName" class="jucePos juceLabel mdEdMachineName" style="left: 64dp; top: {TOP}dp; width: 100dp;">—</div>')
    w(f'{ind(d + 1)}<div id="mdEdMachineInfo" class="jucePos juceLabel mdEdSub" style="left: 166dp; top: {TOP + 6}dp; width: {width - 166 - 16}dp;">machine inconnue : en attente du kit</div>')
    block_close(d)

def machine_picker(d, y):
    # Opens under MACHINE, over the blocks below; MachinePicker fills the families and machines.
    w(f'{ind(d)}<div id="mdEdMachinePicker" class="jucePos mdEdPicker" style="left: {colx(0)}dp; top: {y}dp; width: {span(12)}dp; display: none;">')
    w(f'{ind(d + 1)}<div id="mdEdPickerFamilies" class="mdEdPickerRow"/>')
    w(f'{ind(d + 1)}<div id="mdEdPickerMachines" class="mdEdPickerRow"/>')
    w(f'{ind(d + 1)}<div id="mdEdPickerInfo" class="mdEdPickerInfo"/>')
    w(f'{ind(d)}</div>')

CURVE_H = TOP + 88 + 8

def curves_row(d, y, blocks):
    # A row of curve blocks under the SON view, shown by CurveView (mdCurveView.cpp) only when the
    # page has room for it; each block's area gets a canvas drawn from the edited track's parameters.
    # data-unread names those parameters: the area is greyed while one is unknown (mdUnreadValues.cpp).
    w(f'{ind(d)}<div id="mdEdCurves" class="mdEdCurves" style="display: none;">')
    for bid, title, sub, c0, ncols, params in blocks:
        block_open(d + 1, f"mdEdCurves{bid}", title, sub, c0, ncols, y, CURVE_H)
        w(f'{ind(d + 2)}<div id="mdEdCurve{bid}" class="jucePos mdEdCurveArea" data-unread="{params}" style="left: 16dp; top: {TOP}dp; width: {span(ncols) - 32}dp; height: 88dp;"/>')
        block_close(d + 1)
    w(f'{ind(d)}</div>')
    return CURVE_H

# ---------- MD ----------

def md_sound(d):
    # the SON content switches between the track view (tabs 1-16) and MASTER (tab 17) on the mdTrack tab group's page
    w(f'{ind(d)}<div class="mdEdContent" data-model="tabgroup_mdTrack">')
    d += 1
    w(f'{ind(d)}<div id="mdEdTrackView" class="mdEdView" data-if="page != 16" roomyheight="{{ROOMY_H}}" style="height: {{TRACK_H}}dp;">')
    # Row A: PAS (8) | MACHINE (4)
    y = 12
    steps_block(d + 1, y, 8)
    machine_block(d + 1, y, 8, 4)
    picker_y = y + ROW_A + 4
    # Row B: SOURCE (4) | FILTRE (3) | COULEUR (2) | MIX (3), two rows of knobs each
    y += ROW_A + GUT
    hb = rows_height(2)
    source = [("k", f"MachineParameter{i}", f"PARAM {i}") for i in range(1, 9)]
    controls_block(d + 1, "mdEdSource", "SOURCE", "synthèse de la machine", 0, 4, y, source)
    block_close(d + 1)
    filt = [("k", "FilterBase", "BASE"), ("k", "FilterWidth", "WIDTH"), ("k", "FilterQ", "Q"), ("k", "EQFrequency", "EQF"), ("k", "EQGain", "EQG")]
    controls_block(d + 1, "mdEdFilter", "FILTRE", "passe-bande + EQ", 4, 3, y, filt, height=hb)
    block_close(d + 1)
    colour = [("k", "Distortion", "DIST"), ("k", "SampleRateReduction", "SRR"), ("k", "AMDepth", "AMD"), ("k", "AMRate", "AMF")]
    controls_block(d + 1, "mdEdColour", "COULEUR", "", 7, 2, y, colour, height=hb)
    block_close(d + 1)
    # MIX: three tall faders and the pan in four 56 dp cells
    cw = (span(3) - 16) // 4
    fh = hb - TOP - 28 - 8
    block_open(d + 1, "mdEdMix", "MIX", "", 9, 3, y, hb, "partCurrent")
    for i, (param, name) in enumerate([("Volume", "VOL"), ("DelaySend", "DEL"), ("ReverbSend", "REV")]):
        fader(d + 2, 8 + i * cw, TOP, param, name, fh=fh, cw=cw)
    knob(d + 2, 8 + 3 * cw, TOP, "Pan", "PAN", cw=cw)
    block_close(d + 1)
    # Row C: MODULATION, the three LFO controls and what they do
    y += hb + GUT
    hc = rows_height(1)
    block_open(d + 1, "mdEdLfo", "MODULATION", "LFO de la piste", 0, 12, y, hc, "partCurrent")
    # The LFO page's order on the machine: SPEED, DEPTH, SHMIX (the mix of shape 1 and shape 2)
    for i, item in enumerate([("k", "LFOSpeed", "SPEED"), ("k", "LFOAmount", "DEPTH"), ("k", "LFOShape", "SHMIX")]):
        control(d + 2, bx(i), TOP, item)
    # LfoView (mdLfoView.cpp) fills what the Kit holds: the destination (a menu), the update, the two shapes
    # (a menu each, an icon drawn by JUCE) and the wave they make with SHMIX
    lx = bx(3) + 16
    w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdName mdEdLeft" style="left: {lx}dp; top: {TOP + 4}dp; width: 90dp;">DESTINATION</div>')
    w(f'{ind(d + 2)}<button id="mdEdLfoDest" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {lx + 94}dp; top: {TOP}dp; width: 220dp;">—</button>')
    w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdName mdEdLeft" style="left: {lx + 330}dp; top: {TOP + 4}dp; width: 90dp;">MISE À JOUR</div>')
    selector(d + 2, lx + 424, TOP, ["FREE", "TRIG", "HOLD"], [56, 56, 56], ["mdEdLfoUpdate0", "mdEdLfoUpdate1", "mdEdLfoUpdate2"])
    for n in range(2):
        sx = lx + n * 214
        w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdName mdEdLeft" style="left: {sx}dp; top: {TOP + 36}dp; width: 90dp;">FORME {n + 1}</div>')
        w(f'{ind(d + 2)}<div id="mdEdLfoShape{n + 1}" class="jucePos mdEdLfoShape" style="left: {sx + 94}dp; top: {TOP + 32}dp;">')
        w(f'{ind(d + 3)}<div id="mdEdLfoShape{n + 1}Icon" class="jucePos mdEdLfoIcon" style="left: 6dp; top: 4dp;"/>')
        w(f'{ind(d + 3)}<div id="mdEdLfoShape{n + 1}Name" class="jucePos juceLabel mdEdLfoShapeName" style="left: 40dp; top: 0dp;">—</div>')
        w(f'{ind(d + 2)}</div>')
    w(f'{ind(d + 2)}<div id="mdEdLfoWave" class="jucePos mdEdCurveArea" style="left: {lx + 610}dp; top: {TOP}dp; width: {span(12) - lx - 610 - 16}dp; height: 56dp;"/>')
    w(f'{ind(d + 2)}<div id="mdEdLfoInfo" class="jucePos juceLabel mdEdNote" style="left: {lx}dp; top: {TOP + 66}dp; width: {span(12) - lx - 16}dp;">LFO inconnu : en attente du kit</div>')
    block_close(d + 1)
    track_h = y + hc + 12
    roomy_h = track_h + curves_row(d + 1, y + hc + GUT, [
        ("Filter", "FILTRE", "BASE, WIDTH, Q · allure, pas une mesure", 0, 6, "FilterBase FilterWidth FilterQ"),
        ("Eq", "EQ", "EQF, EQG · allure, pas une mesure", 6, 6, "EQFrequency EQGain")]) + GUT
    machine_picker(d + 1, picker_y)
    w(f'{ind(d)}</div>')

    # MASTER: the MD master effects of the Kit (board 5), 8 parameters each. They are not host parameters:
    # MasterEffectsView (mdMasterEffectsView.cpp) sets the knobs from the controller and sends their changes.
    w(f'{ind(d)}<div id="mdEdMasterView" class="mdEdView" data-if="page == 16" style="height: {{MASTER_H}}dp;">')
    # In the order of their SysEx ids ($5D to $60); parameter names from MCL (MDParams.h)
    fx = [("mdEdEcho", "RHYTHM ECHO", "envois : DEL des pistes", ["TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"]),
          ("mdEdReverb", "GATE BOX REVERB", "envois : REV des pistes", ["DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"]),
          ("mdEdEq", "EQ", "", ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"]),
          ("mdEdDynamix", "DYNAMIX", "", ["ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"])]
    h = rows_height(2)
    cell = span(6) // 4
    for i, (bid, title, sub, names) in enumerate(fx):
        yb = 12 + (i // 2) * (h + GUT)
        block_open(d + 1, bid, title, sub, 6 * (i % 2), 6, yb, h)
        for j, n in enumerate(names):
            x, y = (j % 4) * cell, TOP + (j // 4) * ROW
            w(f'{ind(d + 2)}<knob id="mdEdFx{i}_{j}" class="jucePos juceRotary elektronKnob elektronMasterKnob mdEdKnob mdEdUnread" min="0" max="127" value="0" default="-1" style="left: {x + (cell - KNOB) // 2}dp; top: {y}dp;"/>')
            w(f'{ind(d + 2)}<div id="mdEdFxVal{i}_{j}" class="jucePos juceLabel mdEdValue mdEdUnread" style="left: {x}dp; top: {y + KNOB}dp; width: {cell}dp;">—</div>')
            w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdName" style="left: {x}dp; top: {y + KNOB + 14}dp; width: {cell}dp;">{n}</div>')
        block_close(d + 1)
    w(f'{ind(d + 1)}<div id="mdEdMasterInfo" class="jucePos juceLabel mdEdNote" style="left: 32dp; top: {12 + 2 * (h + GUT)}dp; width: 1036dp;">Effets master du kit, envoyés au firmware à chaque mouvement ; l\'hôte ne les automatise pas (hors paramètres du plug-in). Seules les pistes en MAIN y passent.</div>')
    master_h = 12 + 2 * (h + GUT) + 28
    w(f'{ind(d)}</div>')
    w(f'{ind(d - 1)}</div>')
    return track_h, master_h, roomy_h

# ---------- MM ----------

def mm_sound(d):
    w(f'{ind(d)}<div class="mdEdContent" data-model="tabgroup_mdTrack">')
    d += 1
    w(f'{ind(d)}<div id="mdEdTrackView" class="mdEdView" roomyheight="{{ROOMY_H}}" style="height: {{TRACK_H}}dp;">')
    y = 12
    machine_block(d + 1, y, 0, 12)
    picker_y = y + ROW_A + 4
    y += ROW_A + GUT
    h = rows_height(2)
    osc = [("k", f"Synthesis{c}", f"SYN {c}") for c in "ABCDEFGH"]
    controls_block(d + 1, "mdEdOsc", "OSCILLATEUR", "synthèse de la machine", 0, 4, y, osc)
    block_close(d + 1)
    amp = [("k", "AmpAttack", "ATK"), ("k", "AmpHold", "HOLD"), ("k", "AmpDecay", "DEC"), ("k", "AmpRelease", "REL"),
           ("k", "AmpDistortion", "DIST"), ("f", "AmpVolume", "VOL"), ("k", "AmpPan", "PAN"), ("k", "AmpPortamento", "PORT")]
    controls_block(d + 1, "mdEdAmp", "AMPLI", "ADHR", 4, 4, y, amp)
    block_close(d + 1)
    filt = [("k", "FilterBase", "BASE"), ("k", "FilterWidth", "WDTH"), ("k", "FilterHighpassQ", "HPQ"), ("k", "FilterLowpassQ", "LPQ"),
            ("k", "FilterAttack", "ATK"), ("k", "FilterDecay", "DEC"), ("k", "FilterBaseOffset", "BOFS"), ("k", "FilterWidthOffset", "WOFS")]
    controls_block(d + 1, "mdEdFilter", "FILTRE", "", 8, 4, y, filt)
    block_close(d + 1)
    y += h + GUT
    eff = [("k", "EffectsDelayTime", "DTIM"), ("f", "EffectsDelaySend", "DSND"), ("k", "EffectsDelayFeedback", "DFB"), ("k", "EffectsSampleRate", "SRR"),
           ("k", "EffectsEqGain", "EQG"), ("k", "EffectsEqFrequency", "EQF"), ("k", "EffectsDelayBase", "DBAS"), ("k", "EffectsDelayWidth", "DWID")]
    controls_block(d + 1, "mdEdEffects", "EFFETS", "EQ, lo-fi, delay", 0, 4, y, eff)
    block_close(d + 1)
    # MODULATION, 8 columns: LFO 1-3 tabs in the title row, the LFO's summary on the left, its 8 controls on the right
    block_open(d + 1, "mdEdLfo", "MODULATION", "", 4, 8, y, h, "partCurrent")
    rx = bx(4)
    for n in range(3):
        w(f'{ind(d + 2)}<button id="mmLfoTab{n}" class="jucePos juceButton mdEdLfoTab" isToggle="1" tabgroup="mmLfo" tabbutton="{n}" style="left: {rx + n * 84}dp; top: 4dp;">LFO {n + 1}</button>')
    for n in range(3):
        L = f"Lfo{n + 1}"
        w(f'{ind(d + 2)}<div id="mmLfoPage{n}" class="jucePos mdEdLfoPage" tabgroup="mmLfo" tabpage="{n}" style="left: 0dp; top: {TOP}dp; width: {span(8)}dp; height: {h - TOP - 8}dp;">')
        w(f'{ind(d + 3)}<div class="jucePos juceLabel mdEdNote" data-unread="{L}Page {L}Destination {L}Waveform {L}Trigger" style="left: 16dp; top: 8dp; width: {span(4) - 32}dp;">LFO {n + 1} : page {{{{{L}Page_text}}}}, destination {{{{{L}Destination_text}}}}, forme {{{{{L}Waveform_text}}}}, déclenchement {{{{{L}Trigger_text}}}}</div>')
        items = [("k", f"{L}Page", "PAGE"), ("k", f"{L}Destination", "DEST"), ("k", f"{L}Trigger", "TRIG"), ("k", f"{L}Waveform", "WAVE"),
                 ("k", f"{L}Multiplier", "MULT"), ("k", f"{L}Speed", "SPD"), ("k", f"{L}Interlace", "INTL"), ("k", f"{L}Depth", "DEP")]
        for i, item in enumerate(items):
            control(d + 3, rx + bx(i % 4), (i // 4) * ROW, item)
        w(f'{ind(d + 2)}</div>')
    block_close(d + 1)
    track_h = y + h + 12
    roomy_h = track_h + curves_row(d + 1, y + h + GUT, [
        ("Amp", "AMPLI", "ATK, HOLD, DEC · proportions", 0, 4, "AmpAttack AmpHold AmpDecay"),
        ("Filter", "FILTRE", "BASE, WDTH, HPQ, LPQ · allure", 4, 4, "FilterBase FilterWidth FilterHighpassQ FilterLowpassQ"),
        ("Eq", "EQ", "EQF, EQG · allure", 8, 4, "EffectsEqFrequency EffectsEqGain")]) + GUT
    machine_picker(d + 1, picker_y)
    w(f'{ind(d)}</div>')
    w(f'{ind(d - 1)}</div>')
    return track_h, None, roomy_h

# ---------- MIX (board 7) ----------

def mix_page(d, tracks):
    w(f'{ind(d)}<div class="mdEdContent" style="height: {{MIX_H}}dp;">')
    d += 1
    heads_top, row_h = TOP, 26
    rows_top = heads_top + 16
    note_h = 24
    # NIVEAUX and SORTIES share one height; the output panels split what SORTIES has
    lh = max(rows_top + tracks * row_h + 8 + note_h, TOP + 3 * 96 + 2 * 8 + 8)
    block_open(d, "mdEdLevels", "NIVEAUX", "une colonne par champ", 0, 8, 12, lh)
    if MD:
        cols = [("PISTE", 0), ("NIVEAU", 1), ("VOL", 2), ("PAN", 3), ("DEL", 4), ("REV", 5), ("SORTIE", 6)]
        fields = [("Volume", 2), ("Pan", 3), ("DelaySend", 4), ("ReverbSend", 5)]
    else:
        cols = [("PISTE", 0), ("NIVEAU", 1), ("VOL", 2), ("PAN", 3), ("DEL", 4), ("BUS", 5)]
        fields = [("AmpVolume", 2), ("AmpPan", 3), ("EffectsDelaySend", 4)]
    for label, c in cols:
        w(f'{ind(d + 1)}<div class="jucePos juceLabel mdEdName mdEdLeft" style="left: {bx(c) + 16}dp; top: {heads_top}dp; width: 70dp;">{label}</div>')
    for t in range(tracks):
        y = rows_top + t * row_h
        w(f'{ind(d + 1)}<div id="mdEdLevelRow{t}" class="jucePos mdEdRow" data-model="part{t}" style="left: 0dp; top: {y}dp; width: {span(8)}dp; height: {row_h}dp;">')
        # LED on = the track plays; clicking it mutes the track (Mute is 1 when muted)
        w(f'{ind(d + 2)}<button class="jucePos juceButton mdEdLed" isToggle="1" param="Mute" valueOn="0" valueOff="1" style="left: 16dp; top: 7dp;"/>')
        w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdRowName" data-class-mdEdMuted="Mute_value == 1" style="left: 34dp; top: 0dp; width: 60dp;">{t + 1:02d}</div>')
        w(f'{ind(d + 2)}<input type="range" class="jucePos mdEdHFader" param="Level" style="left: {bx(1) + 8}dp; top: 7dp;"/>')
        for param, c in fields:
            w(f'{ind(d + 2)}<knob class="jucePos mdEdNum" param="{param}" style="left: {bx(c)}dp; top: 1dp;">{{{{{param}_text}}}}</knob>')
        if MD:
            # TrackRoutingView (mdTrackRoutingView.cpp): mdEdOut<track>_<output>, outputs 0 to 5 = A to F, 6 = MAIN
            selector(d + 2, bx(6) + 16, 1, ["MAIN", "A", "B", "C", "D", "E", "F"], [40] + [16] * 6,
                     [f"mdEdOut{t}_{o}" for o in [6, 0, 1, 2, 3, 4, 5]])
        else:
            selector(d + 2, bx(5) + 16, 1, ["AB", "CD", "EF"], [30, 30, 30])
        w(f'{ind(d + 2)}</div>')
    note = "MD : MAIN (A/B stéréo, pan, effets master) ou une seule sortie A à F ; réglage du Global, envoyé au firmware." if MD else \
           "MM : bus AB, CD, EF cumulables ; la piste garde son pan. Choix des bus : à venir."
    w(f'{ind(d + 1)}<div class="jucePos juceLabel mdEdNote" style="left: 16dp; top: {lh - note_h}dp; width: {span(8) - 32}dp;">{note}</div>')
    block_close(d)
    # SORTIES: the three stereo pairs the plug-in exposes to the DAW. OutputMetersView (mdOutputMetersView.cpp)
    # draws the meters, writes the peak in dB, the tracks each pair carries and whether the DAW has it on.
    sh = lh
    ph = (sh - TOP - 8 - 2 * 8) // 3
    pw = span(4) - 32
    block_open(d, "mdEdOutputs", "SORTIES", "bus du plug-in", 8, 4, 12, sh)
    for i, bus in enumerate(["MAIN A/B", "OUT C/D", "OUT E/F"]):
        y = TOP + i * (ph + 8)
        w(f'{ind(d + 1)}<div class="jucePos mdEdPanel" style="left: 16dp; top: {y}dp; width: {pw}dp; height: {ph}dp;">')
        w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdTitle" style="left: 12dp; top: 4dp; width: 120dp;">{bus}</div>')
        w(f'{ind(d + 2)}<div id="mdEdBusActive{i}" class="jucePos mdEdSeg" style="left: {pw - 12 - 150}dp; top: 6dp; width: 150dp;">ACTIF DANS LE DAW</div>')
        # Both meters of the pair, drawn in one canvas
        w(f'{ind(d + 2)}<div id="mdEdMeters{i}" class="jucePos" style="left: 12dp; top: 34dp; width: 20dp; height: {ph - 42}dp;"/>')
        w(f'{ind(d + 2)}<div id="mdEdBusLevel{i}" class="jucePos juceLabel mdEdBusLevel" style="left: 44dp; top: 34dp; width: 80dp;">—</div>')
        w(f'{ind(d + 2)}<div id="mdEdBusTracks{i}" class="jucePos juceLabel mdEdNote" style="left: 44dp; top: 52dp; width: {pw - 56}dp;">—</div>')
        w(f'{ind(d + 2)}<div id="mdEdBusWarning{i}" class="jucePos juceLabel mdEdNote mdEdWarning" style="left: 44dp; top: 70dp; width: {pw - 56}dp;"></div>')
        w(f'{ind(d + 1)}</div>')
    block_close(d)
    return 12 + max(lh, sh) + 12

# ---------- JOUER (boards 1 and 2) ----------

CHAIN_SLOTS = 16
CHAIN_H = 174

def chain_contents(d, gw, state_x):
    # The chain (ChainView, mdChainView.cpp) in a block gw wide: what it does on the title row from state_x,
    # the entries in slots, the chosen slot's pattern as a bank and a number, the actions under them
    w(f'{ind(d)}<div id="mdChainState" class="jucePos juceLabel mdEdSub" style="left: {state_x}dp; top: 4dp; width: {gw - state_x - 16 - 150}dp;">chaîne inactive</div>')
    w(f'{ind(d)}<button id="mdChainEnable" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {gw - 16 - 140}dp; top: 4dp; width: 140dp;">CHAÎNE ACTIVE</button>')
    sw = (gw - 32) // CHAIN_SLOTS
    for s in range(CHAIN_SLOTS):
        w(f'{ind(d)}<div id="mdChainSlot{s}" class="jucePos mdChainSlot mdEdUnread" style="left: {16 + sw * s}dp; top: {TOP}dp; width: {sw - 4}dp;">—</div>')
    py = TOP + 44 + 10
    pw, gap = 42, (gw - 32) - 24 * 42
    selector(d, 16, py, [chr(ord("A") + b) for b in range(8)], [pw] * 8, [f"mdChainBank{b}" for b in range(8)])
    selector(d, 16 + 8 * pw + gap, py, [f"{n + 1:02d}" for n in range(16)], [pw] * 16, [f"mdChainNumber{n}" for n in range(16)])
    ay = py + 24 + 8
    actions = [("mdChainAdd", "AJOUTER"), ("mdChainPassesDown", "PASSAGES −"), ("mdChainPassesUp", "PASSAGES +"),
               ("mdChainMoveLeft", "‹ DÉPLACER"), ("mdChainMoveRight", "DÉPLACER ›"), ("mdChainRemove", "RETIRER"),
               ("mdChainClear", "VIDER")]
    step = (gw - 32) // len(actions)
    for i, (bid, label) in enumerate(actions):
        w(f'{ind(d)}<button id="{bid}" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {16 + step * i}dp; top: {ay}dp; width: {step - 6}dp;">{label}</button>')
    w(f'{ind(d)}<div class="jucePos juceLabel mdEdNote" style="left: 16dp; top: {ay + 24 + 8}dp; width: {gw - 32}dp;">'
      'Joue avec le transport de l\'hôte (SYSTÈME, SUIVRE L\'HÔTE). AJOUTER prend le pattern de la machine ; '
      'A–H et 01–16 changent celui de la case choisie.</div>')

MM_PAGES = ["SYN", "AMP", "FILT", "EFX", "LFO 1", "LFO 2", "LFO 3"]
MM_AMP = ["ATK", "HOLD", "DEC", "REL", "DIST", "VOL", "PAN", "PORT"]

# JOUER's grid, under its block's title row: the 32 steps' numbers (<prefix>Head<step>), a band behind every
# other beat of 4 steps, a line between the two bars, then a row per track: its name (<prefix>Track<track>)
# and its 32 steps (<prefix>Step<track>_<step>), a cell 24 dp wide in a 29 dp column (stepColumns in
# mdStepColumns.h paints the canvases under it on the same columns); the legend under the rows.
# Returns the block's height.
PLAY_LABEL_W = 96
# Beside a lane (LaneInput): what its bars are, and what the mouse does on them
LANE_NOTE = "gris : kit<br/>orange : lock<br/><br/>glisser : lock<br/>double-clic :<br/>l'effacer"

def play_grid(d, prefix, rows, rh, gw, track_class, step_class, legend):
    pitch = (gw - 32 - PLAY_LABEL_W) // 32
    cw = pitch - 5
    x0 = 16 + PLAY_LABEL_W
    rows_top = TOP + 16
    rows_bottom = rows_top + rows * rh
    for g in range(1, 8, 2):
        w(f'{ind(d)}<div class="jucePos mdPlayBand" style="left: {x0 + 4 * g * pitch - 3}dp; top: {TOP - 2}dp; width: {3 * pitch + cw + 6}dp; height: {rows_bottom - TOP + 2}dp;"/>')
    w(f'{ind(d)}<div class="jucePos mdPlayBar" style="left: {x0 + 16 * pitch - 4}dp; top: {TOP - 2}dp; width: 2dp; height: {rows_bottom - TOP + 2}dp;"/>')
    for s in range(32):
        beat = " mdPlayHeadBeat" if s % 4 == 0 else ""
        w(f'{ind(d)}<div id="{prefix}Head{s}" class="jucePos juceLabel mdPlayHead{beat}" style="left: {x0 + pitch * s}dp; top: {TOP}dp; width: {cw}dp;">{s + 1}</div>')
    for t in range(rows):
        y = rows_top + t * rh
        w(f'{ind(d)}<div id="{prefix}Track{t}" class="jucePos juceLabel mdPlayTrack{track_class}" style="left: 16dp; top: {y}dp; width: {PLAY_LABEL_W - 6}dp;">{t + 1:02d} —</div>')
        # the track's trig LED (TrackActivity, mdTrackActivity.cpp) at the end of its name's column
        w(f'{ind(d)}<div id="{prefix}Led{t}" class="jucePos mdEdTrackLed" style="left: {16 + PLAY_LABEL_W - 10}dp; top: {y + (rh - 2 - 6) // 2}dp;"/>')
        for s in range(32):
            w(f'{ind(d)}<div id="{prefix}Step{t}_{s}" class="jucePos mdEdStep mdPlayStep{step_class}" style="left: {x0 + pitch * s}dp; top: {y}dp; width: {cw}dp; height: {rh - 2}dp;"/>')
    chips = "".join(f'<span class="mdPlayChip mdPlayChip{kind}"/>{text}' for kind, text in legend[0])
    w(f'{ind(d)}<div class="jucePos juceLabel mdEdSub mdPlayLegend" style="left: 16dp; top: {rows_bottom + 4}dp; width: {gw - 32}dp;">{chips} · {legend[1]}</div>')
    return rows_bottom + 4 + 16 + 6

def play_page_mm(d):
    # The current pattern, the 6 tracks on 32 steps at a time (1-32 or 33-64), each trig with its note; then
    # under it, as tabs, the edited track's piano roll, the lane of one parameter, or the project's chain.
    # MmPatternView (mdMmPatternView.cpp) fills the grid, the roll and the lane, ChainView (mdChainView.cpp)
    # the chain.
    label_w = PLAY_LABEL_W
    gw = span(12)
    pitch = (gw - 32 - label_w) // 32
    w(f'{ind(d)}<div class="mdEdContent" style="height: {{PLAY_H}}dp;">')
    marker = len(out)
    block_open(d + 1, "mmPlayGrid", "PATTERN", "", 0, 12, 12, "{GRID_H}")
    selector(d + 2, 110, 4, ["PAS 1–32", "PAS 33–64"], [84, 84], ["mmPlayPage0", "mmPlayPage1"])
    # LONGUEUR, COPIER VERS and TOUT EFFACER as on the Machinedrum (PatternCommands)
    w(f'{ind(d + 2)}<button id="mmPlayLength" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {110 + 2 * 84 + 8}dp; top: 4dp; width: 110dp;">LONGUEUR —</button>')
    info_x = 110 + 2 * 84 + 8 + 110 + 12
    clear_x = gw - 16 - 62 - 8 - 120
    copy_x = clear_x - 8 - 130
    w(f'{ind(d + 2)}<div id="mmPlayInfo" class="jucePos juceLabel mdEdSub mdEdRight" style="left: {info_x}dp; top: 4dp; width: {copy_x - 12 - info_x}dp;">pattern : en attente du firmware</div>')
    w(f'{ind(d + 2)}<button id="mmPlayCopy" class="jucePos juceButton mdEdButton mdEdOff" isToggle="0" style="left: {copy_x}dp; top: 4dp; width: 130dp;">COPIER VERS…</button>')
    w(f'{ind(d + 2)}<button id="mmPlayClear" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {clear_x}dp; top: 4dp; width: 120dp;">TOUT EFFACER</button>')
    w(f'{ind(d + 2)}<button id="mmPlayRefresh" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {gw - 16 - 62}dp; top: 4dp; width: 62dp;">RELIRE</button>')
    gh = play_grid(d + 2, "mmPlay", 6, 26, gw, " mmPlayTrack", " mmPlayStep",
                   ([("Trig", "trig et sa note"), ("Lock", "trig avec locks"), ("Silent", "trig sans enveloppe d'ampli"),
                     ("Out", "au-delà de la longueur")],
                    "clic sur un pas : trig · clic dans le piano roll : la note · clic sur un nom : sa piste"))
    for i in range(marker, len(out)):
        out[i] = out[i].replace("{GRID_H}", str(gh))
    block_close(d + 1)
    ly = 12 + gh + GUT
    lh = PAGE_H - ly - 12
    # Tabs instead of a title: PIANO ROLL, LANE, CHAÎNE
    block_open(d + 1, "mmPlayBottom", "", "", 0, 12, ly, lh)
    for n, name in enumerate(["PIANO ROLL", "LANE", "CHAÎNE"]):
        w(f'{ind(d + 2)}<button id="mmPlayTab{n}" class="jucePos juceButton mdEdLfoTab" isToggle="1" tabgroup="mmPlayBottom" tabbutton="{n}" style="left: {16 + n * 96}dp; top: 4dp; width: 90dp;">{name}</button>')
    tabs_w = 16 + 3 * 96 + 12
    d += 1
    area_h = lh - TOP - 8
    # The roll: a keyboard in the names' column, the notes on the steps' columns
    w(f'{ind(d + 1)}<div id="mmPlayRollPage" class="jucePos" tabgroup="mmPlayBottom" tabpage="0" style="left: 0dp; top: 0dp; width: {gw}dp; height: {lh}dp;">')
    # OCTAVE – and + move the notes the roll shows, as the wheel over it does
    up_x = gw - 16 - 80
    down_x = up_x - 4 - 80
    w(f'{ind(d + 2)}<div id="mmPlayRollInfo" class="jucePos juceLabel mdEdSub" style="left: {tabs_w}dp; top: 4dp; width: {down_x - 12 - tabs_w}dp;">—</div>')
    w(f'{ind(d + 2)}<button id="mmPlayRollDown" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {down_x}dp; top: 4dp; width: 80dp;">OCTAVE –</button>')
    w(f'{ind(d + 2)}<button id="mmPlayRollUp" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {up_x}dp; top: 4dp; width: 80dp;">OCTAVE +</button>')
    w(f'{ind(d + 2)}<div id="mmPlayRollKeys" class="jucePos mdEdCurveArea" style="left: 16dp; top: {TOP}dp; width: {label_w - 6}dp; height: {area_h}dp;"/>')
    w(f'{ind(d + 2)}<div id="mmPlayRollArea" class="jucePos mdEdCurveArea" style="left: {16 + label_w}dp; top: {TOP}dp; width: {pitch * 32}dp; height: {area_h}dp;"/>')
    w(f'{ind(d + 1)}</div>')
    # The lane: a page, then a parameter of it, each with its count of locks
    w(f'{ind(d + 1)}<div id="mmPlayLanePage" class="jucePos" tabgroup="mmPlayBottom" tabpage="1" style="left: 0dp; top: 0dp; width: {gw}dp; height: {lh}dp;">')
    w(f'{ind(d + 2)}<div id="mmPlayLaneInfo" class="jucePos juceLabel mdEdSub" style="left: {tabs_w}dp; top: 4dp; width: {gw - tabs_w - 16}dp;">—</div>')
    pw, iw = 80, 57
    selector(d + 2, 16, TOP, MM_PAGES, [pw] * len(MM_PAGES), [f"mmPlayParamPage{p}" for p in range(len(MM_PAGES))])
    selector(d + 2, 16 + pw * len(MM_PAGES) + 16, TOP, MM_AMP, [iw] * 8, [f"mmPlayParam{i}" for i in range(8)])
    lane_top = TOP + 32
    w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdNote" style="left: 16dp; top: {lane_top}dp; width: {label_w - 6}dp;">{LANE_NOTE}</div>')
    w(f'{ind(d + 2)}<div id="mmPlayLaneArea" class="jucePos mdEdCurveArea" style="left: {16 + label_w}dp; top: {lane_top}dp; width: {pitch * 32}dp; height: {lh - lane_top - 8}dp;"/>')
    w(f'{ind(d + 1)}</div>')
    w(f'{ind(d + 1)}<div id="mdPlayChainPage" class="jucePos" tabgroup="mmPlayBottom" tabpage="2" style="left: 0dp; top: 0dp; width: {gw}dp; height: {lh}dp;">')
    chain_contents(d + 2, gw, tabs_w)
    w(f'{ind(d + 1)}</div>')
    d -= 1
    block_close(d + 1)
    w(f'{ind(d)}</div>')
    return ly + lh + 12

def play_page_md(d):
    # The current pattern, every track on 32 steps at a time (1-32 or 33-64 of a pattern over 32 steps),
    # then under it, as tabs, the lane of the edited track and one parameter, or the project's chain.
    # PatternView (mdPatternView.cpp) fills the grid and the lane from the pattern dump, ChainView
    # (mdChainView.cpp) the chain.
    label_w = PLAY_LABEL_W
    gw = span(12)
    pitch = (gw - 32 - label_w) // 32
    w(f'{ind(d)}<div class="mdEdContent" style="height: {{PLAY_H}}dp;">')
    marker = len(out)
    block_open(d + 1, "mdPlayGrid", "PATTERN", "", 0, 12, 12, "{GRID_H}")
    selector(d + 2, 110, 4, ["PAS 1–32", "PAS 33–64"], [84, 84], ["mdPlayPage0", "mdPlayPage1"])
    # LONGUEUR opens a menu of the lengths, 1 to 64
    w(f'{ind(d + 2)}<button id="mdPlayLength" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {110 + 2 * 84 + 8}dp; top: 4dp; width: 110dp;">LONGUEUR —</button>')
    info_x = 110 + 2 * 84 + 8 + 110 + 12
    # TOUT EFFACER asks for a second click (CONFIRMER), then clears every trig and lock of the pattern;
    # COPIER VERS opens a menu of the slots, and asks for a second click (REMPLACER) for one not known empty
    clear_x = gw - 16 - 62 - 8 - 120
    copy_x = clear_x - 8 - 130
    w(f'{ind(d + 2)}<div id="mdPlayInfo" class="jucePos juceLabel mdEdSub mdEdRight" style="left: {info_x}dp; top: 4dp; width: {copy_x - 12 - info_x}dp;">pattern : en attente du firmware</div>')
    w(f'{ind(d + 2)}<button id="mdPlayCopy" class="jucePos juceButton mdEdButton mdEdOff" isToggle="0" style="left: {copy_x}dp; top: 4dp; width: 130dp;">COPIER VERS…</button>')
    w(f'{ind(d + 2)}<button id="mdPlayClear" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {clear_x}dp; top: 4dp; width: 120dp;">TOUT EFFACER</button>')
    w(f'{ind(d + 2)}<button id="mdPlayRefresh" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {gw - 16 - 62}dp; top: 4dp; width: 62dp;">RELIRE</button>')
    gh = play_grid(d + 2, "mdPlay", 16, 16, gw, "", "",
                   ([("Trig", "trig"), ("Lock", "trig avec locks"), ("Out", "au-delà de la longueur")],
                    "clic sur un pas : poser ou retirer un trig · clic sur un nom : sa piste dans la lane"))
    for i in range(marker, len(out)):
        out[i] = out[i].replace("{GRID_H}", str(gh))
    block_close(d + 1)
    ly = 12 + gh + GUT
    lh = PAGE_H - ly - 12
    # Tabs instead of a title: LANE, CHAÎNE
    block_open(d + 1, "mdPlayLane", "", "", 0, 12, ly, lh)
    for n, name in enumerate(["LANE", "CHAÎNE"]):
        w(f'{ind(d + 2)}<button id="mdPlayTab{n}" class="jucePos juceButton mdEdLfoTab" isToggle="1" tabgroup="mdPlayBottom" tabbutton="{n}" style="left: {16 + n * 84}dp; top: 4dp;">{name}</button>')
    tabs_w = 16 + 2 * 84 + 12
    d += 1
    w(f'{ind(d + 1)}<div id="mdPlayLanePage" class="jucePos" tabgroup="mdPlayBottom" tabpage="0" style="left: 0dp; top: 0dp; width: {gw}dp; height: {lh}dp;">')
    w(f'{ind(d + 2)}<div id="mdPlayLaneInfo" class="jucePos juceLabel mdEdSub" style="left: {tabs_w}dp; top: 4dp; width: {gw - tabs_w - 16 - 160}dp;">—</div>')
    w(f'{ind(d + 2)}<button id="mdPlayOpen" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {gw - 16 - 150}dp; top: 4dp; width: 150dp;">OUVRIR DANS SON</button>')
    pb = (gw - 32) // 24
    names = ["P1", "P2", "P3", "P4", "P5", "P6", "P7", "P8", "AMD", "AMF", "EQF", "EQG", "BASE", "WDTH", "Q", "SRR",
             "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"]
    for p, name in enumerate(names):
        w(f'{ind(d + 2)}<div id="mdPlayParam{p}" class="jucePos mdEdSeg mdEdSegChoice" style="left: {16 + pb * p}dp; top: {TOP}dp; width: {pb - 2}dp;">{name}</div>')
    area_top = TOP + 32
    area_h = lh - area_top - 8
    w(f'{ind(d + 2)}<div class="jucePos juceLabel mdEdNote" style="left: 16dp; top: {area_top}dp; width: {label_w - 6}dp;">{LANE_NOTE}</div>')
    # The 32 bars, drawn in a canvas under the steps
    w(f'{ind(d + 2)}<div id="mdPlayLaneArea" class="jucePos mdEdCurveArea" style="left: {16 + label_w}dp; top: {area_top}dp; width: {pitch * 32}dp; height: {area_h}dp;"/>')
    w(f'{ind(d + 1)}</div>')
    w(f'{ind(d + 1)}<div id="mdPlayChainPage" class="jucePos" tabgroup="mdPlayBottom" tabpage="1" style="left: 0dp; top: 0dp; width: {gw}dp; height: {lh}dp;">')
    chain_contents(d + 2, gw, tabs_w)
    w(f'{ind(d + 1)}</div>')
    d -= 1
    block_close(d + 1)
    w(f'{ind(d)}</div>')
    return ly + lh + 12

# ---------- BIBLIO (board 8) ----------

def library_page(d):
    # Two tabs, KITS and PATTERNS, with the reading's state and RELIRE beside them. KITS: every stored Kit,
    # number and name; a click shows its machines without loading it. PATTERNS: every stored pattern, its
    # length and Kit, A to H in columns. LibraryView (mdLibraryView.cpp) reads both when BIBLIO first shows,
    # and fills them.
    kits, cols, kb = (64, 4, 8) if MD else (128, 8, 12)
    rows, rh = 16, 22
    ty = 12
    by = ty + 24 + 8
    kh = TOP + 28 + rows * rh + 8
    gw = span(12)
    w(f'{ind(d)}<div class="mdEdContent" style="height: {{LIB_H}}dp;">')
    for n, name in enumerate(["KITS", "PATTERNS"]):
        w(f'{ind(d + 1)}<button id="mdLibTab{n}" class="jucePos juceButton mdEdLfoTab" isToggle="1" tabgroup="mdLib" tabbutton="{n}" style="left: {colx(0) + n * 96}dp; top: {ty}dp; width: 90dp;">{name}</button>')
    info_x = colx(0) + 2 * 96 + 12
    w(f'{ind(d + 1)}<div id="mdLibInfo" class="jucePos juceLabel mdEdSub mdLibInfo" style="left: {info_x}dp; top: {ty}dp; width: {colx(0) + gw - 140 - 12 - info_x}dp;">bibliothèque non lue</div>')
    w(f'{ind(d + 1)}<button id="mdLibRead" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {colx(0) + gw - 140}dp; top: {ty}dp; width: 140dp;">RELIRE</button>')
    page_w = M + gw + M
    # KITS
    w(f'{ind(d + 1)}<div id="mdLibKitsPage" class="jucePos" tabgroup="mdLib" tabpage="0" style="left: 0dp; top: 0dp; width: {page_w}dp; height: {{LIB_H}}dp;">')
    block_open(d + 2, "mdLibKits", "KITS", "clic : ses machines, sans le charger · en ambre : le kit chargé", 0, kb, by, kh)
    cw = (span(kb) - 32) // cols
    for slot in range(kits):
        c, r = slot // rows, slot % rows
        w(f'{ind(d + 3)}<div id="mdLibKit{slot}" class="jucePos juceLabel mdLibKit mdEdUnread" style="left: {16 + c * cw}dp; top: {TOP + 4 + r * rh}dp; width: {cw - 6}dp;">{slot + 1:02d}  —</div>')
    block_close(d + 2)
    tracks = 16 if MD else 6
    if MD:
        block_open(d + 2, "mdLibMachines", "MACHINES", "sans charger le kit", 8, 4, by, kh)
        w(f'{ind(d + 3)}<div id="mdLibDetail" class="jucePos juceLabel mdEdRowName" style="left: 16dp; top: {TOP - 4}dp; width: {span(4) - 32}dp;">KIT —</div>')
        for t in range(tracks):
            w(f'{ind(d + 3)}<div id="mdLibMachine{t}" class="jucePos juceLabel mdEdNote" style="left: 16dp; top: {TOP + 28 + t * rh}dp; width: {span(4) - 32}dp;">{t + 1:02d}  —</div>')
        block_close(d + 2)
        total = by + kh + 12
    else:
        my = by + kh + GUT
        mh = PAGE_H - my - 12
        block_open(d + 2, "mdLibMachines", "MACHINES", "", 0, 12, my, mh)
        w(f'{ind(d + 3)}<div id="mdLibDetail" class="jucePos juceLabel mdEdSub" style="left: 120dp; top: 4dp; width: 600dp;">KIT —</div>')
        for t in range(tracks):
            w(f'{ind(d + 3)}<div id="mdLibMachine{t}" class="jucePos juceLabel mdEdNote" style="left: {16 + t * 170}dp; top: 28dp; width: 164dp;">{t + 1:02d}  —</div>')
        block_close(d + 2)
        total = my + mh + 12
    w(f'{ind(d + 1)}</div>')
    # PATTERNS: banks A to H in columns, 01 to 16 in rows
    w(f'{ind(d + 1)}<div id="mdLibPatternsPage" class="jucePos" tabgroup="mdLib" tabpage="1" style="left: 0dp; top: 0dp; width: {page_w}dp; height: {{LIB_H}}dp;">')
    block_open(d + 2, "mdLibPatterns", "PATTERNS", "", 0, 12, by, kh)
    # COPIER takes the pattern clicked, COLLER copies it onto the one clicked then; REMPLACER asks for a
    # second click for one not known empty (LibraryView)
    w(f'{ind(d + 3)}<button id="mdLibCopy" class="jucePos juceButton mdEdButton mdEdOff" isToggle="0" style="left: 110dp; top: 4dp; width: 80dp;">COPIER</button>')
    w(f'{ind(d + 3)}<button id="mdLibPaste" class="jucePos juceButton mdEdButton mdEdOff" isToggle="0" style="left: 198dp; top: 4dp; width: 140dp;">COLLER</button>')
    detail_x = 350
    w(f'{ind(d + 3)}<div id="mdLibPatternDetail" class="jucePos juceLabel mdEdSub mdEdRight" style="left: {detail_x}dp; top: 4dp; width: {gw - detail_x - 16}dp;">—</div>')
    pw = (gw - 32) // 8
    for slot in range(128):
        c, r = slot // 16, slot % 16
        w(f'{ind(d + 3)}<div id="mdLibPattern{slot}" class="jucePos juceLabel mdLibKit mdEdUnread" style="left: {16 + c * pw}dp; top: {TOP + 4 + r * rh}dp; width: {pw - 6}dp;">{chr(ord("A") + c)}{r + 1:02d}  —</div>')
    block_close(d + 2)
    w(f'{ind(d + 1)}</div>')
    w(f'{ind(d)}</div>')
    return total

# ---------- SYSTÈME (board 10) ----------

# The plug-in menu's GUI scales (PluginEditorState::openMenu) and 130, the scale a new install opens at
SCALES = [50, 65, 75, 85, 100, 125, 130, 150, 175, 200, 250, 300]

def system_page(d):
    # One row per subject: label 3 columns, state 7, action 2. SystemPage (mdSystemPage.cpp) fills the
    # states and acts on the buttons. ÉCHELLE and DIAGNOSTICS hold what the plug-in menu (OPTIONS, right
    # click) has besides RAM recording, SysEx and the settings: every entry of that menu has a row here.
    rows = [("mdSysGlobal", "RÉGLAGES GLOBAUX", None, "face avant"),
            ("mdSysFollow", "SYNCHRO DAW", "mdSysFollowTempo", "SUIVRE L'HÔTE"),
            ("mdSysParallel", "TRANSPORT PARALLÈLE", "mdSysParallel", "PARALLÈLE")]
    if MD:
        rows.append(("mdSysRam", "ENREGISTREMENT RAM", "mdSysRamComplete", "QUEUES COMPLÈTES"))
    rows += [("mdSysSysex", "TRANSFERT SYSEX", "mdSysSysex", "FICHIER…"),
             ("mdSysStorage", "STOCKAGE MACHINE", "mdSysStorage", "CHARGER…"),
             ("mdSysScale", "ÉCHELLE DE L'INTERFACE", None, "—"),
             ("mdSysDiagnostics", "DIAGNOSTICS", "mdSysCapture", "CAPTURE"),
             ("mdSysPlugin", "PLUG-IN", "mdSysSettings", "RÉGLAGES…")]
    states = {"mdSysStorage": "remplace toute la mémoire de la machine (kits, patterns, samples) ; confirmation demandée",
              "mdSysPlugin": "skin, rendu, ports MIDI, MIDI Learn, entrée audio ; le même menu dans OPTIONS ou au clic droit"}
    rh = 40
    h = TOP + len(rows) * rh + 8
    w(f'{ind(d)}<div class="mdEdContent" style="height: {12 + h + 12 + 28}dp;">')
    block_open(d + 1, "mdSysList", "SYSTÈME", "machine entière et plug-in", 0, 12, 12, h)
    for r, (sid, label, button, action) in enumerate(rows):
        y = TOP + r * rh
        w(f'{ind(d + 2)}<div class="jucePos mdEdRow" style="left: 16dp; top: {y}dp; width: {span(12) - 32}dp; height: {rh}dp;">')
        w(f'{ind(d + 3)}<div class="jucePos juceLabel mdEdRowName" style="left: 0dp; top: 7dp; width: {span(3)}dp;">{label}</div>')
        state = states.get(sid, "—")
        if sid == "mdSysScale":
            # The scales as a selector in the state columns, the current one (also a dragged size) on the right
            sw = span(7) // len(SCALES)
            selector(d + 3, bx(3) - 16, 8, [f"{s} %" for s in SCALES], [sw] * len(SCALES), [f"mdSysScale{s}" for s in SCALES])
        elif sid == "mdSysDiagnostics":
            # The capture's state in 5 columns, the logs folder in the 2 before the action
            w(f'{ind(d + 3)}<div id="{sid}State" class="jucePos juceLabel mdEdNote" style="left: {bx(3) - 16}dp; top: 12dp; width: {span(5)}dp;">{state}</div>')
            w(f'{ind(d + 3)}<button id="mdSysLogs" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {bx(8) - 16}dp; top: 8dp; width: {span(2) - 16}dp;">JOURNAUX…</button>')
        else:
            w(f'{ind(d + 3)}<div id="{sid}State" class="jucePos juceLabel mdEdNote" style="left: {bx(3) - 16}dp; top: 12dp; width: {span(7)}dp;">{state}</div>')
        if button:
            w(f'{ind(d + 3)}<button id="{button}" class="jucePos juceButton mdEdButton" isToggle="0" style="left: {bx(10) - 16}dp; top: 8dp; width: {span(2) - 16}dp;">{action}</button>')
        else:
            aid = f' id="{sid}State"' if sid == "mdSysScale" else ""
            w(f'{ind(d + 3)}<div{aid} class="jucePos juceLabel mdEdNote mdEdRight" style="left: {bx(10) - 16}dp; top: 12dp; width: {span(2) - 16}dp;">{action}</div>')
        w(f'{ind(d + 2)}</div>')
    block_close(d + 1)
    note = ("Les réglages de la machine (canal MIDI, table de notes, sync, routage) se font sur sa face avant, menu GLOBAL ; "
            "le routage des pistes aussi dans MIX." if MD else
            "Les réglages de la machine (canaux MIDI, sync) se font sur sa face avant, menu GLOBAL.")
    w(f'{ind(d + 1)}<div class="jucePos juceLabel mdEdNote" style="left: 32dp; top: {12 + h + 12}dp; width: 1036dp;">{note}</div>')
    w(f'{ind(d)}</div>')
    return 12 + h + 12 + 28

# ---------- page assembly ----------

TRACKS = 16 if MD else 6
LOGO = "GEARMULATOR MD" if MD else "GEARMULATOR MM"

w('\t\t\t<!-- SON: the selected track. Its controls are bound to partCurrent, which the track tabs set. -->')
w('\t\t\t<div id="mdEdPageSound" class="mdEdPage" tabgroup="mdEdit" tabpage="0">')
w('\t\t\t\t<div class="mdEdStrip">')
for t in range(TRACKS):
    if MD:
        w(f'\t\t\t\t\t<button id="editTrack{t}" class="jucePos juceButton mdEdTrackTab" isToggle="1" tabgroup="mdTrack" tabbutton="{t}" style="left: {M + t * 63}dp;">{t + 1:02d}</button>')
        led_x = M + t * 63 + 60 - 12
    else:
        w(f'\t\t\t\t\t<button id="editTrack{t}" class="jucePos juceButton mdEdTrackTab" isToggle="1" tabgroup="mdTrack" tabbutton="{t}" style="left: {colx(2 * t)}dp; width: {span(2)}dp;">PISTE {t + 1:02d}</button>')
        led_x = colx(2 * t) + span(2) - 18
    # the track's trig LED (TrackActivity, mdTrackActivity.cpp) on the right of its tab, which is 28 dp high from 4 dp
    w(f'\t\t\t\t\t<div id="mdEdTrackLed{t}" class="jucePos mdEdTrackLed" style="left: {led_x}dp; top: {4 + (28 - 6) // 2}dp;"/>')
if MD:
    w(f'\t\t\t\t\t<button id="editMaster" class="jucePos juceButton mdEdTrackTab" isToggle="1" tabgroup="mdTrack" tabbutton="16" style="left: {M + 16 * 63}dp;">MASTER</button>')
w('\t\t\t\t</div>')
w('\t\t\t\t<div class="mdEdScroll">')
marker = len(out)
track_h, master_h, roomy_h = (md_sound if MD else mm_sound)(5)
w('\t\t\t\t</div>')
w('\t\t\t</div>')
for i in range(marker, len(out)):
    out[i] = out[i].replace("{TRACK_H}", str(track_h)).replace("{ROOMY_H}", str(roomy_h)).replace("{MASTER_H}", str(master_h))

w('\t\t\t<!-- MIX: one row per track, each bound to its own part; outputs on the right. -->')
w('\t\t\t<div id="mdEdPageMix" class="mdEdPage" tabgroup="mdEdit" tabpage="1">')
w('\t\t\t\t<div class="mdEdScroll">')
marker = len(out)
mix_h = mix_page(5, TRACKS)
for i in range(marker, len(out)):
    out[i] = out[i].replace("{MIX_H}", str(mix_h))
w('\t\t\t\t\t</div>')
w('\t\t\t\t</div>')
w('\t\t\t</div>')

w('\t\t\t<!-- JOUER: the current pattern, then a lane and the chain (MD), or a piano roll, a lane and the chain (MM). -->')
w('\t\t\t<div id="mdEdPagePlay" class="mdEdPage" tabgroup="mdEdit" tabpage="2">')
w('\t\t\t\t<div class="mdEdScroll">')
marker = len(out)
play_h = play_page_md(5) if MD else play_page_mm(5)
for i in range(marker, len(out)):
    out[i] = out[i].replace("{PLAY_H}", str(play_h))
w('\t\t\t\t</div>')
w('\t\t\t</div>')

w('\t\t\t<!-- BIBLIO: the stored Kits, read on request, and the machines of one. -->')
w('\t\t\t<div id="mdEdPageLibrary" class="mdEdPage" tabgroup="mdEdit" tabpage="3">')
w('\t\t\t\t<div class="mdEdScroll">')
marker = len(out)
lib_h = library_page(5)
for i in range(marker, len(out)):
    out[i] = out[i].replace("{LIB_H}", str(lib_h))
w('\t\t\t\t</div>')
w('\t\t\t</div>')

w('\t\t\t<!-- SYSTÈME: the machine and the plug-in, one row per subject. -->')
w('\t\t\t<div id="mdEdPageSystem" class="mdEdPage" tabgroup="mdEdit" tabpage="4">')
w('\t\t\t\t<div class="mdEdScroll">')
system_h = system_page(5)
w('\t\t\t\t</div>')
w('\t\t\t</div>')

for name, h in [("SON", track_h), ("MASTER", master_h), ("MIX", mix_h), ("SYSTÈME", system_h), ("JOUER", play_h), ("BIBLIO", lib_h)]:
    if h and h > PAGE_H:
        sys.exit(f"{MODEL} {name} is {h} dp, more than the {PAGE_H} dp page")

# The top bar, then the editor; the front panel sits between them in the skin (see ViewLayout, mdViewLayout.cpp).
topbar = f'''		<!-- ===== Top bar: name, the FACE AVANT / ÉDITEUR switch, kit/pattern screen, categories, options. ===== -->
		<div id="mdTopBar" class="mdEdTopBar">
			<div class="jucePos juceLabel mdEdLogo" style="left: 16dp; top: 6dp; width: 168dp;">{LOGO}</div>
			<button id="mdViewPanel" class="jucePos juceButton mdEdSwitch" isToggle="1" style="left: {colx(2)}dp;">FACE AVANT</button>
			<button id="mdViewEditor" class="jucePos juceButton mdEdSwitch" isToggle="1" style="left: {colx(3)}dp;">ÉDITEUR</button>
			<div id="mdEdScreen" class="jucePos mdEdScreen" style="left: {colx(4)}dp; top: 4dp; width: {span(2)}dp;"><div id="mdEdScreenMain" class="mdEdScreenMain">KIT — · PATTERN —</div><div id="mdEdScreenName" class="mdEdScreenName"></div></div>
			<button id="mdEdCat2" class="jucePos juceButton mdEdCat" tabgroup="mdEdit" tabbutton="2" style="left: {colx(6)}dp;">JOUER</button>
			<button id="mdEdCat0" class="jucePos juceButton mdEdCat" tabgroup="mdEdit" tabbutton="0" style="left: {colx(7)}dp;">SON</button>
			<button id="mdEdCat1" class="jucePos juceButton mdEdCat" tabgroup="mdEdit" tabbutton="1" style="left: {colx(8)}dp;">MIX</button>
			<button id="mdEdCat3" class="jucePos juceButton mdEdCat" tabgroup="mdEdit" tabbutton="3" style="left: {colx(9)}dp;">BIBLIO</button>
			<button id="mdEdCat4" class="jucePos juceButton mdEdCat" tabgroup="mdEdit" tabbutton="4" style="left: {colx(10)}dp;">SYSTÈME</button>
			<button id="mdEdOptions" class="jucePos juceButton mdEdSwitch" isToggle="0" style="left: {colx(11)}dp;">OPTIONS</button>
		</div>
'''
editor = '''		<!-- ===== Editor: shown instead of the front panel, or below it when the window is tall enough. ===== -->
		<div id="mdEditor" class="mdEdRoot">
''' + "\n".join(out) + "\n\t\t</div>\n"
open(f"topbar_{MODEL}.rml.txt", "w").write(topbar)
open(f"editor_{MODEL}.rml.txt", "w").write(editor)
print(MODEL, "SON", track_h, "with curves", roomy_h, "MASTER", master_h, "MIX", mix_h)
