// MD Drums v15 mockup (v15-ui.css; machine data in v6-machines.js). One shared state drawn into every element with
// class "frame" (data-view: PISTE or MIX; data-font). The track list's scopes, the meters and the trig flashes
// run on an animation loop that draws into the existing DOM; everything else re-renders on a change. v8: one column per
// page of the track (SYN, EFX, ROUTING), each with its knobs as the Machinedrum lays them out (2 x 4), its groups named
// over their knobs, and its screen right under the knobs it shows. v9: the track head in a band of its own. v10: one
// title in it, the machine, with ▶ against it; the kit level only in MIX. v11: the pages' titles alone; how groups
// show is the frame's data-groups (none, names, rules, all), compared in v11-var-groups.html. v12: the machine browser
// plays what it loads and stays open, with a preview column; ROM's empty slots are not offered. v13: no SORTIES view,
// MIX names each track's output as the host does. v14: the browser's families say what they are under their codes.
// v15: golden proportions (type 13/21/34, screens 288 x 180, pages 320 x 520); ROM back with the bank's 32 samples.
"use strict";

const MD = (() => {
	// What each family is, in plain words, under its code in the browser: the Machinedrum's codes say nothing to someone
	// who does not know it, and E12 is where the samples are.
	const kinds = { GND: "générateurs", TRX: "analogique", EFM: "FM", E12: "samples d'usine", "P-I": "physique", ROM: "samples chargés" };
	const families = { GND: "générateur", TRX: "synthèse analogique modélisée", EFM: "synthèse FM", E12: "sample 12 bits intégré",
		"P-I": "modélisation physique", ROM: "sample de la banque UW" };
	const familyOrder = ["GND", "TRX", "EFM", "E12", "P-I", "ROM"];
	const byId = new Map(MD_MACHINES.map(m => [m.id, m]));
	// The UW bank in the flash image (0x100000: magic 0xABCD, then the count) holds 32 samples: ROM-01..32 play them,
	// ROM-33..48 have nothing to play and are not offered.
	const romSlots = 32;
	const offered = m => familyOrder.includes(m.family) && (m.family !== "ROM" || +m.name.slice(4) <= romSlots);
	// The machines offered, in id order (which is also the families' order): what ‹ › and the browser's arrows step through.
	const machineList = MD_MACHINES.filter(offered).map(m => m.id);
	const efxNames = ["AMD","AMF","EQF","EQG","FLTF","FLTW","FLTQ","SRR"];
	const routeNames = ["DIST","VOL","PAN","DEL","REV","LFOS","LFOD","LFOM"];
	const shapes = ["TRI","SAW","SQR","RMP","EXP","RND"];
	const modes = ["FREE","TRIG","HOLD"];
	const defaults = [64,64,64,64,64,64,64,64, 0,0,64,64,0,127,0,0, 0,100,64,0,0,64,0,0];
	const sendsPending = new Set([19, 20]);	// DEL, REV: nothing to send to until the master effects exist
	const two = n => String(n).padStart(2, "0");
	// The host parameters' names (parameter-spec.md), which the controls carry as their id: t<n>_<name>.
	const specNames = ["SYN1","SYN2","SYN3","SYN4","SYN5","SYN6","SYN7","SYN8", ...efxNames, ...routeNames];
	const pid = (t, name) => `t${t + 1}_${name}`;
	const reduceMotion = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

	// Each page's knobs in the order of the Machinedrum's encoders (A-D over E-H), and its groups: name, row, first slot,
	// slot after the last, waiting for the master effects. ROUTING moves REV under DEL, so that the sends stay together
	// and the LFO's three knobs sit right over its screen.
	const layouts = {
		syn: { order: [0, 1, 2, 3, 4, 5, 6, 7], groups: [] },
		efx: { order: [8, 9, 10, 11, 12, 13, 14, 15], groups: [["AM", 1, 0, 2], ["EQ", 1, 2, 4], ["Filtre", 2, 0, 3]] },
		rte: { order: [16, 17, 18, 19, 21, 22, 23, 20], groups: [["Canal", 1, 0, 3], ["Envois", 1, 3, 4, true], ["LFO", 2, 0, 3]] },
	};

	// The kit every track starts with: TRX-BD..TRX-B2, then EFM-BD, -SD, -HH.
	const startKit = [16,17,18,19,20,21,22,23,24,25,26,27,28,32,33,38];
	const tracks = startKit.map((id, t) => ({ machine: id, level: [100,96,88,90,70,64,82,76,60,58,66,72,100,92,84,80][t], mute: t === 12, solo: false,
		p: defaults.map((d, i) => i < 8 ? (d + t * 9 + i * 13) % 128 : d), lfo: { track: t, param: 12, shape1: 0, shape2: 0, mode: 0 } }));
	tracks[0].p = [52,78,96,40,64,12,30,8, 0,0,64,64,18,70,40,0, 0,100,64,0,0,64,38,0];

	let sel = 0, showGrid = false, browserOpen = false, lastNote = null, onScale = null, uiScale = 1;
	// The browser: the machine the track had when it opened (Annuler goes back to it), and whether choosing plays.
	let browserFrom = null, autoListen = true;
	const separated = new Set([0, 1]);	// as if the host had taken outputs 01 and 02
	const lastHit = tracks.map(() => null);	// { time, velocity }
	const history = tracks.map(() => new Float32Array(80));

	const machineOf = t => byId.get(tracks[t].machine) || { name: "?", family: "GND", syn: Array(8).fill("") };
	const names = t => [...machineOf(t).syn, ...efxNames, ...routeNames];
	// A machine's name inside its family column: "BD" for TRX-BD, "01" for ROM-01; GND--- makes no sound.
	const model = m => m.name === "GND---" ? "aucune" : m.name.slice(m.family.length + 1);

	// Loudness of a track right now: its last hit, decaying at a rate from DEC (SYN2) and the kind of sound.
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

	// A flat knob: a 270 degree track, the value as an arc, a pointer. No body, no shading.
	function knobSvg(size, value) {
		const r = size / 2 - 3, c = size / 2, a0 = 0.75 * Math.PI, sweep = 1.5 * Math.PI, a = a0 + sweep * value / 127;
		const p = (t, rr) => [c + rr * Math.cos(t), c + rr * Math.sin(t)];
		const arc = (from, to) => { const [x0, y0] = p(from, r), [x1, y1] = p(to, r); return `M${x0} ${y0} A${r} ${r} 0 ${to - from > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
		const [x0, y0] = p(a, r * 0.35), [x1, y1] = p(a, r - 4);
		return `<svg width="${size}" height="${size}" aria-hidden="true"><path d="${arc(a0, a0 + sweep)}" stroke="var(--track)" stroke-width="2" fill="none"/>
			${value > 0 ? `<path d="${arc(a0, a)}" stroke="var(--ink)" stroke-width="2.5" fill="none"/>` : ""}
			<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}" stroke="var(--ink)" stroke-width="2"/></svg>`;
	}

	// A mute or solo switch; _withId: this one carries the parameter's id (one per view).
	const toggle = (kind, i, on, withId) => `<div class="cell toggle ${kind}${on ? " on" : ""}"${withId ? ` id="${pid(i, kind)}"` : ""} data-${kind}="${i}" tabindex="0" role="switch" aria-checked="${on}"
		aria-label="${kind === "mute" ? "Muet" : "Solo"}, piste ${two(i + 1)}"><i>${kind === "mute" ? "M" : "S"}</i></div>`;

	function top(view) {
		const tabs = ["PISTE", "MIX"].map((n, k) => `<div class="cell tab t${k + 1}${n === view ? " on" : ""}" data-view="${n}" tabindex="0" role="tab" aria-selected="${n === view}"><span>${n}</span></div>`).join("");
		return `<div class="top"><div class="cell logo">MD<span>DRUMS</span></div>${tabs}
			<div class="cell scale" data-scale tabindex="0" title="Taille de la fenêtre">${Math.round(uiScale * 100)} %</div>
			<div class="cell rate num">44,1 kHz · 0,5 ms</div>
			<div class="cell out"><span class="label">Main</span><div class="bars" data-meter><i><b></b></i><i><b></b></i></div></div></div>`;
	}

	function trackList() {
		const rows = tracks.map((tr, i) => `<div class="trow${i === sel ? " sel" : ""}" data-t="${i}">
			<div class="cell n" data-playt="${i}" tabindex="0" aria-label="Écouter la piste ${two(i + 1)}" title="Écouter">${two(i + 1)}</div>
			<div class="cell m" data-pick="${i}" tabindex="0">${byId.get(tr.machine).name}</div>
			<div class="cell scope"><canvas data-scope="${i}"></canvas></div>
			${toggle("mute", i, tr.mute, true)}${toggle("solo", i, tr.solo, true)}</div>`).join("");
		return `<div class="tracks"><div class="cell head"><span class="label">Pistes</span><span class="label">Kit 01</span></div>${rows}
			<div class="cell foot hint">${separated.size} sorties prises par l'hôte</div></div>`;
	}

	function head() {
		const m = machineOf(sel), own = separated.has(sel), hit = lastHit[sel];
		// A band of its own over the pages. Over SYN: ▶ on its first slot (it plays this track, so it sits against its
		// name), the track's number small over the machine, the one title, then ‹ ›. Over EFX: what the machine is, where
		// the track goes, its last velocity. Mute and solo stay in the list; the kit level in MIX.
		return `<div class="dhead">
			<div class="cell play" data-play tabindex="0" role="button" aria-label="Écouter la piste ${two(sel + 1)}" title="Écouter la piste (vélocité 100)"><div class="ring"><svg width="14" height="16" aria-hidden="true"><path d="M2 1 L13 8 L2 15 Z"/></svg></div></div>
			<div class="cell name"><span class="label num">Piste ${two(sel + 1)}</span>
				<span class="machine" id="${pid(sel, "machine")}" data-browse tabindex="0" aria-label="Choisir une machine">${m.name}</span></div>
			<div class="cell step prev" data-step="-1" tabindex="0" aria-label="Machine précédente">‹</div>
			<div class="cell step next" data-step="1" tabindex="0" aria-label="Machine suivante">›</div>
			<div class="cell info meta"><span>${m.family} · ${families[m.family]}</span>
				<span>Sortie <b>${own ? two(sel + 1) + ", prise par l'hôte" : "Main"}</b> · Vélocité <b class="num" data-vel>${hit ? hit.velocity : "—"}</b></span></div></div>`;
	}

	// The LFO's two shape rows, at the foot of its screen: the row's number, then the 6 shapes (the parameter's id on
	// the group). LFOM mixes shape 1 into shape 2.
	const shapeRows = t => `<div class="lfoshapes">${[1, 2].map(row => {
		const current = t.lfo[`shape${row}`];
		return `<div class="srow"><span class="label" title="LFOM mêle la forme 1 à la forme 2">${row}</span><div class="chips" id="${pid(sel, `lfoShape${row}`)}" role="radiogroup" aria-label="Forme ${row} du LFO">${shapes.map((s, i) =>
			`<div class="cell chip${i === current ? " on" : ""}" data-shape${row}="${i}" tabindex="0" role="radio" aria-checked="${i === current}">${s}</div>`).join("")}</div></div>`;
	}).join("")}</div>`;

	// _groups: how a page shows its groups (the frame's data-groups): "none", names without rules ("names"), names
	// and rules ("rules", v10's), or rules in SYN as well ("all"), for the comparison in v11-var-groups.html.
	function pages(_groups) {
		const t = tracks[sel], n = names(sel), m = machineOf(sel);
		const lfoTarget = t.lfo.track === sel ? t.lfo.param : -1;
		const knobs = order => order.map((i, pos) => {
			const at = ` r${pos < 4 ? 1 : 2} s${pos % 4 + 1}`, unused = (i < 8 && !n[i]) || sendsPending.has(i);
			const why = i < 8 && !n[i] ? `${m.name} n'utilise pas ce paramètre` : sendsPending.has(i) ? "Effets maîtres à venir" : "";
			const label = n[i] || "—", cls = unused ? " unused" : "";
			return `<div class="cell knob${at}${cls}" id="${pid(sel, specNames[i])}" data-i="${unused ? "" : i}"${unused ? ` title="${why}"` : ` tabindex="0" role="slider" aria-label="${label}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${t.p[i]}"`}>${knobSvg(48, t.p[i])}</div>
				<div class="cell kl${at}${i === lfoTarget ? " lfo" : ""}${cls}"${why ? ` title="${why}"` : ""}><span class="label">${label}</span><span class="value">${unused && i < 8 ? "" : t.p[i]}</span></div>`;
		}).join("");
		const groups = (cls) => {
			if(_groups === "none") return "";
			const list = _groups === "all" && cls === "syn" ? [["", 1, 0, 4], ["", 2, 0, 4]] : layouts[cls].groups;
			return list.map(([name, row, a, b, pending]) => `<div class="cell grp r${row}${b - a === 1 ? " one" : ""}${_groups === "names" ? " bare" : ""}${name ? "" : " sym"}${pending ? " pending" : ""}" style="grid-column:${a + 1}/${b + 1}"${pending ? ` title="Effets maîtres à venir"` : ""}>${name}</div>`).join("");
		};
		const page = (cls, title, screen) => `<div class="pg ${cls}"><div class="cell title"><b>${title}</b></div>
			${groups(cls)}${knobs(layouts[cls].order)}${screen}</div>`;
		const destName = names(t.lfo.track)[t.lfo.param] || efxNames[0];
		return page("syn", "SYN",
				`<div class="screen" data-screen="hit"><canvas></canvas><div class="stitle"><span class="label">Frappe</span><span class="note hint">dernière note</span></div></div>`)
			+ page("efx", "EFX",
				`<div class="screen" data-screen="filter"><canvas></canvas><div class="stitle"><span class="label">Filtre · EQ</span>${t.p[15] > 0 ? `<span class="note hint num">SRR ${t.p[15]}</span>` : ""}</div></div>`)
			+ page("rte", "ROUTING",
				`<div class="screen" data-screen="lfo"><canvas></canvas><div class="stitle"><span class="label">LFO →</span>
					<span id="${pid(sel, "lfoTrack")}" data-lfotrack tabindex="0" title="Piste modulée">T${two(t.lfo.track + 1)}</span>
					<span id="${pid(sel, "lfoParam")}" data-lfoparam tabindex="0" title="Paramètre modulé">${destName}</span>
					<span class="note"><span id="${pid(sel, "lfoMode")}" data-modecycle tabindex="0" title="Mode : FREE, TRIG ou HOLD">${modes[t.lfo.mode]}</span></span></div>${shapeRows(t)}</div>`);
	}

	function browser() {
		const cur = tracks[sel].machine, m = machineOf(sel), from = byId.get(browserFrom ?? cur);
		// The families on the pages' three columns, each named over its machines like a group over its knobs, what it is
		// under its code: GND and TRX; EFM, E12 and P-I; ROM's 32 samples (40 px each) over the preview of the machine
		// chosen, whose screen sits where the pages put theirs. [first column, column after the last, sub-columns]
		const place = { GND: [1, 5, 1], TRX: [5, 9, 2], EFM: [10, 12, 1], E12: [12, 16, 2], "P-I": [16, 18, 1], ROM: [19, 27, 8] };
		const fams = familyOrder.map(f => {
			const list = MD_MACHINES.filter(x => x.family === f && offered(x)), [a, b, sub] = place[f], rows = Math.ceil(list.length / sub);
			const items = list.map(x => `<div class="cell mach${x.id === cur ? " cur" : ""}" data-mach="${x.id}" tabindex="0" title="${x.name}">${model(x)}</div>`).join("");
			return `<div class="cell grp fam${b - a === 2 ? " one" : ""}" style="grid-column:${a}/${b}">${f}<span class="num">${list.length}</span></div>
				<div class="cell kind" style="grid-column:${a}/${b}" title="${families[f]}">${kinds[f]}</div>
				<div class="col${f === "ROM" ? " rom" : ""}" style="grid-column:${a}/${b};grid-template-rows:repeat(${rows},var(--m));grid-template-columns:repeat(${sub},1fr)">${items}</div>`;
		}).join("");
		return `<div class="browser"><div class="cell bhead"><b>Choisir une machine</b><span>piste ${two(sel + 1)}</span><span data-bhint></span></div>
			<div class="cell bact"><span data-cancel tabindex="0" title="Revenir à ${from.name} (Échap)">Annuler</span><span class="keep" data-keep tabindex="0" title="Garder ${m.name} (Entrée)">Garder</span></div>
			${fams}
			<div class="cell grp fam pvhead">Aperçu</div>
			<div class="cell pvname"><b>${m.name}</b><span class="meta">${kinds[m.family]}</span></div>
			<div class="screen pv" data-screen="preview"><canvas></canvas><div class="stitle"><span class="label">Frappe</span><span class="note hint">${m.name}</span></div></div>
			<div class="cell pvauto${autoListen ? " on" : ""}" data-autolisten tabindex="0" role="switch" aria-checked="${autoListen}"><i></i><span>Écouter en choisissant</span></div></div>`;
	}

	function trackView(_groups) {
		const note = lastNote ? `${lastNote.note} · vél. ${lastNote.velocity}` : "—";
		const gestures = browserOpen
			? `<b>Clic</b> : charger et écouter · <b>↑ ↓</b> : machine voisine · <b>← →</b> : famille voisine · <b>Entrée</b> : garder · <b>Échap</b> : annuler`
			: `Tirer : régler · <b>Maj</b> : réglage fin · <b>Double-clic</b> : valeur par défaut · <b>Clic sur un numéro</b> : écouter`;
		return `${trackList()}<div class="detail">${head()}${browserOpen ? browser() : pages(_groups)}
			<div class="cell dfoot hint"><span>${gestures}</span>
			<span class="last">Dernière note <b class="num" data-last>${note}</b></span></div></div>`;
	}

	function mixView() {
		const strips = tracks.map((tr, i) => `<div class="strip${i === sel ? " sel" : ""}" data-t="${i}">
			<div class="cell n" data-playt="${i}" tabindex="0" aria-label="Écouter la piste ${two(i + 1)}">${two(i + 1)}</div>
			<div class="cell m">${byId.get(tr.machine).name}</div>
			<div class="cell pan" id="${pid(i, "PAN")}" data-mix="18" data-track="${i}" tabindex="0" role="slider" aria-label="PAN, piste ${two(i + 1)}">${knobSvg(48, tr.p[18])}<span class="label">PAN</span></div>
			<div class="cell fz"><div class="vu"><b data-vu="${i}"></b></div><div class="fader" id="${pid(i, "level")}" data-fader="${i}" tabindex="0" role="slider" aria-label="Niveau kit, piste ${two(i + 1)}"><div class="rail"></div>
				<div class="capf" style="bottom:calc(${tr.level / 127 * 100}% - 5px)"></div></div></div>
			<div class="cell val value">${tr.level}</div>${toggle("mute", i, tr.mute, true)}${toggle("solo", i, tr.solo, true)}
			<div class="cell dest${separated.has(i) ? " own" : ""}" title="${outputHelp(i)}">${separated.has(i) ? "Out " + two(i + 1) : "Main"}</div></div>`).join("");
		return `<div class="mix">${strips}</div>`;
	}

	// Where a track's sound goes, for MIX's last line: its own output when the host has taken it (Out nn, after its
	// effects and VOL, before PAN, like the Machinedrum's individual outputs), Main otherwise.
	const outputHelp = i => separated.has(i)
		? `Out ${two(i + 1)} : prise par l'hôte, la piste quitte Main. Elle porte la piste après ses effets et son VOL, avant le PAN.`
		: `Main. Pour une sortie séparée dans Live : piste audio, Audio From « MD Drums », puis « Out ${two(i + 1)} », Monitor sur In.`;

	// ---- screens ----
	function canvasOf(frame, name) {
		const c = frame.querySelector(`[data-screen="${name}"] canvas`);
		if(!c) return [null];
		const w = c.clientWidth, h = c.clientHeight;
		c.width = w * 2; c.height = h * 2;
		const g = c.getContext("2d"); g.scale(2, 2);
		return [g, w, h];
	}
	const cssOf = frame => { const ui = frame.querySelector(".ui"); return n => getComputedStyle(ui).getPropertyValue(n).trim(); };
	// A screen draws only graduations that mean something, each with its label: the plot sits below the title line.
	const plot = h => ({ top: 32, bottom: h - 22 });
	function graduation(g, css, x0, y0, x1, y1, text, tx, ty) {
		g.strokeStyle = css("--tick"); g.lineWidth = 1;
		g.beginPath(); g.moveTo(Math.round(x0) + 0.5, Math.round(y0) + 0.5); g.lineTo(Math.round(x1) + 0.5, Math.round(y1) + 0.5); g.stroke();
		if(text) { g.fillStyle = css("--ink-faint"); g.font = `11px ${css("--font")}`; g.fillText(text, tx, ty); }
	}

	// The last hit of the shown track, in the SYN page's screen or (_screen "preview") in the browser's.
	function drawHit(frame, _screen = "hit") {
		const [g, w, h] = canvasOf(frame, _screen); if(!g) return;
		const css = cssOf(frame), p = tracks[sel].p, hit = lastHit[sel], { top, bottom } = plot(h), mid = (top + bottom) / 2, amp = (bottom - top) / 2 * 0.92;
		// Time: the screen spans 0.8 s from the note on.
		[0, 0.2, 0.4, 0.6].forEach(t => graduation(g, css, t / 0.8 * w, top, t / 0.8 * w, bottom, t ? `${String(t).replace(".", ",")} s` : "0", t / 0.8 * w + 4, h - 7));
		graduation(g, css, 0, mid, w, mid);
		const gain = hit ? hit.velocity / 127 : 0.8;
		const decay = 0.02 + p[1] / 127 * 0.6, freq = 2 + p[0] / 127 * 20, amd = p[8] / 127, amf = 1 + p[9] / 6;
		g.beginPath(); g.strokeStyle = css("--wave"); g.lineWidth = 1;
		for(let x = 0; x <= w; ++x) {
			const s = x / w, env = gain * Math.exp(-s / decay) * (1 - amd * 0.5 * (1 + Math.sin(2 * Math.PI * amf * s)));
			const y = mid - env * amp * Math.sin(2 * Math.PI * freq * s * (1 + (1 - s) * p[2] / 127));
			x ? g.lineTo(x, y) : g.moveTo(x, y);
		}
		g.stroke();
		g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
		for(let x = 0; x <= w; ++x) { const s = x / w, y = mid - gain * Math.exp(-s / decay) * amp; x ? g.lineTo(x, y) : g.moveTo(x, y); }
		g.stroke();
	}

	function drawScreens(frame) {
		drawHit(frame);
		const css = cssOf(frame), t = tracks[sel], p = t.p;
		{	// Filter (band from FLTF, width FLTW, resonance FLTQ) and EQ peak (EQF, EQG), over 20 Hz - 20 kHz
			const [g, w, h] = canvasOf(frame, "filter");
			if(g) {
				const { top, bottom } = plot(h), zero = top + (bottom - top) * 0.45, perDb = (bottom - top) * 0.45 / 24;
				const xOf = f => Math.log(f / 20) / Math.log(1000) * w;
				[[100, "100"], [1000, "1k"], [10000, "10k"]].forEach(([f, text]) => graduation(g, css, xOf(f), top, xOf(f), bottom, text, xOf(f) + 4, h - 7));
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
		{	// LFO: periods at LFOS, scaled by LFOD, shape 1 mixed into shape 2 by LFOM; its zero line. The shape rows take
			// the foot of the screen.
			const [g, w, h] = canvasOf(frame, "lfo");
			if(g) {
				const top = 32, bottom = h - 76, mid = (top + bottom) / 2, amp = (bottom - top) / 2 * 0.9;
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

	function renderFrame(frame) {
		const view = frame.dataset.view || "PISTE";
		const body = view === "MIX" ? mixView() : trackView(frame.dataset.groups || "rules");
		frame.innerHTML = `<div class="ui${showGrid ? " showgrid" : ""}" style="--font:${frame.dataset.font}">${top(view)}${body}</div>`;
		if(view === "PISTE") browserOpen ? drawHit(frame, "preview") : drawScreens(frame);
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
			const last = frame.querySelector("[data-last]"); if(last) last.textContent = `${lastNote.note} · vél. ${velocity}`;
			if(t === sel) { const vel = frame.querySelector("[data-vel]"); if(vel) vel.textContent = velocity; drawHit(frame); drawHit(frame, "preview"); }
			// The track's number flashes in the list, and ▶ in the head when the track is the one shown.
			const flash = [...frame.querySelectorAll(`[data-playt="${t}"]`), ...(t === sel ? frame.querySelectorAll(".dhead [data-play]") : [])];
			if(!reduceMotion) flash.forEach(n => { n.classList.add("trig"); setTimeout(() => n.classList.remove("trig"), 140); });
		});
	}

	let lastTick = performance.now();
	function tick(now) {
		const steps = Math.max(1, Math.min(8, Math.round((now - lastTick) / (1000 / 60))));
		lastTick = now;
		const amps = tracks.map((_, t) => amplitude(t, now));
		history.forEach((h, t) => { h.copyWithin(0, steps); h.fill(amps[t], h.length - steps); });
		document.querySelectorAll(".frame").forEach(frame => {
			const css = cssOf(frame);
			frame.querySelectorAll("canvas[data-scope]").forEach(c => {
				const t = +c.dataset.scope, h = history[t], w = c.clientWidth, ht = c.clientHeight;
				if(c.width !== w * 2) { c.width = w * 2; c.height = ht * 2; }
				const g = c.getContext("2d"); g.setTransform(2, 0, 0, 2, 0, 0); g.clearRect(0, 0, w, ht);
				g.strokeStyle = t === sel ? css("--ink") : css("--wave"); g.lineWidth = 1; g.beginPath();
				for(let x = 0; x < w; ++x) { const a = h[Math.floor(x / w * h.length)], y = a * (ht / 2 - 2); if(y < 0.5) continue; g.moveTo(x + 0.5, ht / 2 - y); g.lineTo(x + 0.5, ht / 2 + y); }
				g.stroke();
				const newest = h[h.length - 1];
				if(newest > 0.02) { g.fillStyle = css("--accent"); const y = newest * (ht / 2 - 2); g.fillRect(w - 2, ht / 2 - y, 2, y * 2); }
			});
			frame.querySelectorAll("[data-vu]").forEach(b => b.style.height = Math.min(100, amps[+b.dataset.vu] * 100) + "%");
			const mainLevel = Math.min(1, amps.reduce((s, a, t) => s + (separated.has(t) ? 0 : a), 0) * 0.6);
			frame.querySelectorAll("[data-meter] b").forEach((b, i) => b.style.width = Math.min(100, mainLevel * (i ? 96 : 100)) + "%");
		});
		requestAnimationFrame(tick);
	}

	// A 16-step demo pattern at 120 BPM, as a DAW clip would send it.
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

	const openBrowser = () => { browserOpen = true; browserFrom = tracks[sel].machine; };
	const cancelBrowser = () => { if(browserFrom !== null) tracks[sel].machine = browserFrom; browserOpen = false; renderAll(); };
	// A machine chosen in the browser or with ‹ ›: loaded at once and played, unless the user turned listening off.
	const choose = id => { tracks[sel].machine = id; renderAll(); if(autoListen) trigger(sel, 100); };
	const focusCurrent = () => document.querySelectorAll(".frame .mach.cur").forEach(m => m.focus({ preventScroll: true }));

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
		if(at("[data-lfotrack]")) { const l = tracks[sel].lfo; l.track = (l.track + 1) % 16; renderAll(); return true; }
		if(at("[data-lfoparam]")) { const l = tracks[sel].lfo; l.param = (l.param + 1) % 24; renderAll(); return true; }
		if(at("[data-play]")) { trigger(sel, 100); return true; }
		// Another track: the browser keeps what it chose for this one and closes.
		if(at("[data-playt]")) { const t = +at("[data-playt]").dataset.playt; if(t !== sel) { sel = t; browserOpen = false; renderAll(); } trigger(t, 100); return true; }
		if(at("[data-pick]")) { sel = +at("[data-pick]").dataset.pick; browserOpen = false; renderAll(); return true; }
		if(at("[data-step]")) { const i = machineList.indexOf(tracks[sel].machine), n = machineList.length;
			choose(machineList[(i + +at("[data-step]").dataset.step + n) % n]); return true; }
		if(at("[data-browse]")) { if(browserOpen) browserOpen = false; else openBrowser(); renderAll(); return true; }
		if(at("[data-keep]")) { browserOpen = false; renderAll(); return true; }
		if(at("[data-cancel]")) { cancelBrowser(); return true; }
		if(at("[data-autolisten]")) { autoListen = !autoListen; renderAll(); return true; }
		if(at("[data-mach]")) { choose(+at("[data-mach]").dataset.mach); return true; }
		const strip = at(".strip[data-t]");
		if(strip && !at("[data-mix],[data-fader]")) { sel = +strip.dataset.t; renderAll(); return true; }
		return false;
	}

	// What a drag or a key changes: its getter and setter, or null.
	function control(target) {
		const at = s => target.closest(s);
		const fader = at("[data-fader]"), mix = at("[data-mix]"), level = at("[data-level]"), knob = at("[data-i]");
		if(fader) { const tr = tracks[+fader.dataset.fader]; return { get: () => tr.level, set: v => tr.level = v, px: fader.getBoundingClientRect().height / 127 }; }
		if(mix) { const tr = tracks[+mix.dataset.track], i = +mix.dataset.mix; return { get: () => tr.p[i], set: v => tr.p[i] = v, px: 1.5 }; }
		if(level) return { get: () => tracks[sel].level, set: v => tracks[sel].level = v, px: 1.5 };
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
		// Shift: four times finer
		const px = drag.c.px * (e.shiftKey ? 4 : 1);
		drag.c.set(clamp(drag.start + (drag.y - e.clientY) / px));
		renderAll();
	}
	function onWheel(e) {
		const c = e.target.closest && e.target.closest(".frame") ? control(e.target) : null;
		if(!c) return;
		e.preventDefault();
		c.set(clamp(c.get() + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 1 : 2)));
		renderAll();
	}
	// In the browser: ↑ ↓ the neighbouring machine, ← → the first of the neighbouring family, each one chosen (and so
	// played); Entrée keeps, Échap goes back to the machine the browser opened on.
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
		const el = document.activeElement; if(!el || !el.closest || !el.closest(".frame")) return;
		if(browserOpen && browserKey(e, el)) { e.preventDefault(); return; }
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
		if(hint) hint.textContent = `${m.name} · ${families[m.family]} · ${m.syn.filter(Boolean).join(" ") || "aucun paramètre SYN"}`;
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
		// For the checks: select a track, set a machine, open or close the browser, the machines offered, the last hits.
		select: t => { sel = t; renderAll(); }, setMachine: id => { tracks[sel].machine = id; renderAll(); },
		setBrowser: on => { if(on) openBrowser(); else browserOpen = false; renderAll(); }, machines: () => machineList.slice(),
		hits: () => lastHit.map(h => h && h.velocity) };
})();
