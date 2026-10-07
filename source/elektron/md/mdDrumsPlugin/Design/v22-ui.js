// MD Drums v22 mockup (v22-ui.css; machine data in v6-machines.js; real hits in v22-hits.js). v21's state, layout and
// behaviour (a Figma column grid, multiples of 4; absolute boxes from box() and raw(); knobs dim at their default, in
// ink once edited), with the user's review of v21: the screens in colour again (the curves the controls set, and the
// options chosen on a screen, in the accent); the hit's waveform follows the SYN parameters this mockup can model, in a
// window as long as the hit; ASSIGN a word, not a framed button; the LFO plotted in its target's units, around the
// target knob's value; the play key lights only while pressed; MIX chooses each track's output; the kits (the top
// bar's kit opens 64 slots: load, save, rename, delete, import and export Machinedrum kits as .syx).
"use strict";

const MD = (() => {
	// ---- the grid ----
	// 16 columns of 64 px, gutters of 16, and margins of 16 at the window's edges (1296 px wide).
	const WIN = 1296, X = c => 16 + (c - 1) * 80, W = n => n * 64 + (n - 1) * 16;
	const box = (c, n, y, h) => `left:${X(c)}px;width:${W(n)}px;top:${y}px;height:${h}px`;
	const raw = (x, w, y, h) => `left:${x}px;width:${w}px;top:${y}px;height:${h}px`;
	// The three bands' tops; each is 216 px tall, the last ends at the window's bottom, 16 px under the track list's last
	// row (y 792). The head band over them is 120 px (y 40-160), its content 28 px from the rules over and under it, as
	// the bands' is. These and every y under the top bar are given as in a window whose bar is 40 px tall: the bar is 56
	// (its controls 16 px from the window's top, as from its other edges), so all under it sits 16 px lower, in one
	// container (808 px), and the window is 1296 x 824.
	const BAND = [160, 376, 592], BAND_H = 216;

	// What each family is, in plain words, under its code in the browser.
	const kinds = { GND: "generators", TRX: "analog", EFM: "FM", E12: "factory samples", "P-I": "physical", ROM: "UW samples" };
	const families = { GND: "generator", TRX: "modelled analog", EFM: "FM synthesis", E12: "built-in 12-bit sample",
		"P-I": "physical modelling", ROM: "UW bank sample" };
	const kindTips = { ROM: "UW bank: 32 samples in 48 slots, about 970 KiB of a MKII's 2.5 MB; ROM-25 to 48 are optimised for loops" };
	const familyOrder = ["GND", "TRX", "EFM", "E12", "P-I", "ROM"];
	const byId = new Map(MD_MACHINES.map(m => [m.id, m]));
	const romSlots = 32;
	const offered = m => familyOrder.includes(m.family) && (m.family !== "ROM" || +m.name.slice(4) <= romSlots);
	const machineList = MD_MACHINES.filter(offered).map(m => m.id);
	const efxNames = ["AMD","AMF","EQF","EQG","FLTF","FLTW","FLTQ","SRR"];
	const routeNames = ["DIST","VOL","PAN","DEL","REV","LFOS","LFOD","LFOM"];
	const shapes = ["TRI","SAW","SQR","RMP","EXP","RND"];
	const modes = ["FREE","TRIG","HOLD"];
	const defaults = [64,64,64,64,64,64,64,64, 0,0,64,64,0,127,0,0, 0,100,64,0,0,64,0,0];
	// The tracks' DEL and REV send to the master effects' echo and reverb (the Master tab).
	const sendsPending = new Set();
	const two = n => String(n).padStart(2, "0");
	const specNames = ["SYN1","SYN2","SYN3","SYN4","SYN5","SYN6","SYN7","SYN8", ...efxNames, ...routeNames];
	const pid = (t, name) => `t${t + 1}_${name}`;
	const reduceMotion = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

	// Each band's knobs in the Machinedrum's order, and its groups: name, first slot, slot after the last, waiting.
	const layouts = {
		syn: { order: [0, 1, 2, 3, 4, 5, 6, 7], groups: [["", 0, 8]] },
		efx: { order: [8, 9, 10, 11, 12, 13, 14, 15], groups: [["AM", 0, 2], ["EQ", 2, 4], ["Filter", 4, 7]] },
		rte: { order: [16, 17, 18, 19, 20, 21, 22, 23], groups: [["Channel", 0, 3], ["Sends", 3, 5], ["LFO", 5, 8]] },
	};

	// A machine's SYN defaults from the OS's table (v20-hits.js), which its hit in the SYN screen was rendered with.
	const synDefaults = id => (typeof MD_SYN_DEFAULTS !== "undefined" && MD_SYN_DEFAULTS[id]) || defaults.slice(0, 8);
	const startKit = [16,17,18,19,20,21,22,23,24,25,26,27,28,32,33,38];
	const tracks = startKit.map((id, t) => ({ machine: id, level: [100,96,88,90,70,64,82,76,60,58,66,72,100,92,84,80][t], mute: t === 12, solo: false,
		p: [...synDefaults(id), ...defaults.slice(8)], lfo: { track: t, param: 12, shape1: 0, shape2: 0, mode: 0 } }));
	tracks[0].p = [...synDefaults(16), 0,0,64,64,18,70,40,0, 0,100,64,0,0,64,38,0];

	let sel = 0, showGrid = false, browserOpen = false, lastNote = null, onScale = null, uiScale = 1;
	let browserFrom = null, autoListen = true;
	// The LFO's target being chosen: "menu" open, or "assign" waiting for a knob.
	let lfoPick = null;
	const separated = new Set([0, 1]);
	const lastHit = tracks.map(() => null);
	const history = tracks.map(() => new Float32Array(80));

	// ---- master effects ----
	// The Machinedrum's four, in the order of their SysEx ids ($5D to $60), their parameters named as MCL names them
	// (MDParams.h): RHYTHM ECHO and GATE BOX take the tracks' DEL and REV sends (a track on its own output sends too),
	// EQ and DYNAMIX are on Main. 8 values each, the Machinedrum's own: Kit 1 as OS 1.63 boots, read by
	// mdEditorFirmwareTest ("MD master effects of Kit 1"), which are also where a knob recedes.
	const fxList = [
		{ key: "echo", title: "RHYTHM ECHO", role: "Delay", feed: "Tracks send with DEL", screen: "Echoes",
			names: ["TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"], groups: [["Delay", 0, 4], ["Filter", 4, 6], ["Output", 6, 8]], bipolar: [] },
		{ key: "reverb", title: "GATE BOX", role: "Reverb", feed: "Tracks send with REV", screen: "Tail",
			names: ["DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"], groups: [["Reverb", 0, 4], ["Filter", 4, 6], ["Gate", 6, 7], ["Output", 7, 8]], bipolar: [] },
		{ key: "eq", title: "EQ", role: "Equaliser", feed: "On Main", screen: "Response",
			names: ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"], groups: [["Low", 0, 2], ["High", 2, 4], ["Peak", 4, 7], ["Output", 7, 8]], bipolar: [1, 3, 5] },
		{ key: "dyn", title: "DYNAMIX", role: "Compressor", feed: "On Main, after the EQ", screen: "Transfer",
			names: ["ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"], groups: [["Envelope", 0, 2], ["Compression", 2, 5], ["Filter", 5, 6], ["Output", 6, 8]], bipolar: [] },
	];
	const fxDefaults = { echo: [16, 0, 32, 27, 0, 44, 0, 71], reverb: [0, 0, 68, 50, 1, 82, 71, 109], eq: [64, 64, 64, 64, 64, 64, 64, 127], dyn: [127, 127, 127, 127, 127, 127, 0, 0] };
	const cloneFx = m => Object.fromEntries(Object.entries(m).map(([k, v]) => [k, v.slice()]));
	const master = cloneFx(fxDefaults);

	// ---- kits ----
	// The plug-in's kit bank: 64 slots, as a Machinedrum project holds them, each empty or holding a kit: its name (up to
	// 16 characters, as on the Machinedrum) and its 16 tracks. The kit played came from slot kitLoaded under the name
	// kitName; kitEdited says it changed since it was loaded or saved.
	const snapshot = list => list.map(tr => ({ ...tr, p: tr.p.slice(), lfo: { ...tr.lfo } }));
	const kitOf = machines => machines.map((id, t) => ({ machine: id, level: 100, mute: false, solo: false,
		p: [...synDefaults(id), ...defaults.slice(8)], lfo: { track: t, param: 12, shape1: 0, shape2: 0, mode: 0 } }));
	// The mockup's kits: the first kit's instruments (BD, SD, CH...) taken from one family where it has them, or from a
	// family per track in turn.
	const instrument = id => byId.get(id).name.slice(-2);
	const fromFamily = (family, t) => { const id = startKit[t], m = MD_MACHINES.find(x => x.family === family && offered(x) && x.name.endsWith(instrument(id))); return m ? m.id : id; };
	const kitFamily = family => kitOf(startKit.map((_, t) => fromFamily(family, t)));
	const kitMixed = k => kitOf(startKit.map((_, t) => fromFamily(["TRX", "EFM", "E12", "P-I"][(t + k) % 4], t)));
	const kits = Array(64).fill(null);
	kits[0] = { name: "Dub techno", tracks: snapshot(tracks), master: cloneFx(master) };
	kits[1] = { name: "Sampled kit", tracks: kitFamily("E12") };
	kits[2] = { name: "FM bells", tracks: kitFamily("EFM") };
	kits[3] = { name: "Physical", tracks: kitFamily("P-I") };
	kits[4] = { name: "UW loops", tracks: kitOf(startKit.map((id, t) => t < 8 ? 152 + t : id)) };
	[[5, "Broken beat"], [6, "Minimal clicks"], [7, "Industrial"], [8, "Electro"], [9, "Jungle breaks"], [16, "Deep house"],
		[17, "Noise toys"], [32, "Rimshot jam"]].forEach(([slot, name], k) => kits[slot] = { name, tracks: kitMixed(k) });
	let kitOpen = false, kitSel = 0, kitLoaded = 0, kitName = kits[0].name, kitEdited = false, kitRenaming = false;
	// A click that would lose something waits for a second: "load" (changes not saved), "savehere" (a filled slot),
	// "delete". kitHint says what the last action did.
	let kitConfirm = null, kitHint = "", imported = 0;

	const machineOf = t => byId.get(tracks[t].machine) || { name: "?", family: "GND", syn: Array(8).fill("") };
	const names = t => [...machineOf(t).syn, ...efxNames, ...routeNames];
	const model = m => m.name === "GND---" ? "none" : m.name.slice(m.family.length + 1);

	function amplitude(t, now) {
		const hit = lastHit[t], tr = tracks[t];
		if(!hit) return 0;
		const anySolo = tracks.some(x => x.solo);
		if(tr.mute || (anySolo && !tr.solo)) return 0;
		const name = machineOf(t).name;
		let decay = 0.04 + tr.p[1] / 127 * 0.9;
		if(/CH|HH|RS|CL|SH/.test(name)) decay *= 0.25;
		else if(/OH|CY|RC|CC|BR/.test(name)) decay *= 1.5;
		let a = hit.velocity / 127 * tr.level / 127 * Math.exp(-(now - hit.time) / 1000 / decay);
		if(/CH|HH|OH|CY|SN|NS|SD|CP/.test(name)) a *= 0.75 + 0.25 * Math.random();
		return a < 0.003 ? 0 : a;
	}

	// A flat knob filling its column: a 270 degree track, the value as an arc, a pointer. At its default it recedes (no
	// arc, a dim pointer); edited, its arc and pointer are ink. A bipolar parameter (64 is neutral) draws its arc from the
	// top, its centre, towards the value.
	function knobSvg(size, value, def = value, bipolar = false) {
		const r = size / 2 - 3, c = size / 2, a0 = 0.75 * Math.PI, sweep = 1.5 * Math.PI, at = v => a0 + sweep * v / 127;
		const p = (t, rr) => [c + rr * Math.cos(t), c + rr * Math.sin(t)];
		const arc = (from, to) => { const [f, t] = from <= to ? [from, to] : [to, from], [x0, y0] = p(f, r), [x1, y1] = p(t, r); return `M${x0} ${y0} A${r} ${r} 0 ${t - f > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
		const a = at(value), from = bipolar ? at(64) : a0, edited = value !== def;
		const [x0, y0] = p(a, r * 0.35), [x1, y1] = p(a, r - 5);
		return `<svg width="${size}" height="${size}" aria-hidden="true"><path d="${arc(a0, a0 + sweep)}" stroke="var(--track)" stroke-width="2" fill="none"/>
			${edited && a !== from ? `<path d="${arc(from, a)}" stroke="var(--ink)" stroke-width="3" fill="none"/>` : ""}
			<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}" stroke="${edited ? "var(--ink)" : "var(--ink-dim)"}" stroke-width="2"/></svg>`;
	}
	// The parameters whose 64 is neutral: EQG, PAN, and a sample's pitch (E12 and ROM's PTCH).
	const bipolar = (i, m) => i === 11 || i === 18 || (i < 8 && m.syn[i] === "PTCH" && (m.family === "E12" || m.family === "ROM"));
	const defaultOf = (t, i) => i < 8 ? synDefaults(tracks[t].machine)[i] : defaults[i];
	const playSvg = (w, h) => `<svg width="${w}" height="${h}" viewBox="0 0 14 16" aria-hidden="true"><path d="M2 1 L13 8 L2 15 Z"/></svg>`;

	// M, S, and in MIX the track's own output (Out 01 on: the track leaves Main for its own output).
	const toggleText = { mute: ["Mute", () => "M"], solo: ["Solo", () => "S"], out: ["Own output", i => "Out " + two(i + 1)] };
	const toggle = (kind, i, on, style, title = "") => `<div class="cell toggle ${kind}${on ? " on" : ""}" id="${pid(i, kind)}" style="${style}" data-${kind}="${i}" tabindex="0" role="switch" aria-checked="${on}"
		aria-label="${toggleText[kind][0]}, track ${two(i + 1)}"${title ? ` title="${title}"` : ""}>${toggleText[kind][1](i)}</div>`;

	function top(view) {
		const tabs = [["TRACK", "Track"], ["MIX", "Mix"], ["MASTER", "Master"]].map(([n, text], k) => `<div class="cell tab${n === view ? " on" : ""}" style="${box(4 + k, 1, 0, 56)}" data-view="${n}" tabindex="0" role="tab" aria-selected="${n === view}"><span>${text}</span></div>`).join("");
		// The window's size, named: a click steps 100, 125, 150 %.
		return `<div class="cell logo" style="${box(1, 3, 0, 56)}">MD<span>Drums</span></div>${tabs}
			<div class="cell size inset num" style="${box(12, 3, 16, 24)}" data-scale tabindex="0" title="Window size: click for 100, 125 or 150%"><span class="label">Size</span>${Math.round(uiScale * 100)}%</div>
			<div class="cell" style="${box(16, 1, 16, 16)}"><span class="label">Main</span></div>
			<div class="cell outbars" style="${box(16, 1, 32, 8)}" data-meter><i><b></b></i><i><b></b></i></div>
			<div class="rule" style="${raw(0, WIN, 55, 1)}"></div>`;
	}

	// The kit played, over "Tracks" on the caption row (y 52-76): its slot and name open the kits; Save, at the track
	// list's right end (over S), keeps its changes in its slot, lit when there are some.
	const kitRow = () => `<div class="cell kitbar${kitOpen ? " open" : ""}" style="${raw(X(1), 168, 68, 24)}" data-kitopen tabindex="0" title="${kitEdited ? `${kitName}, changed since saved. ` : ""}Kits: load, save, rename; import and export Machinedrum kits (.syx)"><span class="label">Kit</span><span class="num">${two(kitLoaded + 1)}</span><span class="kitn">${kitName} ▾</span></div>
		<div class="cell kitsave r${kitEdited ? " on" : ""}" style="${raw(X(1) + 176, 40, 68, 24)}" data-kitsave${kitEdited ? ` tabindex="0" title="Save the changes in slot ${two(kitLoaded + 1)} (Ctrl+S)"` : ` title="Nothing to save: the kit is as slot ${two(kitLoaded + 1)} holds it"`}>Save</div>`;

	// The track list on columns 1-3, its rows laid out on multiples of 4: the number at x 16, the machine right after it
	// (x 48), its scope (x 136-168), then M and S as a pair 8 px apart, 8 px inside column 3's end (x 176-232). The
	// shown track's row is lit to the gutter's middle (x 248), its mark a 4 px bar at the window's edge.
	function trackList(head) {
		const rows = tracks.map((tr, i) => {
			const y = 160 + 40 * i, on = i === sel;
			return `${on ? `<div class="bg surface" style="${raw(0, 248, y, 40)}"></div><div class="bg selbar" style="${raw(0, 4, y + 8, 24)}"></div>` : ""}
			<div class="cell tn${on ? " tsel" : ""}" style="${raw(16, 24, y + 8, 24)}" data-playt="${i}" tabindex="0" aria-label="Play track ${two(i + 1)}" title="Play">${two(i + 1)}</div>
			<div class="cell scope" style="${raw(136, 32, y + 8, 24)}"><canvas data-scope="${i}"></canvas></div>
			<div class="cell tm${on ? " tsel" : ""}" style="${raw(48, 84, y + 8, 24)}" data-pick="${i}" tabindex="0">${byId.get(tr.machine).name}</div>
			${toggle("mute", i, tr.mute, raw(176, 24, y + 8, 24))}${toggle("solo", i, tr.solo, raw(208, 24, y + 8, 24))}`;
		}).join("");
		return `${kitRow()}<div class="cell r24" style="${box(1, 3, 108, 24)}"><span class="label">Tracks</span></div>${rows}`;
	}

	// The head band over the editing columns 4-16, y 40-120. Its rows: the caption (y 56-72) and the machine's name
	// (y 72-104) on columns 4-7; what the machine is and where the track goes on columns 12-16, inset as the screens'
	// titles under them are. Two choices, independent of each other (the frame's data-arrows and data-play):
	// arrows: "ends" ‹ › at the two ends of the name's field; "stepper" ▲ ▼ docked at its right end; "caption" small
	// ‹ › on the caption line, which step through the tracks (the user's choice); "wheel" none (scroll or ↑ ↓ on the
	// name: in every version the machine steps that way). play: "key" a ▶ key on column 16 with its
	// velocity on column 15; "screen" the hit screen plays; "list" no control here, the list's numbers and Space.
	function head(arrows, play) {
		const m = machineOf(sel), own = separated.has(sel);
		const infoCols = play === "key" ? 3 : 5;
		const name = (style, cls = "drop") => `<div class="cell machine ${cls}" style="${style}" id="${pid(sel, "machine")}" data-browse tabindex="0" aria-label="Choose a machine" title="Click to browse the machines; scroll or ↑ ↓ for the next one">${m.name}</div>`;
		const step = (d, style, cls, glyph) => `<div class="cell step ${cls}" style="${style}" data-step="${d}" tabindex="0" aria-label="${d < 0 ? "Previous" : "Next"} machine">${glyph}</div>`;
		let html = `<div class="cell r24" style="${box(4, arrows === "caption" ? 1 : 4, 68, 24)}"><span class="label num">Track ${two(sel + 1)}</span></div>`;
		if(arrows === "ends") {
			html += `<div class="cell picker" style="${box(4, 4, 92, 40)}"></div>${step(-1, raw(X(4), 32, 92, 40), "edge", "‹")}
				${name(raw(X(4) + 32, W(4) - 64, 92, 40) + ";justify-content:center", "inpicker")}${step(1, raw(X(7) + 32, 32, 92, 40), "edge right", "›")}`;
		} else if(arrows === "stepper") {
			html += `<div class="cell field" style="${box(4, 4, 92, 40)}"></div>${name(raw(X(4), W(4) - 32, 92, 40), "")}
				<div class="cell stepper" style="${raw(X(7) + 40, 24, 100, 32)}"><span data-step="-1" tabindex="0" aria-label="Previous machine">▲</span><span data-step="1" tabindex="0" aria-label="Next machine">▼</span></div>`;
		} else if(arrows === "caption") {
			// The user's reading (2026-10-07): ‹ › next to TRACK 01 step through the tracks, not the machines; the
			// machine changes from its name (browser, scroll, ↑ ↓). They start on column 5, right after the caption.
			const trackStep = (d, x, glyph) => `<div class="cell step small" style="${raw(x, 24, 68, 24)}" data-trackstep="${d}" tabindex="0" aria-label="${d < 0 ? "Previous" : "Next"} track" title="${d < 0 ? "Previous" : "Next"} track">${glyph}</div>`;
			html += `${trackStep(-1, X(5), "‹")}${trackStep(1, X(5) + 24, "›")}${name(box(4, 4, 92, 40))}`;
		} else {
			html += name(box(4, 4, 92, 40));
		}
		html += `<div class="cell meta inset kindline r24" style="${box(12, infoCols, 68, 24)}"><span><span class="code">${m.family}</span>${families[m.family]}</span></div>
			<div class="cell meta inset r24" style="${box(12, infoCols, 108, 24)}" title="The track's output, chosen in Mix"><span>Plays on <b>${own ? "Out " + two(sel + 1) : "Main"}</b></span></div>`;
		if(play === "key")
			html += `<div class="cell r24" style="${box(15, 1, 68, 24)}"><span class="label">Velocity</span></div>
				<div class="cell velv" style="${box(15, 1, 96, 32)}" data-auditionvel tabindex="0" role="slider" aria-label="Play velocity" aria-valuemin="1" aria-valuemax="127" aria-valuenow="${auditionVelocity}" title="Drag up or down: the velocity ▶ plays with">${auditionVelocity}</div>
				<div class="bg velbar" style="${box(15, 1, 128, 4)}"><b style="width:${Math.round(auditionVelocity / 127 * 64)}px"></b></div>
				<div class="cell playkey" style="${box(16, 1, 68, 64)}" data-audition tabindex="0" role="button" aria-label="Play track ${two(sel + 1)}" title="Play track ${two(sel + 1)}">${playSvg(18, 20)}</div>`;
		return html;
	}
	let auditionVelocity = 100;

	// The LFO's two shapes (LFOM mixes the first into the second) and its mode, a row each at the foot of its screen.
	const shapeRows = t => `<div class="lfoshapes">${[1, 2].map(row => {
		const current = t.lfo[`shape${row}`];
		return `<div class="srow" id="${pid(sel, `lfoShape${row}`)}" role="radiogroup" aria-label="LFO shape ${row}"><span class="label" title="LFOM mixes shape 1 into shape 2">Shape ${row}</span>${shapes.map((s, i) =>
			`<div class="chip${i === current ? " on" : ""}" data-shape${row}="${i}" tabindex="0" role="radio" aria-checked="${i === current}">${s}</div>`).join("")}</div>`;
	}).join("")}<div class="srow" id="${pid(sel, "lfoMode")}" role="radiogroup" aria-label="LFO mode"><span class="label" title="FREE runs on; TRIG restarts at each hit; HOLD keeps its value from one hit to the next">Mode</span>${modes.map((s, i) =>
		`<div class="chip${i === t.lfo.mode ? " on" : ""}" data-mode="${i}" tabindex="0" role="radio" aria-checked="${i === t.lfo.mode}">${s}</div>`).join("")}</div></div>`;

	// The LFO's target as the ROUTING screen's title says it: "FLTF on track 01".
	const lfoTargetText = t => { const l = t.lfo; return `${names(l.track)[l.param] || "—"} on track ${two(l.track + 1)}`; };

	// ---- the LFO screen, four ways (a frame's data-lfo) ----
	// "chips" (v22): the title, a 56 px plot, three rows of chips (shape 1, shape 2, mode).
	// "side": the plot on the left (x 0-188 inside the screen, 136 px tall), the settings on the right (x 204-368): two
	// rows of six drawn shapes and the mode, every choice one click away.
	// "fields": the Machinedrum's own LFO page: five fields at the foot (track, parameter, shape 1, shape 2, mode); a
	// click opens its list, a drag or the wheel steps it; the plot over them (104 px).
	// "mix": shape 1 at the foot's left, shape 2 at its right, LFOM between them as it mixes the one into the other;
	// the plot draws both shapes faint behind their mix; the mode a menu in the title.
	let lfoStyle = "chips", lfoPop = null;
	const shapePaths = ["M0 6 L5 1 L15 11 L20 6", "M0 1 L19 11 L19 1", "M0 11 L0 1 L10 1 L10 11 L20 11 L20 1", "M0 11 L19 1 L19 11",
		"M1 1 Q3 11 20 11", "M0 7 H4 V2 H8 V10 H12 V4 H16 V8 H20"];
	const shapeIcon = s => `<svg width="20" height="12" viewBox="-1 -1 22 14" aria-hidden="true"><path d="${shapePaths[s]}" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>`;
	const lfoFieldMax = { track: 15, param: 23, shape1: 5, shape2: 5, mode: 2 };
	const lfoFieldKey = { track: "track", param: "param", shape1: "shape1", shape2: "shape2", mode: "mode" };
	function lfoScreen(t) {
		const l = t.lfo, assigning = lfoPick === "assign", menu = lfoPick === "menu" ? lfoMenu(t) : "";
		const label = `<span class="label code">LFO</span>`;
		const target = `<span id="${pid(sel, "lfoTarget")}" data-lfomenu tabindex="0" title="Choose the track and the parameter it modulates (${machineOf(l.track).name})">${assigning ? "Click a knob to modulate" : lfoTargetText(t) + " ▾"}</span>`;
		const assign = `<span class="assign${assigning ? " on" : ""}" data-lfoassign tabindex="0" title="${assigning ? "Stop choosing a knob (Esc)" : "Click any knob, on this track or another, to modulate it"}">${assigning ? "Cancel" : "Assign"}</span>`;
		const modeTip = "FREE runs on; TRIG restarts at each hit; HOLD keeps its value from one hit to the next";
		const field = (key, name, value, title) => `<div class="lfofield${lfoPop === key ? " open" : ""}" data-lfofield="${key}" tabindex="0" title="${title}: click for the list; drag or scroll for the next"><span class="label">${name}</span><span class="fv">${value}<i>▾</i></span></div>`;
		const pop = !lfoPop || lfoPop === "track" || lfoPop === "param" ? "" : `<div class="lfopop${lfoPop === "mode" && lfoStyle === "mix" ? " top" : ""}">${(lfoPop === "mode" ? modes : shapes).map((s, i) =>
			`<div class="chip${l[lfoPop] === i ? " on" : ""}" data-${lfoPop}="${i}" tabindex="0">${lfoPop === "mode" ? "" : shapeIcon(i)}${s}</div>`).join("")}</div>`;
		if(lfoStyle === "side") {
			const icons = row => `<div class="iconrow" role="radiogroup" aria-label="LFO shape ${row}">${shapes.map((s, i) => `<div class="ico${i === l["shape" + row] ? " on" : ""}" data-shape${row}="${i}" tabindex="0" role="radio" aria-checked="${i === l["shape" + row]}" title="${s}">${shapeIcon(i)}</div>`).join("")}</div>`;
			const modeRow = `<div class="iconrow mode" role="radiogroup" aria-label="LFO mode">${modes.map((s, i) => `<div class="chip${i === l.mode ? " on" : ""}" data-mode="${i}" tabindex="0" role="radio" aria-checked="${i === l.mode}">${s}</div>`).join("")}</div>`;
			return { name: "lfo", title: label + target + `<span class="note">${assign}</span>`,
				extra: `<div class="lfoside"><div class="sgroup"><span class="label" title="LFOM mixes shape 1 into shape 2">Shape 1</span>${icons(1)}</div><div class="sgroup"><span class="label">Shape 2</span>${icons(2)}</div><div class="sgroup"><span class="label" title="${modeTip}">Mode</span>${modeRow}</div></div>${menu}` };
		}
		if(lfoStyle === "fields") {
			const fields = field("track", "Track", two(l.track + 1), "The track it modulates") + field("param", "Parameter", names(l.track)[l.param] || "—", "The parameter it modulates")
				+ field("shape1", "Shape 1", shapes[l.shape1], "Its first shape; LFOM mixes it into the second") + field("shape2", "Shape 2", shapes[l.shape2], "Its second shape")
				+ field("mode", "Mode", modes[l.mode], modeTip);
			return { name: "lfo", title: label + (assigning ? `<span class="hint">Click a knob to modulate</span>` : "") + `<span class="note">${assign}</span>`,
				extra: `<div class="lfofields">${fields}</div>${pop}${menu}` };
		}
		if(lfoStyle === "mix") {
			const mode = `<span class="lfofield inline${lfoPop === "mode" ? " open" : ""}" data-lfofield="mode" tabindex="0" title="${modeTip}: click for the list; drag or scroll for the next">${modes[l.mode]} ▾</span>`;
			const mixAt = Math.round(t.p[23] / 127 * 100);
			const row = `<div class="lfomix">${field("shape1", "Shape 1", shapeIcon(l.shape1) + shapes[l.shape1], "Its first shape")}
				<div class="mixbar" title="LFOM ${t.p[23]} mixes shape 1 into shape 2 (its knob is in ROUTING)"><span class="label">LFOM ${t.p[23]}</span><b><i style="left:${mixAt}%"></i></b></div>
				${field("shape2", "Shape 2", shapeIcon(l.shape2) + shapes[l.shape2], "Its second shape")}</div>`;
			return { name: "lfo", title: label + target + `<span class="note">${mode}${assign}</span>`, extra: row + pop + menu };
		}
		return { name: "lfo", title: label + target + `<span class="note">${assign}</span>`, extra: shapeRows(t) + menu };
	}

	function pages(play) {
		const t = tracks[sel], n = names(sel), m = machineOf(sel);
		// The LFOs that modulate a parameter of the shown track (any track's LFO, with some depth), named under its knob.
		const modulators = i => tracks.map((tr, k) => k).filter(k => tracks[k].lfo.track === sel && tracks[k].lfo.param === i && tracks[k].p[22] > 0);
		const band = (k, cls, title, screen) => {
			const y = BAND[k], L = layouts[cls];
			// A band's rows, its 160 px of content centred in its 216: title (y + 28), groups (+ 60), knobs (+ 84),
			// labels (+ 156), values (+ 172).
			const groups = L.groups.map(([gname, a, b, pending]) => `<div class="cell grp${b - a === 1 ? " one" : ""}${gname ? "" : " sym"}${pending ? " pending" : ""}" style="${box(4 + a, b - a, y + 60, 16)}"${pending ? ` title="Master effects to come"` : ""}>${gname}</div>`).join("");
			const knobs = L.order.map((i, pos) => {
				const unused = (i < 8 && !n[i]) || sendsPending.has(i);
				const why = i < 8 && !n[i] ? `${m.name} does not use this parameter` : sendsPending.has(i) ? "Master effects to come" : "";
				const label = n[i] || "—", cls2 = unused ? " unused" : "", c = 4 + pos;
				const target = lfoPick === "assign" && !unused ? " target" : "", mods = modulators(i);
				const modTitle = mods.length ? ` title="Modulated by ${mods.map(k => "LFO " + two(k + 1)).join(", ")}"` : "";
				const def = defaultOf(sel, i), edited = t.p[i] !== def ? " edited" : "";
				return `<div class="cell knob${cls2}${target}" style="${box(c, 1, y + 84, 64)}" id="${pid(sel, specNames[i])}" data-i="${unused ? "" : i}"${unused ? ` title="${why}"` : ` tabindex="0" role="slider" aria-label="${label}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${t.p[i]}" title="${knobTip}"`}>${knobSvg(64, t.p[i], def, bipolar(i, m))}</div>
					<div class="cell kl${mods.length ? " lfo" : ""}${cls2}" style="${box(c, 1, y + 156, 16)}"${why ? ` title="${why}"` : modTitle}><span class="label">${label}</span></div>
					<div class="cell kv value${cls2}${edited}" style="${box(c, 1, y + 172, 16)}">${unused && i < 8 ? "" : t.p[i]}</div>`;
			}).join("");
			return `<div class="cell btitle" style="${box(4, 3, y + 28, 24)}">${title}</div>${groups}${knobs}
				<div class="cell screen${screen.cls || ""}" style="${raw(X(12), WIN - X(12), y, BAND_H)}" data-screen="${screen.name}"${screen.attrs || ""}><canvas></canvas><div class="stitle">${screen.title}</div>${screen.extra || ""}</div>`;
		};
		const plays = play === "screen";
		// The LFO's target, two ways: its name opens the menu, which takes the whole screen; ASSIGN waits for a click on
		// any knob, on this track or on another chosen in the list (lfoScreen).
		// A rule over every band: SYN, EFX and ROUTING are framed alike, and so are their screens (the last ends at the window's edge).
		// Each rule spans the editing frame, as its surface does: from the gutter's middle (x 248) to the window's edge;
		// the head band's runs across the whole window.
		const rules = BAND.map((y, k) => `<div class="rule" style="${k ? raw(248, WIN - 248, y - 1, 1) : raw(0, WIN, y - 1, 1)}"></div>`).join("");
		return rules + band(0, "syn", "SYN", { name: "hit", cls: plays ? " plays" : "", attrs: plays ? ` data-audition tabindex="0" role="button" aria-label="Play track ${two(sel + 1)}" title="Click to play"` : "",
				title: `${plays ? `<span class="playglyph">${playSvg(10, 12)}</span>` : ""}<span class="label">Hit</span><span class="note hint">${
					lastHit[sel] ? `Last hit, velocity <span class="num" data-vel>${lastHit[sel].velocity}</span>` : plays ? "Click to play" : "Not played yet"}</span>` })
			+ band(1, "efx", "EFX", { name: "filter", title: `<span class="label">Filter and EQ</span>${t.p[15] > 0 ? `<span class="note hint num">SRR ${t.p[15]}</span>` : ""}` })
			+ band(2, "rte", "ROUTING", lfoScreen(t));
	}

	// The target menu takes the LFO's whole screen: its own title (Done closes it), the 16 tracks with their machines,
	// then the chosen track's 24 parameters by their names on its machine, a row per band.
	function lfoMenu(t) {
		const l = t.lfo, list = names(l.track);
		// The tracks in two rows of 8 after the label column, as the parameters under them.
		const trackCells = tracks.map((tr, i) => `${i % 8 ? "" : `<span class="label">${i ? "" : "Track"}</span>`}<div class="mt${i === l.track ? " on" : ""}" data-lfotrack="${i}" tabindex="0" title="${byId.get(tr.machine).name}">${two(i + 1)}</div>`).join("");
		const group = (title, from) => `<div class="mg"><span class="label">${title}</span>${list.slice(from, from + 8).map((p, k) =>
			`<div class="mp${from + k === l.param ? " on" : ""}${p ? "" : " off"}" data-lfoparam="${from + k}" tabindex="0">${p || "—"}</div>`).join("")}</div>`;
		return `<div class="lfomenu"><div class="mhead"><span class="label">LFO ${two(sel + 1)} modulates</span><span class="hint">track ${two(l.track + 1)}, ${machineOf(l.track).name}</span>
				<span class="done" data-lfomenu tabindex="0" title="Close (Esc)">Done</span></div>
			<div class="mtracks">${trackCells}</div><div class="mparams">${group("SYN", 0)}${group("EFX", 8)}${group("ROUTING", 16)}</div></div>`;
	}

	function browser() {
		const cur = tracks[sel].machine, m = machineOf(sel), from = byId.get(browserFrom ?? cur);
		// Families on the editing columns: GND 4, TRX 5-6, EFM 7, E12 8-9, P-I 10; ROM's 32 samples on 12-16, over the
		// preview, whose screen is where ROUTING's is.
		const place = { GND: [4, 1], TRX: [5, 2], EFM: [7, 1], E12: [8, 2], "P-I": [10, 1], ROM: [12, 5] };
		const fams = familyOrder.map(f => {
			const list = MD_MACHINES.filter(x => x.family === f && offered(x)), [c, n] = place[f];
			const rows = Math.ceil(list.length / n);
			const items = list.map((x, k) => {
				// ROM fills its columns row by row; the others column by column
				const col = f === "ROM" ? k % n : Math.floor(k / rows), row = f === "ROM" ? Math.floor(k / n) : k % rows;
				return `<div class="cell mach${x.id === cur ? " cur" : ""}" style="${box(c + col, 1, 272 + 32 * row, 24)}" data-mach="${x.id}" tabindex="0" title="${x.name}">${model(x)}</div>`;
			}).join("");
			return `<div class="cell grp fam${n === 1 ? " one" : ""}" style="${box(c, n, 232, 16)}">${f}<span class="num">${list.length}</span></div>
				<div class="cell kind" style="${box(c, n, 248, 16)}" title="${kindTips[f] || families[f]}">${kinds[f]}</div>${items}`;
		}).join("");
		const keys = "Click: load and listen; ↑ ↓: next machine; ← →: next family; Enter: keep; Esc: cancel";
		return `<div class="cell bhead" style="${box(4, 8, 192, 24)}" title="${keys}"><b>Choose a machine</b><span>for track ${two(sel + 1)}</span><span data-bhint></span></div>
			<div class="cell bact r" style="${box(12, 5, 192, 24)}"><span data-cancel tabindex="0" title="Back to ${from.name} (Esc)">Cancel</span><span class="keep" data-keep tabindex="0" title="Keep ${m.name} (Enter)">Keep</span></div>
			${fams}
			<div class="cell grp fam" style="${box(12, 5, 512, 16)}">Preview</div>
			<div class="cell pvname" style="${box(12, 5, 536, 24)}"><b>${m.name}</b><span class="meta">${kinds[m.family]}</span></div>
			<div class="cell pvauto${autoListen ? " on" : ""}" style="${box(12, 5, 568, 16)}" data-autolisten tabindex="0" role="switch" aria-checked="${autoListen}"><i></i><span>Listen while choosing</span></div>
			<div class="cell screen" style="${raw(X(12), WIN - X(12), BAND[2], BAND_H)}" data-screen="preview"><canvas></canvas><div class="stitle"><span class="label">Hit</span><span class="note hint">${m.name}</span></div></div>`;
	}

	// The kits, over everything under the top bar: the 64 slots in four columns of 16 (01-16 on columns 1-3, then 4-6,
	// 7-9, 10-12), rows of 40 px as the track list's, the kit played marked with its accent bar; on columns 13-16, the
	// chosen slot: its name, its tracks' machines, what to do with it.
	function kitPanel() {
		const k = kits[kitSel], playing = kitSel === kitLoaded, nn = two(kitSel + 1);
		const slots = kits.map((kit, i) => {
			const c = 1 + 3 * Math.floor(i / 16), y = 160 + 40 * (i % 16);
			const from = c === 1 ? 0 : X(c) - 8;
			return `${i === kitSel ? `<div class="bg surface" style="${raw(from, X(c) + W(3) + 8 - from, y, 40)}"></div>` : ""}${i === kitLoaded ? `<div class="bg selbar" style="${raw(from, 4, y + 8, 24)}"></div>` : ""}
				<div class="cell slot${kit ? "" : " empty"}${i === kitSel ? " on" : ""}" style="${raw(X(c), W(3), y + 8, 24)}" data-kitslot="${i}" tabindex="0" title="${kit ? "Click to see it; double-click or Enter to play it" : "Empty: Save here puts the kit you play in it"}"><span class="num">${two(i + 1)}</span>${kit ? kit.name : "Empty"}</div>`;
		}).join("");
		const machines = k ? k.tracks.map((tr, t) => `<div class="cell kitm" style="${box(13 + 2 * Math.floor(t / 8), 2, 264 + 24 * (t % 8), 24)}"><span class="num">${two(t + 1)}</span>${byId.get(tr.machine).name}</div>`).join("") : "";
		const action = (name, text, title) => `<span class="${name}${kitConfirm === name ? " confirm" : ""}" data-kit${name}="${kitSel}" tabindex="0" title="${title}">${text}</span>`;
		// Asking for a second click, the row holds that action and Cancel only.
		const confirming = { load: "Load anyway", savehere: "Replace it", delete: "Delete it" };
		const actions = kitConfirm ? action(kitConfirm, confirming[kitConfirm], "Click again to go on") + `<span data-kitcancel tabindex="0" title="Esc">Cancel</span>` : [
			k && action("load", "Load", `Play this kit: its 16 tracks replace the ones you play (Enter)`),
			action("savehere", "Save here", k && !playing ? `Slot ${nn} holds ${k.name}: saving here replaces it` : `Save the kit you play in slot ${nn}`),
			k && action("rename", "Rename", "Type its name, up to 16 characters; Enter keeps it, Esc leaves it"),
			k && action("delete", "Delete", `Empty slot ${nn}`),
		].filter(Boolean).join("");
		const confirmText = { load: `Changes to ${kitName} will be lost`, savehere: `Replaces ${k ? k.name : ""} in slot ${nn}`, delete: `Empties slot ${nn}` };
		const note = kitConfirm ? confirmText[kitConfirm] : kitHint;
		return `${kitRow()}<div class="cell bhead" style="${box(1, 8, 108, 24)}"><b>Kits</b><span>64 slots, kept by the plug-in</span></div>
			<div class="cell bact r" style="${box(12, 5, 108, 24)}"><span data-kitimport tabindex="0" title="Open a .syx file of Machinedrum kits, one kit or a project's 64: they go into the empty slots from ${nn} on">Import .syx</span><span data-kitexport tabindex="0" title="Save slot ${nn} as a Machinedrum kit (.syx), for the machine or another set">Export .syx</span><span class="keep" data-kitopen tabindex="0" title="Close (Esc)">Done</span></div>
			<div class="rule" style="${raw(0, WIN, 159, 1)}"></div>${slots}
			<div class="bg sepline" style="${raw(X(13) - 8, 1, 160, 648)}"></div>
			<div class="cell r24" style="${box(13, 4, 168, 24)}"><span class="label num">Slot ${nn}${playing ? ", playing" : ""}</span></div>
			<div class="cell kitpname${k ? "" : " empty"}" style="${box(13, 4, 192, 32)}" data-kitname${kitRenaming ? ` contenteditable="true" spellcheck="false"` : ""}>${k ? k.name : "Empty"}</div>
			${k ? `<div class="cell grp" style="${box(13, 4, 240, 16)}">Tracks</div>${machines}` : ""}
			<div class="cell kact" style="${box(13, 4, 480, 24)}">${actions}</div>
			${note ? `<div class="cell hint" style="${box(13, 4, 512, 16)}">${note}</div>` : ""}`;
	}

	function loadKit(i) {
		if(!kits[i]) return;
		if(kitEdited && kitConfirm !== "load") { kitConfirm = "load"; renderAll(); return; }
		kits[i].tracks.forEach((tr, t) => tracks[t] = { ...tr, p: tr.p.slice(), lfo: { ...tr.lfo } });
		Object.assign(master, cloneFx(kits[i].master || fxDefaults));
		kitLoaded = i; kitName = kits[i].name; kitEdited = false; kitConfirm = null; kitHint = `Playing ${two(i + 1)}, ${kitName}`;
		lastHit.fill(null); renderAll();
	}
	function saveKit(i) {
		if(kits[i] && i !== kitLoaded && kitConfirm !== "savehere") { kitConfirm = "savehere"; renderAll(); return; }
		kits[i] = { name: kitName, tracks: snapshot(tracks), master: cloneFx(master) };
		kitLoaded = i; kitEdited = false; kitConfirm = null; kitHint = `Saved in slot ${two(i + 1)}`; renderAll();
	}
	function deleteKit(i) {
		if(kitConfirm !== "delete") { kitConfirm = "delete"; renderAll(); return; }
		kits[i] = null; if(i === kitLoaded) kitEdited = true;
		kitConfirm = null; kitHint = `Slot ${two(i + 1)} is empty`; renderAll();
	}
	// The mockup has no files: Import puts a kit as a .syx would bring it (its name as the machine shows it) into the
	// first empty slot from the chosen one on; Export says what it would write.
	function importKit() {
		let i = kitSel; while(i < 64 && kits[i]) ++i;
		if(i === 64) { kitHint = `No empty slot from ${two(kitSel + 1)} on`; renderAll(); return; }
		kits[i] = { name: `TECHNO ${++imported}`, tracks: kitMixed(imported + 3) };
		kitSel = i; kitConfirm = null; kitHint = `Imported 1 kit from techno.syx into slot ${two(i + 1)}`; renderAll();
	}
	function exportKit() {
		const k = kits[kitSel];
		kitHint = k ? `Exported slot ${two(kitSel + 1)} as ${k.name}.syx` : `Slot ${two(kitSel + 1)} is empty: nothing to export`;
		kitConfirm = null; renderAll();
	}
	function commitRename(el, keep) {
		if(!kitRenaming) return;
		kitRenaming = false;
		const text = el.textContent.replace(/\s+/g, " ").trim().slice(0, 16);
		if(keep && text && kits[kitSel]) { kits[kitSel].name = text; if(kitSel === kitLoaded) kitName = text; }
		renderAll();
	}

	// No bar of shortcuts: they are in the tooltips (knobTip on every knob, the browser's on its title).
	function trackView(arrows, play) {
		return `<div class="bg surface" style="${raw(248, WIN - 248, BAND[0], BAND[2] + BAND_H - BAND[0])}"></div>${trackList()}${head(arrows, play)}${browserOpen ? browser() : pages(play)}`;
	}
	const knobTip = "Drag up or down to adjust; Shift: fine; double-click: back to the default; scroll: one step";

	// MIX: one strip per column; a hairline in each gutter's middle; the shown track's strip lit to the gutters' middles.
	// At each strip's foot, under M and S, its output: Out 01 on takes the track out of Main to its own output.
	function mixView() {
		const strips = tracks.map((tr, i) => {
			const c = i + 1, on = i === sel;
			const from = c === 1 ? 0 : X(c) - 8, to = c === 16 ? WIN : X(c) + 72;
			return `${on ? `<div class="bg" style="${raw(from, to - from, 40, 768)};background:var(--chrome)"></div><div class="bg stripsel" style="${box(c, 1, 40, 4)}"></div>` : ""}
				${i ? `<div class="bg sepline" style="${raw(X(c) - 8, 1, 40, 768)}"></div>` : ""}
				<div class="cell c sn${on ? " on" : ""}" style="${box(c, 1, 68, 16)}" data-playt="${i}" tabindex="0" aria-label="Play track ${two(i + 1)}">${two(i + 1)}</div>
				<div class="cell c sm" style="${box(c, 1, 84, 24)}" data-strip="${i}">${byId.get(tr.machine).name}</div>
				<div class="cell pan" style="${box(c, 1, 116, 64)}" id="${pid(i, "PAN")}" data-mix="18" data-track="${i}" tabindex="0" role="slider" aria-label="PAN, track ${two(i + 1)}">${knobSvg(64, tr.p[18], 64, true)}</div>
				<div class="cell c" style="${box(c, 1, 180, 16)}"><span class="label">Pan</span></div>
				<div class="cell vu" style="${raw(X(c), 4, 212, 492)}"><b data-vu="${i}"></b></div>
				<div class="cell fader" style="${box(c, 1, 212, 492)}" id="${pid(i, "level")}" data-fader="${i}" tabindex="0" role="slider" aria-label="Kit level, track ${two(i + 1)}"><div class="rail"></div>
					<div class="capf" style="bottom:calc(${tr.level / 127 * 100}% - 4px)"></div></div>
				<div class="cell c value" style="${box(c, 1, 712, 16)}">${tr.level}</div>
				${toggle("mute", i, tr.mute, raw(X(c) + 4, 24, 736, 24))}${toggle("solo", i, tr.solo, raw(X(c) + 36, 24, 736, 24))}
				${toggle("out", i, separated.has(i), raw(X(c) + 4, 56, 768, 24), outputHelp(i))}`;
		}).join("");
		return `<div class="bg surface" style="${raw(0, WIN, 40, 768)}"></div>${strips}`;
	}

	// Live lists the plug-in's outputs by these names (Audio From: MD Drums, then Out 01): the processor's buses, "Track 1"
	// to "Track 16" today, are to be renamed to match.
	// MASTER: the four effects in two columns of two, ECHO and GATE BOX over EQ and DYNAMIX: each on 8 columns (1-8,
	// 9-16), a knob a column, in a cell of 272 px (y 40-312, 312-584), its content 28 px from the rules over and under it,
	// as in the Track tab's bands: the effect's name (y + 28), what it is and what feeds it (+ 60), its groups (+ 116), 8
	// knobs (+ 140), their names (+ 212) and values (+ 228). Under them, the 16 tracks' sends (y 584-808): a column a
	// track, its number and machine as in MIX, DEL and REV as two 28 px knobs side by side. Rules between the rows only:
	// every rule has 28 px of room on both sides, as in the Track tab; the two effects of a row are apart by a gutter,
	// as the Track tab's columns are. No screens (the user).
	const FX_H = 272, SENDS_Y = 40 + 2 * FX_H;
	function masterView() {
		const rules = `<div class="rule" style="${raw(0, WIN, 40 + FX_H - 1, 1)}"></div><div class="rule" style="${raw(0, WIN, SENDS_Y - 1, 1)}"></div>`;
		const effects = fxList.map((fx, k) => {
			const c0 = k % 2 ? 9 : 1, y = 40 + FX_H * Math.floor(k / 2), v = master[fx.key], def = fxDefaults[fx.key];
			const groups = fx.groups.map(([name, a, b]) => `<div class="cell grp${b - a === 1 ? " one" : ""}" style="${box(c0 + a, b - a, y + 116, 16)}">${name}</div>`).join("");
			const knobs = fx.names.map((name, i) => {
				const c = c0 + i, edited = v[i] !== def[i] ? " edited" : "";
				return `<div class="cell knob" style="${box(c, 1, y + 140, 64)}" id="m_${fx.key}_${name}" data-fx="${fx.key}" data-k="${i}" tabindex="0" role="slider" aria-label="${fx.title} ${name}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${v[i]}" title="${knobTip}">${knobSvg(64, v[i], def[i], fx.bipolar.includes(i))}</div>
					<div class="cell kl" style="${box(c, 1, y + 212, 16)}"><span class="label">${name}</span></div>
					<div class="cell kv value${edited}" style="${box(c, 1, y + 228, 16)}">${v[i]}</div>`;
			}).join("");
			return `<div class="cell btitle" style="${box(c0, 8, y + 28, 24)}">${fx.title}</div>
				<div class="cell meta fxrole" style="${box(c0, 8, y + 60, 24)}">${fx.role}, ${fx.feed.charAt(0).toLowerCase() + fx.feed.slice(1)}</div>
				${groups}${knobs}`;
		}).join("");
		// The sends, 28 px from the rule over them and from the window's foot: a title row (y + 28), then per track its
		// number (+ 76) and machine (+ 92), DEL and REV (+ 128), their names (+ 164) and values (+ 180).
		const y = SENDS_Y;
		const sends = tracks.map((tr, t) => {
			const c = t + 1;
			const send = (i, dx, name) => `<div class="cell knob" style="${raw(X(c) + dx, 28, y + 128, 28)}" id="${pid(t, name)}" data-mix="${i}" data-track="${t}" tabindex="0" role="slider" aria-label="${name}, track ${two(t + 1)}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${tr.p[i]}" title="${name}, track ${two(t + 1)}: ${knobTip}">${knobSvg(28, tr.p[i], defaults[i])}</div>
				<div class="cell c kl" style="${raw(X(c) + dx, 28, y + 164, 16)}"><span class="label">${name}</span></div>
				<div class="cell c kv value${tr.p[i] !== defaults[i] ? " edited" : ""}" style="${raw(X(c) + dx, 28, y + 180, 16)}">${tr.p[i]}</div>`;
			return `<div class="cell c sn${t === sel ? " on" : ""}" style="${box(c, 1, y + 76, 16)}" data-playt="${t}" tabindex="0" aria-label="Play track ${two(t + 1)}">${two(t + 1)}</div>
				<div class="cell c sm" style="${box(c, 1, y + 92, 24)}">${byId.get(tr.machine).name}</div>
				${send(19, 0, "DEL")}${send(20, 36, "REV")}`;
		}).join("");
		return `<div class="bg surface" style="${raw(0, WIN, 40, 2 * FX_H + 224)}"></div>${rules}${effects}
			<div class="cell btitle" style="${box(1, 3, y + 28, 24)}">Sends</div>
			<div class="cell meta fxrole" style="${box(4, 13, y + 28, 24)}">DEL feeds RHYTHM ECHO, REV feeds GATE BOX; a track on its own output sends too</div>
			${sends}`;
	}

	const outputHelp = i => separated.has(i)
		? `Track ${two(i + 1)} plays on its own output, Out ${two(i + 1)} (after its effects and VOL, before PAN), and leaves Main. In Live: an audio track, Audio From "MD Drums", then "Out ${two(i + 1)}". Click to send it back to Main.`
		: `Track ${two(i + 1)} plays on Main. Click to give it its own output, Out ${two(i + 1)}.`;

	// ---- screens ----
	function canvasOf(frame, name) {
		const c = frame.querySelector(`[data-screen="${name}"] canvas`);
		if(!c) return [null];
		const w = c.clientWidth, h = c.clientHeight;
		c.width = w * 2; c.height = h * 2;
		// A screen runs to the window's edge; its plot is inset 16 px from its own edges: from column 12's start and from
		// the window's edge (x 1280, column 16's end), where its title starts and ends.
		const g = c.getContext("2d"); g.scale(2, 2); g.translate(16, 0);
		return [g, w - 32, h];
	}
	const cssOf = frame => { const ui = frame.querySelector(".ui"); return n => getComputedStyle(ui).getPropertyValue(n).trim(); };
	// Under the title (16-40), 8 px; over the axis' words, whose baseline is 16 px from the screen's foot, 8 px.
	const plot = h => ({ top: 48, bottom: h - 36 });
	function graduation(g, css, x0, y0, x1, y1, text, tx, ty) {
		g.strokeStyle = css("--tick"); g.lineWidth = 1;
		g.beginPath(); g.moveTo(Math.round(x0) + 0.5, Math.round(y0) + 0.5); g.lineTo(Math.round(x1) + 0.5, Math.round(y1) + 0.5); g.stroke();
		if(text) { g.fillStyle = css("--ink-faint"); g.font = `12px ${css("--font")}`; g.fillText(text, tx, ty); }
	}

	// The shown machine's real hit (v22-hits.js: min/max over 384 columns of 0.8 s, rendered by the engine at its SYN
	// defaults, velocity 100), reshaped by the SYN parameters this mockup can model, where the plug-in will re-render the
	// hit with a second engine (22-27 ms a hit): DEC stretches the amplitude envelope (a doubling per 16 steps from the
	// default), HOLD holds its peak (up to 0.4 s at 127), PTCH changes the rate (a sample's playback; a synthesis
	// machine's oscillation, under the same envelope; an octave per 24 steps), STRT and END cut a sample; the velocity
	// and VOL scale it. The other SYN parameters leave it as recorded. A hit is split once into its envelope (an instant
	// attack, a 30 ms release) and its carrier (the waveform over that envelope), whose last period repeats where a
	// longer envelope outlasts the recording.
	const HIT_S = 0.8, hitWindows = [0.1, 0.2, 0.3, 0.4, 0.6, 0.8], tickMs = { 0.1: 25, 0.2: 50, 0.3: 100, 0.4: 100, 0.6: 200, 0.8: 200 };
	const hitShapes = new Map();
	function hitShape(id) {
		if(hitShapes.has(id)) return hitShapes.get(id);
		const data = (typeof MD_HITS !== "undefined" && MD_HITS[id]) || null;
		let shape = null;
		if(data) {
			const n = data.length / 2, env = new Float32Array(n), release = Math.exp(-1 / (n * 0.03 / HIT_S));
			let peak = 0, peakAt = 0;
			for(let k = 0, e = 0; k < n; ++k) {
				e = Math.max(Math.abs(data[2 * k]), Math.abs(data[2 * k + 1]), e * release); env[k] = e;
				if(e > peak) { peak = e; peakAt = k; }
			}
			if(peak > 0) {
				const lo = new Float32Array(n), hi = new Float32Array(n);
				let last = 0, end = 0;
				for(let k = 0; k < n; ++k) {
					if(env[k] > 1e-6) { lo[k] = data[2 * k] / env[k]; hi[k] = data[2 * k + 1] / env[k]; }
					if(env[k] > 0.03 * peak) last = k;
					if(env[k] > 0.001 * peak) end = k;
				}
				// The carrier's period near its end: the lag (4 to 100 ms) its upper edge repeats best with.
				let period = 8, best = -Infinity;
				const from = Math.floor(last / 2);
				for(let lag = 2; lag <= 48 && last - lag > from; ++lag) {
					let score = 0;
					for(let k = from + lag; k <= last; ++k) score += hi[k] * hi[k - lag];
					score /= last - from - lag + 1;
					if(score > best + 1e-9) { best = score; period = lag; }
				}
				shape = { n, env, lo, hi, peak, peakAt, last, end, period };
			}
		}
		hitShapes.set(id, shape);
		return shape;
	}

	// Track t's hit as this mockup models it: W, the window's length in seconds, and at(s), the column s seconds in:
	// the waveform's [lo, hi] and the envelope, at velocity 100 and VOL 100.
	function hitModel(t) {
		const tr = tracks[t], m = machineOf(t), shape = hitShape(tr.machine);
		if(!shape) return null;
		const { n, env, lo, hi, peak, peakAt, last, end, period } = shape;
		const syn = m.syn, defs = synDefaults(tr.machine), p = tr.p, sample = m.family === "E12" || m.family === "ROM";
		const slot = name => syn.indexOf(name), moved = (name, per) => slot(name) < 0 ? 0 : (p[slot(name)] - defs[slot(name)]) / per;
		const rate = Math.pow(2, moved("PTCH", 24)), stretch = Math.pow(2, moved("DEC", 16)), hold = Math.max(0, moved("HOLD", 127)) * 0.4;
		// A sample's STRT and END as fractions of its recording.
		const col = HIT_S / n, length = (end + 1) * col;
		const start = sample && slot("STRT") >= 0 ? p[slot("STRT")] / 127 * length : 0;
		const stop = sample && slot("END") >= 0 ? p[slot("END")] / 127 * length : Infinity;
		const envRate = sample ? rate : 1, tp = peakAt * col;
		const lerp = (a, u) => { const k = Math.floor(u), f = u - k; return k + 1 < n ? a[k] * (1 - f) + a[k + 1] * f : a[n - 1]; };
		const at = s => {
			const src = start + s * rate;
			if(src > stop) return [0, 0, 0];
			// The envelope's time: the attack as recorded, its peak held, then its decay stretched.
			let e = start + s * envRate;
			if(e > tp) e = tp + Math.max(0, e - tp - hold * envRate) / stretch;
			if(e / col >= n - 1) return [0, 0, 0];
			const envelope = lerp(env, e / col);
			let c = src / col;
			if(c > last) c = last - period + ((c - last) % period);
			if(c >= n - 1) return [0, 0, 0];
			return [lerp(lo, c) * envelope, lerp(hi, c) * envelope, envelope];
		};
		// The window: as long as the hit at its defaults or as it is now, whichever is longer (and a tenth more), up to
		// the 0.8 s recorded. A hit lengthened past its window steps to the next.
		let audible = 0;
		for(let k = 0; k <= n; ++k) if(at(k * col)[2] > 0.03 * peak) audible = k * col;
		const need = 1.1 * Math.max((last + 1) * col, audible);
		return { W: hitWindows.find(w => w >= need) || HIT_S, at };
	}

	// The hit screen: the waveform, dim until the track plays, in ink behind a playhead that crosses the window in real
	// time (tick() redraws this screen meanwhile); over it, in accent, the envelope DEC and HOLD set.
	function drawHit(frame, _screen = "hit") {
		const [g, w, h] = canvasOf(frame, _screen); if(!g) return;
		const css = cssOf(frame), hit = lastHit[sel], { top, bottom } = plot(h), mid = (top + bottom) / 2, amp = (bottom - top) / 2;
		const model = hitModel(sel), W = model ? model.W : HIT_S;
		const heard = hit ? Math.min(1, (performance.now() - hit.time) / (W * 1000)) : 0;
		for(let ms = 0; ms < W * 1000 - 1; ms += tickMs[W]) { const x = ms / 1000 / W * w; graduation(g, css, x, top, x, bottom, ms ? `${ms} ms` : "0", x + 4, h - 16); }
		graduation(g, css, 0, mid, w, mid);
		if(!model) { g.fillStyle = css("--ink-faint"); g.font = `12px ${css("--font")}`; g.fillText("No sound", 16, mid - 8); return; }
		// Full scale is 1.0; the loudest hits peak near 0.45: draw 0.5 at the plot's edge.
		const gain = (hit ? hit.velocity : 100) / 100 * tracks[sel].p[17] / 100, scale = amp / 0.5 * gain;
		const playX = heard * w, ink = css("--ink"), dim = css("--track"), envelope = new Float32Array(w + 1);
		for(let x = 0; x <= w; ++x) {
			const [lo, hi, e] = model.at((x + 0.5) / w * W);
			envelope[x] = e;
			if(x === w) break;
			const y0 = mid - Math.min(amp, hi * scale), y1 = mid - Math.max(-amp, lo * scale);
			g.fillStyle = hit && x <= playX ? ink : dim;
			g.fillRect(x, y0, 1, Math.max(1, y1 - y0));
		}
		g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
		for(let x = 0; x <= w; ++x) { const y = mid - Math.min(amp, envelope[x] * scale); x ? g.lineTo(x, y) : g.moveTo(x, y); }
		g.stroke();
		if(hit && heard < 1) { g.fillStyle = css("--accent"); g.fillRect(Math.round(playX), top, 1, bottom - top); }
	}

	function drawScreens(frame) {
		drawHit(frame);
		const css = cssOf(frame), t = tracks[sel], p = t.p;
		{
			const [g, w, h] = canvasOf(frame, "filter");
			if(g) {
				const { top, bottom } = plot(h), zero = top + (bottom - top) * 0.45, perDb = (bottom - top) * 0.45 / 24;
				const xOf = f => Math.log(f / 20) / Math.log(1000) * w;
				[[100, "100"], [1000, "1k"], [10000, "10k"]].forEach(([f, text]) => graduation(g, css, xOf(f), top, xOf(f), bottom, text, xOf(f) + 4, h - 16));
				graduation(g, css, 0, zero, w, zero, "0 dB", 4, zero - 4);
				const lo = 20 * Math.pow(1000, p[12] / 127), hi = 20 * Math.pow(1000, Math.min(1, (p[12] + p[13]) / 127)), q = p[14] / 127;
				const eqf = 20 * Math.pow(1000, p[10] / 127), eqg = (p[11] - 64) / 64 * 12;
				g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
				for(let x = 0; x <= w; ++x) {
					const f = 20 * Math.pow(1000, x / w);
					const hp = 1 / Math.sqrt(1 + Math.pow(lo / f, 4)), lp = 1 / Math.sqrt(1 + Math.pow(f / hi, 4));
					const res = 1 + q * 3 * (Math.exp(-Math.pow(Math.log(f / lo), 2) * 20) + Math.exp(-Math.pow(Math.log(f / hi), 2) * 20));
					const eq = Math.pow(10, eqg * Math.exp(-Math.pow(Math.log(f / eqf), 2) * 2) / 20);
					const y = Math.min(bottom, zero - 20 * Math.log10(Math.max(1e-4, hp * lp * res * eq)) * perDb);
					x ? g.lineTo(x, y) : g.moveTo(x, y);
				}
				g.stroke();
				g.fillStyle = css("--ink"); g.fillRect(xOf(eqf) - 3, zero - eqg * perDb - 3, 6, 6);
			}
		}
		{
			const [g, w, h] = canvasOf(frame, "lfo");
			if(g) {
				// The target's value as the LFO moves it, zoomed on what it covers: the knob's value swung by LFOD (127: 64
				// either way), clipped to 0-127, from the plot's foot to its top, so the wave fills the plot whatever the
				// knob and the depth. No line: the wave is the plot; the lowest and the highest values it reaches are named at
				// its foot and top, in the rows' label column (the title names the parameter). The plot spans the chips'
				// columns (x 52 on), under the title, over the three rows of chips (88 px, 16 from the foot).
				const [x0, x1, top, bottom, inside] = { chips: [52, w, 48, h - 112, false], side: [0, 188, 48, h - 32, true],
					fields: [0, w, 48, h - 64, true], mix: [0, w, 48, h - 64, true] }[lfoStyle];
				const l = t.lfo, base = tracks[l.track].p[l.param];
				const depth = p[22] / 127 * 64, periods = 1 + p[21] / 127 * 5, mix = p[23] / 127;
				const lo = Math.max(0, Math.round(base - depth)), hi = Math.min(127, Math.round(base + depth));
				const yOf = v => bottom - (Math.max(lo, Math.min(hi, v)) - lo) / Math.max(1, hi - lo) * (bottom - top);
				g.fillStyle = css("--ink-faint"); g.font = `12px ${css("--font")}`;
				if(hi > lo) { g.fillText(String(hi), inside ? x0 + 4 : 0, top + 4); g.fillText(String(lo), inside ? x0 + 4 : 0, bottom + 4); }
				else g.fillText(`LFOD ${p[22]}: no modulation`, x0, Math.round((top + bottom + 8) / 8) * 4);
				let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
				const steps = []; for(let i = 0; i < 64; ++i) steps.push(rnd() * 2 - 1);
				const shape = (s, ph) => { const f = ph - Math.floor(ph);
					return [1 - 4 * Math.abs(f - 0.5), 1 - 2 * f, f < 0.5 ? 1 : -1, 2 * f - 1, Math.exp(-4 * f) * 2 - 1, steps[Math.floor(ph * 2) % 64]][s]; };
				const wave = ph => (1 - mix) * shape(l.shape1, ph) + mix * shape(l.shape2, ph);
				const trace = (f, colour, width) => {
					g.beginPath(); g.strokeStyle = colour; g.lineWidth = width;
					for(let x = x0; x <= x1; ++x) { const y = yOf(base + f((x - x0) / (x1 - x0) * periods) * depth); x > x0 ? g.lineTo(x, y) : g.moveTo(x, y); }
					g.stroke();
				};
				if(hi > lo) {
					// mix: each shape alone, faint, behind what LFOM makes of the two
					if(lfoStyle === "mix") { trace(ph => shape(l.shape1, ph), css("--track"), 1); trace(ph => shape(l.shape2, ph), css("--track"), 1); }
					trace(wave, css("--accent"), 2);
				}
			}
		}
	}

	// ---- rendering ----
	function focusKey(el) {
		if(!el || !el.closest || !el.closest(".frame")) return null;
		const attrs = [...el.attributes].filter(a => a.name.startsWith("data-") && a.name !== "data-t").map(a => `[${a.name}="${a.value}"]`);
		return attrs.length ? attrs.join("") : null;
	}

	function gridLayer() {
		const cols = Array.from({ length: 16 }, (_, k) => `<div class="colstripe" style="left:${X(k + 1)}px"></div>`).join("");
		return `<div class="gridlayer">${cols}<div class="baseline"></div></div>`;
	}

	function renderFrame(frame) {
		const view = frame.dataset.view || "TRACK", arrows = frame.dataset.arrows || "ends", play = frame.dataset.play || "key";
		// A comparison frame may show the LFO's target being chosen whatever the shared state says (data-lfopick).
		const pick = lfoPick;
		if(frame.dataset.lfopick) lfoPick = frame.dataset.lfopick;
		lfoStyle = frame.dataset.lfo || "chips";
		const body = kitOpen ? kitPanel() : view === "MIX" ? mixView() : view === "MASTER" ? masterView() : trackView(arrows, play);
		lfoPick = pick;
		const grid = showGrid || frame.dataset.grid === "on";
		frame.innerHTML = `<div class="ui" data-playmode="${play}" style="--font:${frame.dataset.font}">${top(view)}<div class="below">${body}</div>${grid ? gridLayer() : ""}</div>`;
		if(view === "TRACK" && !kitOpen) browserOpen ? drawHit(frame, "preview") : drawScreens(frame);
	}

	function renderAll() {
		const key = focusKey(document.activeElement), owner = document.activeElement && document.activeElement.closest ? document.activeElement.closest(".frame") : null;
		document.querySelectorAll(".frame").forEach(renderFrame);
		if(key && owner) { const again = owner.querySelector(key); if(again) again.focus({ preventScroll: true }); }
	}

	// ---- sound: triggers, scopes, meters ----
	function trigger(t, velocity) {
		lastHit[t] = { time: performance.now(), velocity };
		lastNote = { note: 36 + t, velocity };
		document.querySelectorAll(".frame").forEach(frame => {
			const last = frame.querySelector("[data-last]"); if(last) last.textContent = `${lastNote.note} · vel ${velocity}`;
			if(t === sel) {
				const note = frame.querySelector('[data-screen="hit"] .note');
				if(note) note.innerHTML = `Last hit, velocity <span class="num" data-vel>${velocity}</span>`;
				drawHit(frame); drawHit(frame, "preview");
			}
			// A hit lights the track's number in the list and in MIX. The play key does not flash: it lights while pressed,
			// and the hit shows in the hit screen.
			if(!reduceMotion) frame.querySelectorAll(`[data-playt="${t}"]`).forEach(n => { n.classList.add("trig"); setTimeout(() => n.classList.remove("trig"), 140); });
		});
	}

	let lastTick = performance.now();
	function tick(now) {
		const steps = Math.max(1, Math.min(8, Math.round((now - lastTick) / (1000 / 60))));
		lastTick = now;
		const amps = tracks.map((_, t) => amplitude(t, now));
		history.forEach((h, t) => { h.copyWithin(0, steps); h.fill(amps[t], h.length - steps); });
		// The hit screen follows its playhead for 0.8 s after a hit of the shown track (one more frame to end it).
		const playing = lastHit[sel] && now - lastHit[sel].time < 850;
		document.querySelectorAll(".frame").forEach(frame => {
			const css = cssOf(frame);
			if(playing) { drawHit(frame); drawHit(frame, "preview"); }
			frame.querySelectorAll("canvas[data-scope]").forEach(c => {
				const t = +c.dataset.scope, h = history[t], w = c.clientWidth, ht = c.clientHeight;
				if(c.width !== w * 2) { c.width = w * 2; c.height = ht * 2; }
				const g = c.getContext("2d"); g.setTransform(2, 0, 0, 2, 0, 0); g.clearRect(0, 0, w, ht);
				g.strokeStyle = t === sel ? css("--ink") : css("--wave"); g.lineWidth = 1; g.beginPath();
				for(let x = 0; x < w; ++x) { const a = h[Math.floor(x / w * h.length)], y = a * (ht / 2 - 2); if(y < 0.5) continue; g.moveTo(x + 0.5, ht / 2 - y); g.lineTo(x + 0.5, ht / 2 + y); }
				g.stroke();
			});
			frame.querySelectorAll("[data-vu]").forEach(b => b.style.height = Math.min(100, amps[+b.dataset.vu] * 100) + "%");
			const mainLevel = Math.min(1, amps.reduce((s, a, t) => s + (separated.has(t) ? 0 : a), 0) * 0.6);
			frame.querySelectorAll("[data-meter] b").forEach((b, i) => b.style.width = Math.min(100, mainLevel * (i ? 96 : 100)) + "%");
		});
		requestAnimationFrame(tick);
	}

	const pattern = [[0, [0,4,8,10], 120], [1, [4,12], 110], [3, [12], 90], [4, [7,15], 70], [5, [3], 60], [6, [0,2,4,6,8,10,12,14], 90], [7, [14], 100], [14, [6], 80]];
	let demo = null, demoStep = 0;
	function setDemo(on) {
		if(demo) { clearInterval(demo); demo = null; }
		if(!on) return;
		demoStep = 0;
		demo = setInterval(() => { pattern.forEach(([t, steps, vel]) => { if(steps.includes(demoStep)) trigger(t, vel); }); demoStep = (demoStep + 1) % 16; }, 125);
	}

	// ---- interaction ----
	let drag = null;
	let browserFromP = null, browserFromEdited = false;
	const openBrowser = () => { browserOpen = true; browserFrom = tracks[sel].machine; browserFromP = tracks[sel].p.slice(); browserFromEdited = kitEdited; lfoPick = null; };
	const cancelBrowser = () => { if(browserFrom !== null) { tracks[sel].machine = browserFrom; tracks[sel].p = browserFromP; kitEdited = browserFromEdited; } browserOpen = false; renderAll(); };
	// A machine loaded takes its own SYN defaults, as on the Machinedrum.
	const choose = id => { tracks[sel].machine = id; tracks[sel].p.splice(0, 8, ...synDefaults(id)); kitEdited = true; renderAll(); if(autoListen) trigger(sel, 100); };
	const focusCurrent = () => document.querySelectorAll(".frame .mach.cur").forEach(m => m.focus({ preventScroll: true }));
	const stepMachine = d => { const i = machineList.indexOf(tracks[sel].machine), n = machineList.length; choose(machineList[(i + d + n) % n]); };
	// The LFO being assigned belongs to the track that started the assignment.
	let lfoOwner = 0;

	function act(target, e) {
		const frame = target.closest(".frame"); if(!frame) return false;
		const at = s => target.closest(s);
		const tab = at(".tab[data-view]"), mute = at("[data-mute]"), solo = at("[data-solo]"), out = at("[data-out]"), chip = at("[data-shape1],[data-shape2],[data-mode]");
		if(tab) { frame.dataset.view = tab.dataset.view; browserOpen = false; kitOpen = false; renderAll(); return true; }
		if(at("[data-kitopen]")) {
			kitOpen = !kitOpen; kitConfirm = null; kitHint = ""; kitRenaming = false;
			if(kitOpen) { kitSel = kitLoaded; browserOpen = false; lfoPick = null; }
			renderAll(); return true;
		}
		if(at("[data-kitsave]")) { if(kitEdited) saveKit(kitLoaded); return true; }
		if(at("[data-kitslot]")) { const i = +at("[data-kitslot]").dataset.kitslot; if(i !== kitSel) { kitSel = i; kitConfirm = null; kitHint = ""; kitRenaming = false; renderAll(); } return true; }
		if(at("[data-kitload]")) { loadKit(kitSel); return true; }
		if(at("[data-kitcancel]")) { kitConfirm = null; renderAll(); return true; }
		if(at("[data-kitsavehere]")) { saveKit(kitSel); return true; }
		if(at("[data-kitdelete]")) { deleteKit(kitSel); return true; }
		if(at("[data-kitimport]")) { importKit(); return true; }
		if(at("[data-kitexport]")) { exportKit(); return true; }
		if(at("[data-kitrename]")) {
			kitRenaming = true; kitConfirm = null; kitHint = ""; renderAll();
			document.querySelectorAll(".frame [data-kitname]").forEach(n => { n.focus(); getSelection().selectAllChildren(n); });
			return true;
		}
		if(at("[data-scale]")) { uiScale = uiScale >= 1.5 ? 1 : uiScale + 0.25; if(onScale) onScale(uiScale); renderAll(); return true; }
		if(mute) { const i = +mute.dataset.mute; tracks[i].mute = !tracks[i].mute; renderAll(); return true; }
		if(solo) { const i = +solo.dataset.solo; tracks[i].solo = !tracks[i].solo; renderAll(); return true; }
		if(out) { const i = +out.dataset.out; if(separated.has(i)) separated.delete(i); else separated.add(i); renderAll(); return true; }
		if(chip) {
			const l = tracks[sel].lfo;
			if(chip.dataset.shape1) l.shape1 = +chip.dataset.shape1;
			else if(chip.dataset.shape2) l.shape2 = +chip.dataset.shape2;
			else l.mode = +chip.dataset.mode;
			lfoPop = null; kitEdited = true; renderAll(); return true;
		}
		if(at("[data-lfomenu]")) { lfoPick = lfoPick === "menu" ? null : "menu"; renderAll(); return true; }
		if(at("[data-lfotrack]")) { tracks[sel].lfo.track = +at("[data-lfotrack]").dataset.lfotrack; kitEdited = true; renderAll(); return true; }
		if(at("[data-lfoparam]")) { tracks[sel].lfo.param = +at("[data-lfoparam]").dataset.lfoparam; lfoPick = null; kitEdited = true; renderAll(); return true; }
		if(at("[data-lfoassign]")) { lfoPick = lfoPick === "assign" ? null : "assign"; lfoOwner = sel; renderAll(); return true; }
		if(lfoPick === "assign" && at("[data-i]") && at("[data-i]").dataset.i !== "") {
			const l = tracks[lfoOwner].lfo; l.track = sel; l.param = +at("[data-i]").dataset.i; lfoPick = null; sel = lfoOwner; kitEdited = true; renderAll(); return true;
		}
		if(at("[data-audition]")) { trigger(sel, at("[data-audition]").classList.contains("playkey") ? auditionVelocity : 100); return true; }
		if(at("[data-playt]")) { const t = +at("[data-playt]").dataset.playt; if(t !== sel) { sel = t; browserOpen = false; renderAll(); } trigger(t, 100); return true; }
		if(at("[data-pick]")) { sel = +at("[data-pick]").dataset.pick; browserOpen = false; renderAll(); return true; }
		if(at("[data-step]")) { stepMachine(+at("[data-step]").dataset.step); return true; }
		if(at("[data-trackstep]")) { sel = (sel + +at("[data-trackstep]").dataset.trackstep + 16) % 16; browserOpen = false; renderAll(); return true; }
		if(at("[data-browse]")) { if(browserOpen) browserOpen = false; else openBrowser(); renderAll(); return true; }
		if(at("[data-keep]")) { browserOpen = false; renderAll(); return true; }
		if(at("[data-cancel]")) { cancelBrowser(); return true; }
		if(at("[data-autolisten]")) { autoListen = !autoListen; renderAll(); return true; }
		if(at("[data-mach]")) { choose(+at("[data-mach]").dataset.mach); return true; }
		if(at("[data-strip]")) { sel = +at("[data-strip]").dataset.strip; renderAll(); return true; }
		return false;
	}

	function control(target) {
		const at = s => target.closest(s);
		const fader = at("[data-fader]"), mix = at("[data-mix]"), knob = at("[data-i]"), vel = at("[data-auditionvel]");
		if(lfoPick === "assign") return null;
		if(fader) { const tr = tracks[+fader.dataset.fader]; return { get: () => tr.level, set: v => { tr.level = v; kitEdited = true; }, px: fader.getBoundingClientRect().height / 127 }; }
		if(mix) { const tr = tracks[+mix.dataset.track], i = +mix.dataset.mix; return { get: () => tr.p[i], set: v => { tr.p[i] = v; kitEdited = true; }, px: 1.5 }; }
		if(vel) return { get: () => auditionVelocity, set: v => auditionVelocity = Math.max(1, v), px: 1.5 };
		const field = at("[data-lfofield]");
		if(field) {
			const key = field.dataset.lfofield, l = tracks[sel].lfo, owner = field.closest(".frame");
			return { get: () => l[key], set: v => { l[key] = Math.max(0, Math.min(lfoFieldMax[key], v)); kitEdited = true; }, px: 12, step: 1,
				click: () => { if(key === "track" || key === "param") { lfoPop = null; lfoPick = lfoPick === "menu" ? null : "menu"; } else lfoPop = lfoPop === key ? null : key; renderAll(); } };
		}
		const fx = at("[data-fx]");
		if(fx) { const v = master[fx.dataset.fx], i = +fx.dataset.k, def = fxDefaults[fx.dataset.fx];
			return { get: () => v[i], set: x => { v[i] = x; kitEdited = true; }, px: 1.5, reset: () => { v[i] = def[i]; kitEdited = true; } }; }
		if(knob && knob.dataset.i !== "") { const i = +knob.dataset.i; return { get: () => tracks[sel].p[i], set: v => { tracks[sel].p[i] = v; kitEdited = true; }, px: 1.5, reset: () => { tracks[sel].p[i] = defaultOf(sel, i); kitEdited = true; } }; }
		return null;
	}
	const clamp = v => Math.max(0, Math.min(127, Math.round(v)));

	function onDown(e) {
		if(!e.target.closest(".frame")) return;
		const c = control(e.target);
		if(c) { drag = { c, y: e.clientY, start: c.get() }; e.preventDefault(); return; }
		if(act(e.target, e)) e.preventDefault();
	}
	function onMove(e) {
		if(!drag) return;
		if(Math.abs(e.clientY - drag.y) >= 4) drag.moved = true;
		if(!drag.moved) return;
		const px = drag.c.px * (e.shiftKey ? 4 : 1);
		drag.c.set(clamp(drag.start + (drag.y - e.clientY) / px));
		renderAll();
	}
	function onWheel(e) {
		if(!e.target.closest || !e.target.closest(".frame")) return;
		// Over the machine's name: step through the machines (every head band; D's only way besides the browser).
		if(e.target.closest("[data-browse]")) { e.preventDefault(); stepMachine(e.deltaY > 0 ? 1 : -1); return; }
		const c = control(e.target);
		if(!c) return;
		e.preventDefault();
		c.set(clamp(c.get() + (e.deltaY < 0 ? 1 : -1) * (c.step || (e.shiftKey ? 1 : 2))));
		renderAll();
	}
	function browserKey(e, el) {
		const i = machineList.indexOf(tracks[sel].machine), n = machineList.length;
		if(e.key === "Escape") { cancelBrowser(); return true; }
		if(e.key === "Enter" && !el.closest("[data-cancel],[data-keep],[data-autolisten]")) { browserOpen = false; renderAll(); return true; }
		if(e.key === "ArrowUp" || e.key === "ArrowDown") { choose(machineList[(i + (e.key === "ArrowDown" ? 1 : -1) + n) % n]); focusCurrent(); return true; }
		if(e.key === "ArrowLeft" || e.key === "ArrowRight") {
			const f = familyOrder.indexOf(byId.get(tracks[sel].machine).family), k = familyOrder.length;
			const family = familyOrder[(f + (e.key === "ArrowRight" ? 1 : -1) + k) % k];
			choose(MD_MACHINES.find(m => m.family === family).id); focusCurrent(); return true;
		}
		return false;
	}
	function onKey(e) {
		const el = document.activeElement;
		const inFrame = el && el.closest && el.closest(".frame");
		// A kit's name being typed takes every key but Enter (keep it) and Esc (leave it).
		if(el && el.closest && el.closest("[data-kitname]") && kitRenaming) { if(e.key === "Enter" || e.key === "Escape") { e.preventDefault(); commitRename(el, e.key === "Enter"); } return; }
		if((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") { if(kitEdited) saveKit(kitLoaded); e.preventDefault(); return; }
		if(kitOpen && e.key === "Escape") { if(kitConfirm) kitConfirm = null; else kitOpen = false; renderAll(); return; }
		if(kitOpen && el && el.closest && el.closest("[data-kitslot]")) {
			const moves = { ArrowUp: -1, ArrowDown: 1, ArrowLeft: -16, ArrowRight: 16 };
			if(moves[e.key] !== undefined) {
				kitSel = Math.max(0, Math.min(63, kitSel + moves[e.key])); kitConfirm = null; kitHint = ""; renderAll();
				document.querySelectorAll(`.frame [data-kitslot="${kitSel}"]`).forEach(n => n.focus({ preventScroll: true }));
				e.preventDefault(); return;
			}
			if(e.key === "Enter") { loadKit(kitSel); e.preventDefault(); return; }
		}
		if(e.key === "Escape" && lfoPop) { lfoPop = null; renderAll(); return; }
		if(e.key === "Escape" && lfoPick) { if(lfoPick === "assign") sel = lfoOwner; lfoPick = null; renderAll(); return; }
		// Space with nothing focused in the editor plays the shown track.
		if(e.key === " " && !inFrame && document.querySelector(".frame")) { trigger(sel, 100); e.preventDefault(); return; }
		if(!inFrame) return;
		if(browserOpen && browserKey(e, el)) { e.preventDefault(); return; }
		if(el.closest("[data-browse]") && (e.key === "ArrowUp" || e.key === "ArrowDown")) { stepMachine(e.key === "ArrowDown" ? 1 : -1); e.preventDefault(); return; }
		const c = control(el);
		if(c && (e.key === "ArrowUp" || e.key === "ArrowDown")) { c.set(clamp(c.get() + (e.key === "ArrowUp" ? 1 : -1) * (e.shiftKey ? 10 : 1))); renderAll(); e.preventDefault(); return; }
		if(e.key === "Enter" || e.key === " ") { if(act(el, e)) e.preventDefault(); return; }
	}
	function onDouble(e) {
		const slot = e.target.closest && e.target.closest("[data-kitslot]");
		if(slot) { loadKit(+slot.dataset.kitslot); return; }
		const c = e.target.closest && e.target.closest(".frame") ? control(e.target) : null;
		if(c && c.reset) { c.reset(); renderAll(); }
	}
	function onOver(e) {
		const mach = e.target.closest && e.target.closest("[data-mach]"); if(!mach) return;
		const m = byId.get(+mach.dataset.mach), hint = mach.closest(".frame").querySelector("[data-bhint]");
		if(hint) hint.textContent = `${m.name}, ${families[m.family]}: ${m.syn.filter(Boolean).join(" ") || "no SYN parameter"}`;
	}

	function start() {
		document.addEventListener("pointerdown", onDown);
		document.addEventListener("pointermove", onMove);
		// A drag that did not move is a click, for the controls that have one (the LFO's fields).
		document.addEventListener("pointerup", () => { const d = drag; drag = null; if(d && !d.moved && d.c.click) d.c.click(); });
		document.addEventListener("dblclick", onDouble);
		document.addEventListener("wheel", onWheel, { passive: false });
		document.addEventListener("keydown", onKey);
		document.addEventListener("pointerover", onOver);
		document.addEventListener("focusout", e => { if(e.target.closest && e.target.closest("[data-kitname]")) commitRename(e.target, true); });
		renderAll();
		if(document.fonts) document.fonts.ready.then(renderAll);
		requestAnimationFrame(tick);
	}

	return { start, renderAll, setGrid: on => { showGrid = on; renderAll(); }, setDemo, onScaleChange: f => onScale = f, trigger,
		select: t => { sel = t; renderAll(); }, setMachine: id => { tracks[sel].machine = id; renderAll(); },
		setBrowser: on => { if(on) openBrowser(); else browserOpen = false; renderAll(); }, machines: () => machineList.slice(),
		setLfoPick: mode => { lfoPick = mode; lfoOwner = sel; renderAll(); },
		setKits: on => { kitOpen = on; if(on) kitSel = kitLoaded; renderAll(); }, kitConfirm: c => { kitConfirm = c; renderAll(); },
		hits: () => lastHit.map(h => h && h.velocity), grid: { X, W } };
})();
