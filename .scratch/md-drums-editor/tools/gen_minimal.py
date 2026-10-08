"""MD Drums' parameter descriptions and its minimal skin, generated from one list of parameters.

Run from the repository root: python .scratch/md-drums-editor/tools/gen_minimal.py

Writes source/elektron/md/mdDrumsPlugin/parameterDescriptions_mddrums.json and the skin
source/elektron/md/mdDrumsPlugin/skins/mdDrumsMinimal/. The parameter pages, indices and order are part of the JUCE
parameter IDs and of the host's parameter order (ticket 06): append parameters, never renumber.
The minimal skin stands in for the v22 editor until ticket 08 builds it: every parameter as a number dragged like a
knob, the machine as a menu, Mute, Solo and Out as buttons.
"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
PLUGIN = os.path.join(ROOT, 'source', 'elektron', 'md', 'mdDrumsPlugin')
MACHINES = os.path.join(ROOT, 'source', 'elektron', 'md', 'mdProtocol', 'mdmachines.cpp')

TRACK_PARAMS = ['SYN1', 'SYN2', 'SYN3', 'SYN4', 'SYN5', 'SYN6', 'SYN7', 'SYN8',
                'AMD', 'AMF', 'EQF', 'EQG', 'FLTF', 'FLTW', 'FLTQ', 'SRR',
                'DIST', 'VOL', 'PAN', 'DEL', 'REV', 'LFOS', 'LFOD', 'LFOM']
TRACK_DEFAULTS = [64] * 8 + [0, 0, 64, 64, 0, 127, 0, 0] + [0, 100, 64, 0, 0, 64, 0, 0]
BIPOLAR = {'EQG', 'PAN'}

MASTER = [('Echo', 'Echo', ['TIME', 'MOD', 'MFRQ', 'FB', 'FLTF', 'FLTW', 'MONO', 'LEV'], [16, 0, 32, 27, 0, 44, 0, 71]),
          ('Reverb', 'Reverb', ['DVOL', 'PRED', 'DEC', 'DAMP', 'HP', 'LP', 'GATE', 'LEV'], [0, 0, 68, 50, 1, 82, 71, 109]),
          ('Eq', 'EQ', ['LF', 'LG', 'HF', 'HG', 'PF', 'PG', 'PQ', 'GAIN'], [64, 64, 64, 64, 64, 64, 64, 127]),
          ('Dynamix', 'Dynamix', ['ATCK', 'REL', 'TRHD', 'RTIO', 'KNEE', 'HP', 'OUTG', 'MIX'], [127, 127, 127, 127, 127, 127, 0, 0])]
MASTER_BIPOLAR = {('Eq', 'LG'), ('Eq', 'HG'), ('Eq', 'PG')}


def machine_names():
    text = open(MACHINES, encoding='utf-8').read()
    start = text.index('createMdMachines()')
    end = text.index('createMmMachines()')
    names = {}
    for match in re.finditer(r'\{(\d+), "([^"]+)", Md\w+, (true|false)\}', text[start:end]):
        names[int(match.group(1))] = match.group(2)
    return [names.get(i, '#%d' % i) for i in range(192)]


def descriptions():
    """(name, displayName, page, index, extra) in the host's order: per track, then the master effects."""
    track = [('Machine', 'Machine', 5, 0, {'max': 191, 'default': 16, 'toText': 'machines'})]
    for i, name in enumerate(TRACK_PARAMS):
        extra = {'default': TRACK_DEFAULTS[i]}
        if name in BIPOLAR:
            extra['isBipolar'] = True
        track.append((name, name, i // 8, i % 8, extra))
    track += [('LfoTrack', 'LFO Track', 6, 0, {'max': 15, 'toText': 'lfoTracks'}),
              ('LfoParam', 'LFO Param', 6, 1, {'max': 23, 'toText': 'lfoParams'}),
              ('LfoShape1', 'LFO Shape 1', 6, 2, {'max': 5, 'toText': 'lfoShapes'}),
              ('LfoShape2', 'LFO Shape 2', 6, 3, {'max': 5, 'toText': 'lfoShapes'}),
              ('LfoMode', 'LFO Mode', 6, 4, {'max': 2, 'toText': 'lfoModes'}),
              ('Level', 'Level', 3, 0, {'default': 100}),
              ('Mute', 'Mute', 4, 0, {'max': 1, 'isBool': True, 'toText': 'offOn'}),
              ('Solo', 'Solo', 7, 0, {'max': 1, 'isBool': True, 'toText': 'offOn'}),
              ('Out', 'Out', 7, 1, {'max': 1, 'isBool': True, 'toText': 'outs'})]
    master = []
    for e, (key, label, names, defaults) in enumerate(MASTER):
        for p, name in enumerate(names):
            extra = {'default': defaults[p], 'class': 'NonPartSensitive'}
            if (key, name) in MASTER_BIPOLAR:
                extra['isBipolar'] = True
            master.append((key + name, label + ' ' + name, 8, e * 8 + p, extra))
    return track, master


def write_json(track, master):
    entries = []
    for name, display, page, index, extra in track + master:
        entry = {'page': page, 'index': index, 'name': name, 'displayName': display}
        entry.update(extra)
        entries.append(entry)
    doc = {
        'parameterdescriptiondefaults': {
            'isPublic': True, 'isBipolar': False, 'toText': {'format': '%d'}, 'name': '', 'class': '',
            'min': 0, 'max': 127, 'isBool': False, 'isDiscrete': True, 'page': 0, 'step': 0},
        'valuelists': {
            'offOn': ['Off', 'On'],
            'outs': ['Main', 'Out'],
            'machines': machine_names(),
            'lfoTracks': [str(t + 1) for t in range(16)],
            'lfoParams': TRACK_PARAMS,
            'lfoShapes': ['TRI', 'SAW', 'SQR', 'RMP', 'EXP', 'RND'],
            'lfoModes': ['FREE', 'TRIG', 'HOLD']},
        'parameterdescriptions': entries}
    lines = ['{']
    lines.append('\t"parameterdescriptiondefaults": ' + json.dumps(doc['parameterdescriptiondefaults']) + ',')
    lines.append('\t"valuelists":')
    lines.append('\t{')
    lists = list(doc['valuelists'].items())
    for i, (key, values) in enumerate(lists):
        lines.append('\t\t"%s": %s%s' % (key, json.dumps(values), ',' if i + 1 < len(lists) else ''))
    lines.append('\t},')
    lines.append('\t"parameterdescriptions":')
    lines.append('\t[')
    for i, entry in enumerate(entries):
        lines.append('\t\t' + json.dumps(entry) + (',' if i + 1 < len(entries) else ''))
    lines.append('\t]')
    lines.append('}')
    path = os.path.join(PLUGIN, 'parameterDescriptions_mddrums.json')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return len(entries)


def write_skin(track, master):
    folder = os.path.join(PLUGIN, 'skins', 'mdDrumsMinimal')
    os.makedirs(folder, exist_ok=True)
    cell = 34
    x0 = 16 + 40 + 96
    out = []
    out.append('<rml>')
    out.append('\t<head>')
    out.append('\t\t<link type="text/rcss" href="tus_default.rcss"/>')
    out.append('\t\t<link type="text/rcss" href="tus_juceskin.rcss"/>')
    out.append('\t\t<link type="text/rcss" href="mdDrumsMinimal.rcss"/>')
    out.append('\t</head>')
    out.append('\t<!-- Generated by .scratch/md-drums-editor/tools/gen_minimal.py: edit the script, not this file. -->')
    out.append('\t<body id="Root" class="mdd" style="width: 1296dp; height: 824dp;">')
    out.append('\t\t<div class="jucePos juceLabel mddTitle" style="left: 16dp; top: 16dp; width: 400dp;">MD Drums</div>')
    out.append('\t\t<div class="jucePos juceLabel mddNote" style="left: 432dp; top: 16dp; width: 848dp;">'
               'Drag a value up or down. Notes 36 to 51 play tracks 1 to 16.</div>')
    columns = [name for name, _, _, _, _ in track if name not in ('Machine', 'Mute', 'Solo', 'Out')]
    for c, name in enumerate(columns):
        label = name.replace('Lfo', 'L').replace('Shape', 'S').replace('Level', 'LEV').replace('Track', 'T') \
            .replace('Param', 'P').replace('Mode', 'M')
        out.append('\t\t<div class="jucePos juceLabel mddHead" style="left: %ddp; top: 56dp; width: %ddp;">%s</div>'
                   % (x0 + c * cell, cell, label))
    xb = x0 + len(columns) * cell + 8
    for b, name in enumerate(['M', 'S', 'O']):
        out.append('\t\t<div class="jucePos juceLabel mddHead" style="left: %ddp; top: 56dp; width: 28dp;">%s</div>'
                   % (xb + b * 28, name))
    for t in range(16):
        y = 80 + t * 36
        out.append('\t\t<div class="jucePos mddRow" data-model="part%d" style="left: 0dp; top: %ddp; width: 1296dp; height: 36dp;">' % (t, y))
        out.append('\t\t\t<div class="jucePos juceLabel mddTrack" style="left: 16dp; top: 6dp; width: 40dp;">%d</div>' % (t + 1))
        out.append('\t\t\t<combo class="jucePos mddMachine" param="Machine" style="left: 56dp; top: 6dp; width: 92dp; height: 24dp;"/>')
        for c, name in enumerate(columns):
            # SYN1-8 read their machine's names in the host ("PTCH 64"); a cell has room for the value only
            shown = name + ('_value' if name.startswith('SYN') else '_text')
            out.append('\t\t\t<knob class="jucePos mddNum" param="%s" style="left: %ddp; top: 6dp; width: %ddp;">{{%s}}</knob>'
                       % (name, x0 + c * cell, cell, shown))
        for b, name in enumerate(['Mute', 'Solo', 'Out']):
            out.append('\t\t\t<button class="jucePos juceButton mddToggle" isToggle="1" param="%s" style="left: %ddp; top: 8dp;"/>'
                       % (name, xb + b * 28))
        out.append('\t\t</div>')
    out.append('\t\t<div class="jucePos juceLabel mddSection" style="left: 16dp; top: 676dp; width: 400dp;">Master effects (stored in the Kit, silent until they are built)</div>')
    out.append('\t\t<div class="jucePos" data-model="part0" style="left: 0dp; top: 708dp; width: 1296dp; height: 100dp;">')
    for i, (name, display, _, _, _) in enumerate(master):
        x = 16 + i * 39
        short = display.split(' ')[1]
        out.append('\t\t\t<div class="jucePos juceLabel mddHead" style="left: %ddp; top: 0dp; width: 39dp;">%s</div>' % (x, short))
        out.append('\t\t\t<knob class="jucePos mddNum" param="%s" style="left: %ddp; top: 20dp; width: 39dp;">{{%s_text}}</knob>'
                   % (name, x, name))
    for e, (_, label, _, _) in enumerate(MASTER):
        out.append('\t\t\t<div class="jucePos juceLabel mddNote" style="left: %ddp; top: 52dp; width: 312dp;">%s</div>' % (16 + e * 312, label))
    out.append('\t\t</div>')
    out.append('\t</body>')
    out.append('</rml>')
    with open(os.path.join(folder, 'mdDrumsMinimal.rml'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')

    rcss = '''/* MD Drums' minimal skin (gen_minimal.py writes the document, this style is written by hand there too) */
.mdd { background-color: #1a1b1d; color: #e8e6e1; font-family: Roboto; font-size: 12dp; }
.mddTitle { height: 28dp; line-height: 28dp; font-size: 20dp; font-weight: bold; }
.mddNote { height: 28dp; line-height: 28dp; color: #8a8d93; }
.mddSection { height: 24dp; line-height: 24dp; font-size: 14dp; font-weight: bold; }
.mddHead { height: 20dp; line-height: 20dp; font-size: 10dp; text-align: center; color: #8a8d93; }
.mddRow { border-bottom: 1dp #2e3034; }
.mddTrack { height: 24dp; line-height: 24dp; font-weight: bold; }
.mddMachine { color: #e8e6e1; line-height: 24dp; }
.mddNum { height: 24dp; line-height: 24dp; text-align: center; white-space: nowrap; color: #e8e6e1; cursor: pointer;
	decorator: none; background-color: transparent; border-width: 0dp; box-shadow: none; }
.mddNum:hover { color: #ff6b1f; }
.mddToggle { width: 20dp; height: 20dp; background-color: #2e3034; border-radius: 3dp; }
.mddToggle:checked { background-color: #ff6b1f; }
'''
    with open(os.path.join(folder, 'mdDrumsMinimal.rcss'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(rcss)


if __name__ == '__main__':
    track, master = descriptions()
    count = write_json(track, master)
    write_skin(track, master)
    print('%d descriptions: %d per track (%d host parameters), %d master' % (count, len(track), len(track) * 16 + len(master), len(master)))
