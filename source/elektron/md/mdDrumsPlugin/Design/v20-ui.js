// MD Drums v20 mockup (v20-ui.css; machine data in v6-machines.js; real hits in v20-hits.js). v19's state and
// behaviour, placed on a column grid as Figma lays one out: 16 columns of 64 px, 16 px gutters, 8 px margins, an 8 px
// baseline. Every element is an absolute box whose sides lie on column edges and whose top and bottom lie on the
// baseline (box(), raw() for backgrounds that bleed to a gutter's middle). In English. The SYN screen draws the
// machine's real hit, rendered by mdDrums::Engine (hitexport.cpp). No bar of shortcuts under the bands: they are in the
// tooltips, and the bands take its height. The frame's data-arrows and data-play pick where ‹ › and the play control go
// (see head()). The LFO's target is chosen two ways: a menu over its whole screen, or ASSIGN and a click on any knob.
"use strict";

const MD = (() => {
	// ---- the grid ----
	const X = c => 8 + (c - 1) * 80, W = n => n * 64 + (n - 1) * 16;
	const box = (c, n, y, h) => `left:${X(c)}px;width:${W(n)}px;top:${y}px;height:${h}px`;
	const raw = (x, w, y, h) => `left:${x}px;width:${w}px;top:${y}px;height:${h}px`;
	// The three bands' tops; each is 224 px tall, the last ends at the window's bottom (800).
	const BAND = [128, 352, 576], BAND_H = 224;

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
	const sendsPending = new Set([19, 20]);
	const two = n => String(n).padStart(2, "0");
	const specNames = ["SYN1","SYN2","SYN3","SYN4","SYN5","SYN6","SYN7","SYN8", ...efxNames, ...routeNames];
	const pid = (t, name) => `t${t + 1}_${name}`;
	const reduceMotion = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

	// Each band's knobs in the Machinedrum's order, and its groups: name, first slot, slot after the last, waiting.
	const layouts = {
		syn: { order: [0, 1, 2, 3, 4, 5, 6, 7], groups: [["", 0, 8]] },
		efx: { order: [8, 9, 10, 11, 12, 13, 14, 15], groups: [["AM", 0, 2], ["EQ", 2, 4], ["Filter", 4, 7]] },
		rte: { order: [16, 17, 18, 19, 20, 21, 22, 23], groups: [["Channel", 0, 3], ["Sends", 3, 5, true], ["LFO", 5, 8]] },
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

	// A flat knob filling its column: a 270 degree track, the value as an arc, a pointer.
	function knobSvg(size, value) {
		const r = size / 2 - 3, c = size / 2, a0 = 0.75 * Math.PI, sweep = 1.5 * Math.PI, a = a0 + sweep * value / 127;
		const p = (t, rr) => [c + rr * Math.cos(t), c + rr * Math.sin(t)];
		const arc = (from, to) => { const [x0, y0] = p(from, r), [x1, y1] = p(to, r); return `M${x0} ${y0} A${r} ${r} 0 ${to - from > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
		const [x0, y0] = p(a, r * 0.35), [x1, y1] = p(a, r - 5);
		return `<svg width="${size}" height="${size}" aria-hidden="true"><path d="${arc(a0, a0 + sweep)}" stroke="var(--track)" stroke-width="2" fill="none"/>
			${value > 0 ? `<path d="${arc(a0, a)}" stroke="var(--ink)" stroke-width="3" fill="none"/>` : ""}
			<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}" stroke="var(--ink)" stroke-width="2"/></svg>`;
	}
	const playSvg = (w, h) => `<svg width="${w}" height="${h}" viewBox="0 0 14 16" aria-hidden="true"><path d="M2 1 L13 8 L2 15 Z"/></svg>`;

	const toggle = (kind, i, on, style) => `<div class="cell toggle ${kind}${on ? " on" : ""}" id="${pid(i, kind)}" style="${style}" data-${kind}="${i}" tabindex="0" role="switch" aria-checked="${on}"
		aria-label="${kind === "mute" ? "Mute" : "Solo"}, track ${two(i + 1)}">${kind === "mute" ? "M" : "S"}</div>`;

	function top(view) {
		const tabs = ["TRACK", "MIX"].map((n, k) => `<div class="cell tab${n === view ? " on" : ""}" style="${box(4 + k, 1, 0, 40)}" data-view="${n}" tabindex="0" role="tab" aria-selected="${n === view}"><span>${n}</span></div>`).join("");
		return `<div class="cell logo" style="${box(1, 3, 0, 40)}">MD<span>DRUMS</span></div>${tabs}
			<div class="cell num" style="${box(13, 1, 0, 40)};font-size:12px;color:var(--ink-dim)" data-scale tabindex="0" title="Window size">${Math.round(uiScale * 100)} %</div>
			<div class="cell" style="${box(16, 1, 8, 16)}"><span class="label">Main</span></div>
			<div class="cell outbars" style="${box(16, 1, 24, 8)}" data-meter><i><b></b></i><i><b></b></i></div>
			<div class="rule" style="${raw(0, 1280, 39, 1)}"></div>`;
	}

	// The track list on columns 1-3, its rows laid out on multiples of 4: the number at x 8, the machine right after it
	// (x 40), its scope (x 132-164), then M and S as a pair 4 px apart, 16 px from the panel's edge (x 172-224). The
	// shown track's row is lit to the gutter's middle (x 240), its mark a 4 px bar at the window's edge.
	function trackList(head) {
		const rows = tracks.map((tr, i) => {
			const y = 128 + 40 * i, on = i === sel;
			return `${on ? `<div class="bg surface" style="${raw(0, 240, y, 40)}"></div><div class="bg selbar" style="${raw(0, 4, y + 8, 24)}"></div>` : ""}
			<div class="cell tn${on ? " tsel" : ""}" style="${raw(8, 24, y + 8, 24)}" data-playt="${i}" tabindex="0" aria-label="Play track ${two(i + 1)}" title="Play">${two(i + 1)}</div>
			<div class="cell scope" style="${raw(132, 32, y + 8, 24)}"><canvas data-scope="${i}"></canvas></div>
			<div class="cell tm${on ? " tsel" : ""}" style="${raw(40, 84, y + 8, 24)}" data-pick="${i}" tabindex="0">${byId.get(tr.machine).name}</div>
			${toggle("mute", i, tr.mute, raw(172, 24, y + 8, 24))}${toggle("solo", i, tr.solo, raw(200, 24, y + 8, 24))}`;
		}).join("");
		// Under the 16 rows (y 768-792): the kit, where its menu will open (save, load, the factory kits).
		return `<div class="cell r24" style="${box(1, 3, 92, 24)}"><span class="label">Tracks</span></div>${rows}
			<div class="cell kitname r24" style="${box(1, 3, 768, 24)}" data-kit tabindex="0" title="Kit: save, load, factory kits (to come)">Kit 01 ▾</div>`;
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
		const name = (style, cls = "drop") => `<div class="cell machine ${cls}" style="${style}" id="${pid(sel, "machine")}" data-browse tabindex="0" aria-label="Choose a machine" title="Click to browse · scroll or ↑ ↓ to step">${m.name}</div>`;
		const step = (d, style, cls, glyph) => `<div class="cell step ${cls}" style="${style}" data-step="${d}" tabindex="0" aria-label="${d < 0 ? "Previous" : "Next"} machine">${glyph}</div>`;
		let html = `<div class="cell r24" style="${box(4, arrows === "caption" ? 1 : 4, 52, 24)}"><span class="label num">Track ${two(sel + 1)}</span></div>`;
		if(arrows === "ends") {
			html += `<div class="cell picker" style="${box(4, 4, 76, 40)}"></div>${step(-1, raw(X(4), 32, 76, 40), "edge", "‹")}
				${name(raw(X(4) + 32, W(4) - 64, 76, 40) + ";justify-content:center", "inpicker")}${step(1, raw(X(7) + 32, 32, 76, 40), "edge right", "›")}`;
		} else if(arrows === "stepper") {
			html += `<div class="cell field" style="${box(4, 4, 76, 40)}"></div>${name(raw(X(4), W(4) - 32, 76, 40), "")}
				<div class="cell stepper" style="${raw(X(7) + 40, 24, 84, 32)}"><span data-step="-1" tabindex="0" aria-label="Previous machine">▲</span><span data-step="1" tabindex="0" aria-label="Next machine">▼</span></div>`;
		} else if(arrows === "caption") {
			// The user's reading (2026-10-07): ‹ › next to TRACK 01 step through the tracks, not the machines; the
			// machine changes from its name (browser, scroll, ↑ ↓). They start on column 5, right after the caption.
			const trackStep = (d, x, glyph) => `<div class="cell step small" style="${raw(x, 24, 52, 24)}" data-trackstep="${d}" tabindex="0" aria-label="${d < 0 ? "Previous" : "Next"} track" title="${d < 0 ? "Previous" : "Next"} track">${glyph}</div>`;
			html += `${trackStep(-1, X(5), "‹")}${trackStep(1, X(5) + 24, "›")}${name(box(4, 4, 76, 40))}`;
		} else {
			html += name(box(4, 4, 76, 40));
		}
		html += `<div class="cell meta inset r24" style="${box(12, infoCols, 52, 24)}">${m.family} · ${families[m.family]}</div>
			<div class="cell meta inset r24" style="${box(12, infoCols, 92, 24)}">Output&nbsp;<b>${own ? "Out " + two(sel + 1) : "Main"}</b></div>`;
		if(play === "key")
			html += `<div class="cell r24" style="${box(15, 1, 52, 24)}"><span class="label">Vel</span></div>
				<div class="cell velv" style="${box(15, 1, 84, 32)}" data-auditionvel tabindex="0" role="slider" aria-label="Play velocity" title="Drag: the velocity ▶ plays with">${auditionVelocity}</div>
				<div class="cell playkey" style="${box(16, 1, 52, 64)}" data-audition tabindex="0" role="button" aria-label="Play track ${two(sel + 1)}" title="Play track ${two(sel + 1)}">${playSvg(18, 20)}</div>`;
		return html;
	}
	let auditionVelocity = 100;

	const shapeRows = t => `<div class="lfoshapes">${[1, 2].map(row => {
		const current = t.lfo[`shape${row}`];
		return `<div class="srow"><span class="label" title="LFOM mixes shape 1 into shape 2">${row}</span>${shapes.map((s, i) =>
			`<div class="chip${i === current ? " on" : ""}" data-shape${row}="${i}" tabindex="0" role="radio" aria-checked="${i === current}">${s}</div>`).join("")}</div>`;
	}).join("")}</div>`;

	// The LFO's target as the ROUTING screen's title says it: "LFO → T01 · TRX-BD · FLTF".
	const lfoTargetText = t => { const l = t.lfo; return `T${two(l.track + 1)} · ${machineOf(l.track).name} · ${names(l.track)[l.param] || "—"}`; };

	function pages(play) {
		const t = tracks[sel], n = names(sel), m = machineOf(sel);
		// The LFOs that modulate a parameter of the shown track (any track's LFO, with some depth), named under its knob.
		const modulators = i => tracks.map((tr, k) => k).filter(k => tracks[k].lfo.track === sel && tracks[k].lfo.param === i && tracks[k].p[22] > 0);
		const band = (k, cls, title, screen) => {
			const y = BAND[k], L = layouts[cls];
			// A band's rows, its 160 px of content centred in its 224: title (y + 32), groups (+ 64), knobs (+ 88),
			// labels (+ 160), values (+ 176).
			const groups = L.groups.map(([gname, a, b, pending]) => `<div class="cell grp${b - a === 1 ? " one" : ""}${gname ? "" : " sym"}${pending ? " pending" : ""}" style="${box(4 + a, b - a, y + 64, 16)}"${pending ? ` title="Master effects to come"` : ""}>${gname}</div>`).join("");
			const knobs = L.order.map((i, pos) => {
				const unused = (i < 8 && !n[i]) || sendsPending.has(i);
				const why = i < 8 && !n[i] ? `${m.name} does not use this parameter` : sendsPending.has(i) ? "Master effects to come" : "";
				const label = n[i] || "—", cls2 = unused ? " unused" : "", c = 4 + pos;
				const target = lfoPick === "assign" && !unused ? " target" : "", mods = modulators(i);
				const modTitle = mods.length ? ` title="Modulated by ${mods.map(k => "LFO " + two(k + 1)).join(", ")}"` : "";
				return `<div class="cell knob${cls2}${target}" style="${box(c, 1, y + 88, 64)}" id="${pid(sel, specNames[i])}" data-i="${unused ? "" : i}"${unused ? ` title="${why}"` : ` tabindex="0" role="slider" aria-label="${label}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${t.p[i]}" title="${knobTip}"`}>${knobSvg(64, t.p[i])}</div>
					<div class="cell kl${mods.length ? " lfo" : ""}${cls2}" style="${box(c, 1, y + 160, 16)}"${why ? ` title="${why}"` : modTitle}><span class="label">${label}</span></div>
					<div class="cell kv value${cls2}" style="${box(c, 1, y + 176, 16)}">${unused && i < 8 ? "" : t.p[i]}</div>`;
			}).join("");
			return `<div class="cell btitle" style="${box(4, 3, y + 32, 24)}">${title}</div>${groups}${knobs}
				<div class="cell screen${screen.cls || ""}" style="${raw(X(12), 1280 - X(12), y, BAND_H)}" data-screen="${screen.name}"${screen.attrs || ""}><canvas></canvas><div class="stitle">${screen.title}</div>${screen.extra || ""}</div>`;
		};
		const plays = play === "screen";
		// The LFO's target, two ways: its name opens the menu, which takes the whole screen; ASSIGN waits for a click on
		// any knob, on this track or on another chosen in the list.
		const assigning = lfoPick === "assign";
		const lfoTitle = `<span class="label">LFO →</span><span id="${pid(sel, "lfoTarget")}" data-lfomenu tabindex="0" title="Choose the track and the parameter">${assigning ? "Click a knob…" : lfoTargetText(t) + " ▾"}</span>
			<span class="note"><span class="assign${assigning ? " on" : ""}" data-lfoassign tabindex="0" title="${assigning ? "Esc or click to cancel" : "Click any knob, on this track or another, to modulate it"}">Assign</span>
			<span id="${pid(sel, "lfoMode")}" data-modecycle tabindex="0" title="Mode: FREE, TRIG or HOLD">${modes[t.lfo.mode]}</span></span>`;
		// A rule over every band: SYN, EFX and ROUTING are framed alike, and so are their screens (the last ends at the window's edge).
		// Each rule spans the editing frame, as its surface does: from the gutter's middle (x 240) to the window's edge.
		const rules = BAND.map(y => `<div class="rule" style="${raw(240, 1040, y - 1, 1)}"></div>`).join("");
		return rules + band(0, "syn", "SYN", { name: "hit", cls: plays ? " plays" : "", attrs: plays ? ` data-audition tabindex="0" role="button" aria-label="Play track ${two(sel + 1)}" title="Click to play"` : "",
				title: `${plays ? `<span class="playglyph">${playSvg(10, 12)}</span>` : ""}<span class="label">Hit</span><span class="note hint">${
					t.p.slice(0, 8).some((v, i) => v !== synDefaults(t.machine)[i]) ? "the engine's hit at SYN defaults"
						: `${plays ? "click to play · " : ""}last hit · vel <span class="num" data-vel>${lastHit[sel] ? lastHit[sel].velocity : "—"}</span>`}</span>` })
			+ band(1, "efx", "EFX", { name: "filter", title: `<span class="label">Filter · EQ</span>${t.p[15] > 0 ? `<span class="note hint num">SRR ${t.p[15]}</span>` : ""}` })
			+ band(2, "rte", "ROUTING", { name: "lfo", title: lfoTitle, extra: shapeRows(t) + (lfoPick === "menu" ? lfoMenu(t) : "") });
	}

	// The target menu takes the LFO's whole screen: its own title (Done closes it), the 16 tracks with their machines,
	// then the chosen track's 24 parameters by their names on its machine, a row per band.
	function lfoMenu(t) {
		const l = t.lfo, list = names(l.track);
		const trackCells = tracks.map((tr, i) => `<div class="mt${i === l.track ? " on" : ""}" data-lfotrack="${i}" tabindex="0" title="${byId.get(tr.machine).name}">${two(i + 1)}</div>`).join("");
		const group = (title, from) => `<div class="mg"><span class="label">${title}</span>${list.slice(from, from + 8).map((p, k) =>
			`<div class="mp${from + k === l.param ? " on" : ""}${p ? "" : " off"}" data-lfoparam="${from + k}" tabindex="0">${p || "—"}</div>`).join("")}</div>`;
		return `<div class="lfomenu"><div class="mhead"><span class="label">LFO ${two(sel + 1)} modulates</span><span class="hint">track ${two(l.track + 1)} · ${machineOf(l.track).name}</span>
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
				return `<div class="cell mach${x.id === cur ? " cur" : ""}" style="${box(c + col, 1, 240 + 32 * row, 24)}" data-mach="${x.id}" tabindex="0" title="${x.name}">${model(x)}</div>`;
			}).join("");
			return `<div class="cell grp fam${n === 1 ? " one" : ""}" style="${box(c, n, 200, 16)}">${f}<span class="num">${list.length}</span></div>
				<div class="cell kind" style="${box(c, n, 216, 16)}" title="${kindTips[f] || families[f]}">${kinds[f]}</div>${items}`;
		}).join("");
		const keys = "Click: load and listen · ↑ ↓: next machine · ← →: next family · Enter: keep · Esc: cancel";
		return `<div class="cell bhead" style="${box(4, 8, 160, 24)}" title="${keys}"><b>Choose a machine</b><span>track ${two(sel + 1)}</span><span data-bhint></span></div>
			<div class="cell bact r" style="${box(12, 5, 160, 24)}"><span data-cancel tabindex="0" title="Back to ${from.name} (Esc)">Cancel</span><span class="keep" data-keep tabindex="0" title="Keep ${m.name} (Enter)">Keep</span></div>
			${fams}
			<div class="cell grp fam" style="${box(12, 5, 496, 16)}">Preview</div>
			<div class="cell pvname" style="${box(12, 5, 520, 24)}"><b>${m.name}</b><span class="meta">${kinds[m.family]}</span></div>
			<div class="cell pvauto${autoListen ? " on" : ""}" style="${box(12, 5, 552, 16)}" data-autolisten tabindex="0" role="switch" aria-checked="${autoListen}"><i></i><span>Listen while choosing</span></div>
			<div class="cell screen" style="${raw(X(12), 1280 - X(12), BAND[2], BAND_H)}" data-screen="preview"><canvas></canvas><div class="stitle"><span class="label">Hit</span><span class="note hint">${m.name}</span></div></div>`;
	}

	// No bar of shortcuts: they are in the tooltips (knobTip on every knob, the browser's on its title).
	function trackView(arrows, play) {
		return `<div class="bg surface" style="${raw(240, 1040, BAND[0], BAND[2] + BAND_H - BAND[0])}"></div>${trackList()}${head(arrows, play)}${browserOpen ? browser() : pages(play)}`;
	}
	const knobTip = "Drag to adjust · Shift: fine · double-click: default · scroll: step";

	// MIX: one strip per column; a hairline in each gutter's middle; the shown track's strip lit to the gutters' middles.
	function mixView() {
		const strips = tracks.map((tr, i) => {
			const c = i + 1, on = i === sel;
			return `${on ? `<div class="bg" style="${raw(X(c) - 8, 80, 40, 760)};background:var(--chrome)"></div><div class="bg stripsel" style="${box(c, 1, 40, 4)}"></div>` : ""}
				${i ? `<div class="bg sepline" style="${raw(X(c) - 8, 1, 40, 760)}"></div>` : ""}
				<div class="cell c sn${on ? " on" : ""}" style="${box(c, 1, 48, 16)}" data-playt="${i}" tabindex="0" aria-label="Play track ${two(i + 1)}">${two(i + 1)}</div>
				<div class="cell c sm" style="${box(c, 1, 64, 24)}" data-strip="${i}">${byId.get(tr.machine).name}</div>
				<div class="cell pan" style="${box(c, 1, 96, 64)}" id="${pid(i, "PAN")}" data-mix="18" data-track="${i}" tabindex="0" role="slider" aria-label="PAN, track ${two(i + 1)}">${knobSvg(64, tr.p[18])}</div>
				<div class="cell c" style="${box(c, 1, 160, 16)}"><span class="label">Pan</span></div>
				<div class="cell vu" style="${raw(X(c), 4, 192, 448)}"><b data-vu="${i}"></b></div>
				<div class="cell fader" style="${box(c, 1, 192, 448)}" id="${pid(i, "level")}" data-fader="${i}" tabindex="0" role="slider" aria-label="Kit level, track ${two(i + 1)}"><div class="rail"></div>
					<div class="capf" style="bottom:calc(${tr.level / 127 * 100}% - 4px)"></div></div>
				<div class="cell c value" style="${box(c, 1, 648, 16)}">${tr.level}</div>
				${toggle("mute", i, tr.mute, raw(X(c), 24, 672, 24))}${toggle("solo", i, tr.solo, raw(X(c) + 40, 24, 672, 24))}
				<div class="cell c dest${separated.has(i) ? " own" : ""}" style="${box(c, 1, 712, 16)}" title="${outputHelp(i)}">${separated.has(i) ? "Out " + two(i + 1) : "Main"}</div>`;
		}).join("");
		return `<div class="bg surface" style="${raw(0, 1280, 40, 760)}"></div>${strips}`;
	}

	const outputHelp = i => separated.has(i)
		? `Out ${two(i + 1)}: taken by the host, the track leaves Main. It carries the track after its effects and VOL, before PAN.`
		: `Main. For a separate output in Live: an audio track, Audio From "MD Drums", then "Out ${two(i + 1)}", Monitor In.`;

	// ---- screens ----
	function canvasOf(frame, name) {
		const c = frame.querySelector(`[data-screen="${name}"] canvas`);
		if(!c) return [null];
		const w = c.clientWidth, h = c.clientHeight;
		c.width = w * 2; c.height = h * 2;
		// A screen runs to the window's edge; its plot is inset 16 px from column 12's start and from column 16's end
		// (24 px from the window's edge), where its title starts and ends.
		const g = c.getContext("2d"); g.scale(2, 2); g.translate(16, 0);
		return [g, w - 40, h];
	}
	const cssOf = frame => { const ui = frame.querySelector(".ui"); return n => getComputedStyle(ui).getPropertyValue(n).trim(); };
	const plot = h => ({ top: 40, bottom: h - 24 });
	function graduation(g, css, x0, y0, x1, y1, text, tx, ty) {
		g.strokeStyle = css("--tick"); g.lineWidth = 1;
		g.beginPath(); g.moveTo(Math.round(x0) + 0.5, Math.round(y0) + 0.5); g.lineTo(Math.round(x1) + 0.5, Math.round(y1) + 0.5); g.stroke();
		if(text) { g.fillStyle = css("--ink-faint"); g.font = `11px ${css("--font")}`; g.fillText(text, tx, ty); }
	}

	// The shown machine's real hit (v20-hits.js: min/max per column of 0.8 s, rendered by the engine with its SYN
	// defaults), scaled by the last velocity. Dim until the track plays; on a hit, the part already heard turns to ink
	// behind a playhead that crosses the screen in 0.8 s (tick() redraws this screen alone meanwhile).
	function drawHit(frame, _screen = "hit") {
		const [g, w, h] = canvasOf(frame, _screen); if(!g) return;
		const css = cssOf(frame), hit = lastHit[sel], { top, bottom } = plot(h), mid = (top + bottom) / 2, amp = (bottom - top) / 2;
		const heard = hit ? Math.min(1, (performance.now() - hit.time) / 800) : 0;
		[0, 0.2, 0.4, 0.6].forEach(t => graduation(g, css, t / 0.8 * w, top, t / 0.8 * w, bottom, t ? `${t} s` : "0", t / 0.8 * w + 4, h - 8));
		graduation(g, css, 0, mid, w, mid);
		const data = (typeof MD_HITS !== "undefined" && MD_HITS[tracks[sel].machine]) || null;
		if(!data) { g.fillStyle = css("--ink-faint"); g.font = `12px ${css("--font")}`; g.fillText("no sound", 16, mid - 6); return; }
		const columns = data.length / 2, gain = (hit ? hit.velocity : 100) / 100;
		// Full scale is 1.0; the loudest hits peak near 0.45: draw 0.5 at the plot's edge.
		const scale = amp / 0.5 * gain, playX = heard * w, ink = css("--ink"), dim = css("--track");
		for(let x = 0; x < w; ++x) {
			const k = Math.min(columns - 1, Math.floor(x / w * columns)), lo = data[2 * k], hi = data[2 * k + 1];
			const y0 = mid - Math.min(amp, hi * scale), y1 = mid - Math.max(-amp, lo * scale);
			g.fillStyle = hit && x <= playX ? ink : dim;
			g.fillRect(x, y0, 1, Math.max(1, y1 - y0));
		}
		// The amplitude envelope, in accent, over the hit: the real hit's peaks (an instant attack, a 30 ms release), so at
		// the machine's defaults it hugs the waveform. DEC stretches it (a doubling per 16 steps from the default) and HOLD
		// holds its start (up to 0.4 s at 127): a model in this mockup, where the plug-in will re-render the hit instead.
		const syn = machineOf(sel).syn, defs = synDefaults(tracks[sel].machine), p = tracks[sel].p;
		const decI = syn.indexOf("DEC"), holdI = syn.indexOf("HOLD");
		const stretch = decI >= 0 ? Math.pow(2, (p[decI] - defs[decI]) / 16) : 1;
		const hold = holdI >= 0 ? Math.max(0, p[holdI] - defs[holdI]) / 127 * 0.5 : 0;	// of the 0.8 s screen
		const env = new Float32Array(columns), release = Math.exp(-1 / (columns * 0.03 / 0.8));
		for(let k = 0, e = 0; k < columns; ++k) { e = Math.max(Math.abs(data[2 * k]), Math.abs(data[2 * k + 1]), e * release); env[k] = e; }
		const envAt = s => env[Math.min(columns - 1, Math.floor((s < hold ? 0 : (s - hold) / stretch) * columns))];
		g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
		for(let x = 0; x <= w; ++x) { const y = mid - Math.min(amp, envAt(x / w) * scale); x ? g.lineTo(x, y) : g.moveTo(x, y); }
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
				[[100, "100"], [1000, "1k"], [10000, "10k"]].forEach(([f, text]) => graduation(g, css, xOf(f), top, xOf(f), bottom, text, xOf(f) + 4, h - 8));
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
				const top = 40, bottom = h - 80, mid = (top + bottom) / 2, amp = (bottom - top) / 2 * 0.9;
				graduation(g, css, 0, mid, w, mid, "0", 4, mid - 4);
				const depth = p[22] / 127, periods = 1 + p[21] / 127 * 5, mix = p[23] / 127;
				let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
				const steps = []; for(let i = 0; i < 64; ++i) steps.push(rnd() * 2 - 1);
				const shape = (s, ph) => { const f = ph - Math.floor(ph);
					return [1 - 4 * Math.abs(f - 0.5), 1 - 2 * f, f < 0.5 ? 1 : -1, 2 * f - 1, Math.exp(-4 * f) * 2 - 1, steps[Math.floor(ph * 2) % 64]][s]; };
				const wave = ph => (1 - mix) * shape(t.lfo.shape1, ph) + mix * shape(t.lfo.shape2, ph);
				g.beginPath(); g.strokeStyle = depth > 0 ? css("--accent") : css("--ink-faint"); g.lineWidth = 2;
				for(let x = 0; x <= w; ++x) { const y = mid - wave(x / w * periods) * Math.max(depth, 0.04) * amp; x ? g.lineTo(x, y) : g.moveTo(x, y); }
				g.stroke();
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
		const body = view === "MIX" ? mixView() : trackView(arrows, play);
		lfoPick = pick;
		const grid = showGrid || frame.dataset.grid === "on";
		frame.innerHTML = `<div class="ui" data-playmode="${play}" style="--font:${frame.dataset.font}">${top(view)}${body}${grid ? gridLayer() : ""}</div>`;
		if(view === "TRACK") browserOpen ? drawHit(frame, "preview") : drawScreens(frame);
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
			if(t === sel) { const vel = frame.querySelector("[data-vel]"); if(vel) vel.textContent = velocity; drawHit(frame); drawHit(frame, "preview"); }
			const flash = [...frame.querySelectorAll(`[data-playt="${t}"]`), ...(t === sel ? frame.querySelectorAll("[data-audition]") : [])];
			if(!reduceMotion) flash.forEach(n => { n.classList.add("trig"); setTimeout(() => n.classList.remove("trig"), 140); });
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
	const openBrowser = () => { browserOpen = true; browserFrom = tracks[sel].machine; lfoPick = null; };
	const cancelBrowser = () => { if(browserFrom !== null) tracks[sel].machine = browserFrom; browserOpen = false; renderAll(); };
	// A machine loaded takes its own SYN defaults, as on the Machinedrum.
	const choose = id => { tracks[sel].machine = id; tracks[sel].p.splice(0, 8, ...synDefaults(id)); renderAll(); if(autoListen) trigger(sel, 100); };
	const focusCurrent = () => document.querySelectorAll(".frame .mach.cur").forEach(m => m.focus({ preventScroll: true }));
	const stepMachine = d => { const i = machineList.indexOf(tracks[sel].machine), n = machineList.length; choose(machineList[(i + d + n) % n]); };
	// The LFO being assigned belongs to the track that started the assignment.
	let lfoOwner = 0;

	function act(target, e) {
		const frame = target.closest(".frame"); if(!frame) return false;
		const at = s => target.closest(s);
		const tab = at(".tab[data-view]"), mute = at("[data-mute]"), solo = at("[data-solo]"), chip = at("[data-shape1],[data-shape2],[data-modecycle]");
		if(tab) { frame.dataset.view = tab.dataset.view; browserOpen = false; renderAll(); return true; }
		if(at("[data-scale]")) { uiScale = uiScale >= 1.5 ? 1 : uiScale + 0.25; if(onScale) onScale(uiScale); renderAll(); return true; }
		if(mute) { const i = +mute.dataset.mute; tracks[i].mute = !tracks[i].mute; renderAll(); return true; }
		if(solo) { const i = +solo.dataset.solo; tracks[i].solo = !tracks[i].solo; renderAll(); return true; }
		if(chip) {
			const l = tracks[sel].lfo;
			if(chip.dataset.shape1) l.shape1 = +chip.dataset.shape1;
			else if(chip.dataset.shape2) l.shape2 = +chip.dataset.shape2;
			else l.mode = (l.mode + 1) % modes.length;
			renderAll(); return true;
		}
		if(at("[data-lfomenu]")) { lfoPick = lfoPick === "menu" ? null : "menu"; renderAll(); return true; }
		if(at("[data-lfotrack]")) { tracks[sel].lfo.track = +at("[data-lfotrack]").dataset.lfotrack; renderAll(); return true; }
		if(at("[data-lfoparam]")) { tracks[sel].lfo.param = +at("[data-lfoparam]").dataset.lfoparam; lfoPick = null; renderAll(); return true; }
		if(at("[data-lfoassign]")) { lfoPick = lfoPick === "assign" ? null : "assign"; lfoOwner = sel; renderAll(); return true; }
		if(lfoPick === "assign" && at("[data-i]") && at("[data-i]").dataset.i !== "") {
			const l = tracks[lfoOwner].lfo; l.track = sel; l.param = +at("[data-i]").dataset.i; lfoPick = null; sel = lfoOwner; renderAll(); return true;
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
		if(fader) { const tr = tracks[+fader.dataset.fader]; return { get: () => tr.level, set: v => tr.level = v, px: fader.getBoundingClientRect().height / 127 }; }
		if(mix) { const tr = tracks[+mix.dataset.track], i = +mix.dataset.mix; return { get: () => tr.p[i], set: v => tr.p[i] = v, px: 1.5 }; }
		if(vel) return { get: () => auditionVelocity, set: v => auditionVelocity = Math.max(1, v), px: 1.5 };
		if(knob && knob.dataset.i !== "") { const i = +knob.dataset.i; return { get: () => tracks[sel].p[i], set: v => tracks[sel].p[i] = v, px: 1.5, reset: () => tracks[sel].p[i] = defaults[i] }; }
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
		c.set(clamp(c.get() + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 1 : 2)));
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
		const c = e.target.closest && e.target.closest(".frame") ? control(e.target) : null;
		if(c && c.reset) { c.reset(); renderAll(); }
	}
	function onOver(e) {
		const mach = e.target.closest && e.target.closest("[data-mach]"); if(!mach) return;
		const m = byId.get(+mach.dataset.mach), hint = mach.closest(".frame").querySelector("[data-bhint]");
		if(hint) hint.textContent = `${m.name} · ${families[m.family]} · ${m.syn.filter(Boolean).join(" ") || "no SYN parameter"}`;
	}

	function start() {
		document.addEventListener("pointerdown", onDown);
		document.addEventListener("pointermove", onMove);
		document.addEventListener("pointerup", () => drag = null);
		document.addEventListener("dblclick", onDouble);
		document.addEventListener("wheel", onWheel, { passive: false });
		document.addEventListener("keydown", onKey);
		document.addEventListener("pointerover", onOver);
		renderAll();
		if(document.fonts) document.fonts.ready.then(renderAll);
		requestAnimationFrame(tick);
	}

	return { start, renderAll, setGrid: on => { showGrid = on; renderAll(); }, setDemo, onScaleChange: f => onScale = f, trigger,
		select: t => { sel = t; renderAll(); }, setMachine: id => { tracks[sel].machine = id; renderAll(); },
		setBrowser: on => { if(on) openBrowser(); else browserOpen = false; renderAll(); }, machines: () => machineList.slice(),
		setLfoPick: mode => { lfoPick = mode; lfoOwner = sel; renderAll(); },
		hits: () => lastHit.map(h => h && h.velocity), grid: { X, W } };
})();
