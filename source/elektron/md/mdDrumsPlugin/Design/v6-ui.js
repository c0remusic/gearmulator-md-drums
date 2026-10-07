// MD Drums v6 mockup (v6-ui.css; machine data in v6-machines.js). One shared state drawn into every element with class
// "frame" (data-view: PISTE, MIX or SORTIES; data-font). The track list's scopes, the meters and the trig flashes run
// on an animation loop that draws into the existing DOM; everything else re-renders on a change.
"use strict";

const MD = (() => {
	const families = { GND: "générateur", TRX: "synthèse analogique modélisée", EFM: "synthèse FM", E12: "sample 12 bits intégré",
		"P-I": "modélisation physique", ROM: "sample de l'image flash" };
	const familyOrder = ["GND", "TRX", "EFM", "E12", "P-I", "ROM"];
	const byId = new Map(MD_MACHINES.map(m => [m.id, m]));
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

	// The kit every track starts with: TRX-BD..TRX-B2, then EFM-BD, -SD, -HH.
	const startKit = [16,17,18,19,20,21,22,23,24,25,26,27,28,32,33,38];
	const tracks = startKit.map((id, t) => ({ machine: id, level: [100,96,88,90,70,64,82,76,60,58,66,72,100,92,84,80][t], mute: t === 12, solo: false,
		p: defaults.map((d, i) => i < 8 ? (d + t * 9 + i * 13) % 128 : d), lfo: { track: t, param: 12, shape1: 0, shape2: 0, mode: 0 } }));
	tracks[0].p = [52,78,96,40,64,12,30,8, 0,0,64,64,18,70,40,0, 0,100,64,0,0,64,38,0];

	let sel = 0, showGrid = false, browserOpen = false, lastNote = null, onScale = null, uiScale = 1;
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
		const tabs = ["PISTE", "MIX", "SORTIES"].map(n => `<div class="cell tab rr${n === view ? " on" : ""}" data-view="${n}" tabindex="0" role="tab" aria-selected="${n === view}">${n}</div>`).join("");
		return `<div class="top"><div class="cell logo rr">MD<span>DRUMS</span></div>
			<div class="cell status rr num">Kit 01 · T${two(sel + 1)} · ${machineOf(sel).name}</div>${tabs}
			<div class="cell scale rl rr" data-scale tabindex="0" title="Taille de la fenêtre">${Math.round(uiScale * 100)} %</div>
			<div class="cell rate rr num">44,1 kHz · 0,5 ms</div>
			<div class="cell out"><span class="caption">Main</span><div class="bars" data-meter><i><b></b></i><i><b></b></i></div></div></div>`;
	}

	function trackList() {
		const rows = tracks.map((tr, i) => `<div class="trow${i === sel ? " sel" : ""}" data-t="${i}">
			<div class="cell n" data-playt="${i}" tabindex="0" aria-label="Jouer la piste ${two(i + 1)}" title="Jouer">${two(i + 1)}</div>
			<div class="cell m" data-pick="${i}" tabindex="0">${byId.get(tr.machine).name}</div>
			<div class="cell scope"><canvas data-scope="${i}"></canvas></div>
			${toggle("mute", i, tr.mute, true)}${toggle("solo", i, tr.solo, true)}</div>`).join("");
		return `<div class="tracks"><div class="cell head"><span class="caption">Pistes</span><span class="caption">Kit 01</span></div>${rows}
			<div class="cell foot">${separated.size} sorties prises par l'hôte</div></div>`;
	}

	function head() {
		const t = tracks[sel], m = machineOf(sel), own = separated.has(sel), hit = lastHit[sel];
		return `<div class="dhead"><div class="cell big readout">${two(sel + 1)}</div>
			<div class="cell play" data-play tabindex="0" aria-label="Jouer la piste ${two(sel + 1)}"><svg width="18" height="20" aria-hidden="true"><path d="M2 1 L17 10 L2 19 Z" fill="var(--ink)"/></svg><span class="caption">Jouer</span></div>
			<div class="cell step prev" data-step="-1" tabindex="0" aria-label="Machine précédente">‹</div>
			<div class="cell machine" id="${pid(sel, "machine")}" data-browse tabindex="0" aria-label="Choisir une machine">${m.name}</div>
			<div class="cell step next" data-step="1" tabindex="0" aria-label="Machine suivante">›</div>
			<div class="cell family">${m.family} · ${families[m.family]}</div>
			<div class="cell stack output"><span class="caption">Sortie</span><b>${own ? two(sel + 1) + " · prise par l'hôte" : "Main"}</b></div>
			<div class="cell stack velocity"><span class="caption">Vélocité</span><span class="value" data-vel>${hit ? hit.velocity : "—"}</span></div>
			<div class="cell lknob" id="${pid(sel, "level")}" data-level tabindex="0" aria-label="Niveau kit" role="slider" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${t.level}">${knobSvg(48, t.level)}</div>
			<div class="cell stack lvalue"><span class="caption">Niveau kit</span><span class="value">${t.level}</span></div>
			${toggle("mute", sel, t.mute)}${toggle("solo", sel, t.solo)}</div>`;
	}

	// One row of the LFO's shape chips: its number, the 6 shapes (the parameter's id on the group), then _end.
	const shapeRow = (row, current, end) => `<div class="cell chip lbl${row === 2 ? " r2" : ""}">${row}</div>
		<div id="${pid(sel, `lfoShape${row}`)}" role="radiogroup" aria-label="Forme ${row} du LFO" style="display:contents">${shapes.map((s, i) =>
			`<div class="cell chip${row === 2 ? " r2" : ""}${i === current ? " on" : ""}" data-shape${row}="${i}" tabindex="0" role="radio" aria-checked="${i === current}">${s}</div>`).join("")}</div>${end}`;

	function pages() {
		const t = tracks[sel], n = names(sel), m = machineOf(sel);
		const lfoTarget = t.lfo.track === sel ? t.lfo.param : -1;
		const knobs = from => [0,1,2,3,4,5,6,7].map(k => {
			const i = from + k, unused = (i < 8 && !n[i]) || sendsPending.has(i);
			const why = i < 8 && !n[i] ? `${m.name} n'utilise pas ce paramètre` : sendsPending.has(i) ? "Effets maîtres à venir" : "";
			const label = n[i] || "—", cls = unused ? " unused" : "";
			return `<div class="cell knob k${k + 1}${cls}" id="${pid(sel, specNames[i])}" data-i="${unused ? "" : i}"${unused ? ` title="${why}"` : ` tabindex="0" role="slider" aria-label="${label}" aria-valuemin="0" aria-valuemax="127" aria-valuenow="${t.p[i]}"`}>${knobSvg(48, t.p[i])}</div>
				<div class="cell klabel k${k + 1}${i === lfoTarget ? " lfo" : ""}${cls}"${why ? ` title="${why}"` : ""}>${label}</div>
				<div class="cell kvalue value k${k + 1}${cls}">${unused && i < 8 ? "" : t.p[i]}</div>`;
		}).join("");
		const page = (cls, title, note, from, screen) => `<div class="pg ${cls}"><div class="cell title"><b>${title}</b><span>${note}</span></div>${knobs(from)}${screen}</div>`;
		const dest = tracks[t.lfo.track], destName = names(t.lfo.track)[t.lfo.param] || efxNames[0];
		return page("syn", "SYN", `paramètres de ${m.name}`, 0,
				`<div class="screen" data-screen="hit"><div class="stitle"><span class="cell caption">Frappe</span><span class="cell note">dernière note</span></div><canvas></canvas></div>`)
			+ page("efx", "EFX", "effets de la piste", 8,
				`<div class="screen" data-screen="filter"><div class="stitle"><span class="cell caption">Filtre · EQ · AM</span><span class="cell note num">20 Hz – 20 kHz</span></div><canvas></canvas></div>`)
			+ page("rte", "ROUTING", "volume, panoramique, envois et LFO", 16,
				`<div class="screen lfo" data-screen="lfo"><div class="stitle"><span class="cell caption">LFO →</span>
					<span class="cell dest" id="${pid(sel, "lfoTrack")}" data-lfotrack tabindex="0" title="Piste modulée">T${two(t.lfo.track + 1)}</span>
					<span class="cell dest" id="${pid(sel, "lfoParam")}" data-lfoparam tabindex="0" title="Paramètre modulé">${destName}</span>
					<span class="cell note" style="grid-column: 7 / 10">LFOM mêle 1 et 2</span></div><canvas></canvas>
					<div class="chips">${shapeRow(1, t.lfo.shape1, `<div class="cell chip wide" id="${pid(sel, "lfoMode")}" data-modecycle tabindex="0" title="Mode : FREE, TRIG ou HOLD">${modes[t.lfo.mode]}</div>`)}
						${shapeRow(2, t.lfo.shape2, `<div class="cell chip wide lbl num">LFOM ${t.p[23]}</div>`)}</div></div>`);
	}

	function browser() {
		const cur = tracks[sel].machine, m = machineOf(sel);
		const span = { GND: [1, 4], TRX: [4, 8], EFM: [8, 12], E12: [12, 16], "P-I": [16, 20], ROM: [20, 26] };
		const fams = familyOrder.map(f => {
			const list = MD_MACHINES.filter(x => x.family === f), [a, b] = span[f];
			const items = list.map(x => `<div class="cell mach${x.id === cur ? " cur" : ""}" data-mach="${x.id}" tabindex="0" title="${x.name}">${model(x)}</div>`).join("");
			return `<div class="cell fam" style="grid-column:${a}/${b}"><b>${f}</b><span class="num">${list.length}</span></div>
				<div class="col${f === "ROM" ? " rom" : f === "E12" ? " e12" : ""}" style="grid-column:${a}/${b}">${items}</div>`;
		}).join("");
		return `<div class="browser"><div class="cell bhead"><b>Choisir une machine</b><span>piste ${two(sel + 1)}</span><span data-bhint>${m.name} · ${m.syn.filter(Boolean).join(" ")}</span></div>
			<div class="cell close" data-close tabindex="0">Fermer</div>${fams}</div>`;
	}

	function trackView() {
		const note = lastNote ? `${lastNote.note} · vél. ${lastNote.velocity}` : "—";
		return `${trackList()}<div class="detail">${head()}${browserOpen ? browser() : pages()}
			<div class="cell dfoot"><span>Tirer : régler · <b>Maj</b> : réglage fin · <b>Double-clic</b> : valeur par défaut · <b>Clic sur un numéro</b> : jouer</span>
			<span class="last">Dernière note <b class="num" data-last>${note}</b></span></div></div>`;
	}

	function mixView() {
		const strips = tracks.map((tr, i) => `<div class="strip${i === sel ? " sel" : ""}" data-t="${i}">
			<div class="cell n" data-playt="${i}" tabindex="0" aria-label="Jouer la piste ${two(i + 1)}">${two(i + 1)}</div>
			<div class="cell m">${byId.get(tr.machine).name}</div>
			<div class="cell pan" id="${pid(i, "PAN")}" data-mix="18" data-track="${i}" tabindex="0" role="slider" aria-label="PAN, piste ${two(i + 1)}">${knobSvg(48, tr.p[18])}<span>PAN</span></div>
			<div class="cell fz"><div class="vu"><b data-vu="${i}"></b></div><div class="fader" id="${pid(i, "level")}" data-fader="${i}" tabindex="0" role="slider" aria-label="Niveau kit, piste ${two(i + 1)}"><div class="rail"></div>
				<div class="capf" style="bottom:calc(${tr.level / 127 * 100}% - 5px)"></div></div></div>
			<div class="cell val value">${tr.level}</div>${toggle("mute", i, tr.mute, true)}${toggle("solo", i, tr.solo, true)}
			<div class="cell dest${separated.has(i) ? " own" : ""}">${separated.has(i) ? "→ " + two(i + 1) : "Main"}</div></div>`).join("");
		return `<div class="mix">${strips}</div>`;
	}

	function outputsView() {
		const rows = tracks.map((tr, i) => {
			const own = separated.has(i);
			return `<div class="orow${own ? " own" : ""}"><div class="cell num">${two(i + 1)}</div><div class="cell">${byId.get(tr.machine).name}</div><div class="cell out">${two(i + 1)}</div>
				<div class="cell state">${own ? "Prise par l'hôte, hors du mix" : "Libre, dans Main"}</div><div class="cell lvl"><i><b data-ovu="${i}"></b></i></div></div>`;
		}).join("");
		return `<div class="otable">
			<div class="orow ohead"><div class="cell caption">Piste</div><div class="cell caption">Machine</div><div class="cell caption">Sortie</div><div class="cell caption">État</div><div class="cell caption">Niveau</div></div>
			<div class="orow main"><div class="cell">—</div><div class="cell">Stéréo</div><div class="cell out">Main</div><div class="cell state">Les pistes libres</div><div class="cell lvl"><i><b data-ovu="main"></b></i></div></div>
			${rows}</div>
			<div class="ohelp">
			<div class="cell h h1 caption">Sorties séparées</div>
			<p class="t1">Chaque piste a sa sortie mono, du même numéro : piste 01, sortie 01. Dans l'hôte, elles s'appellent <b>Out 01</b> à <b>Out 16</b> ; la sortie stéréo principale, <b>Main</b>. C'est l'hôte qui prend une sortie : dès qu'une piste y est branchée, elle quitte Main.</p>
			<div class="cell h h2 caption">Dans Live</div>
			<p class="t2">Créer une piste audio, puis dans « Audio From » : <b>MD Drums</b>, et dessous <b>Out 01</b> à <b>Out 16</b>. Monitor sur <b>In</b>. La sortie porte la piste après ses effets et son VOL, avant le PAN, comme les sorties individuelles du Machinedrum.</p>
			<div class="cell h h3 caption">Main</div>
			<p class="t3">Stéréo : toutes les pistes libres, sans effets maîtres pour l'instant.</p></div>`;
	}

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
	const moduleGrid = (g, w, h, css) => { g.strokeStyle = css("--row"); g.lineWidth = 1;
		for(let x = 40; x < w; x += 40) { g.beginPath(); g.moveTo(x + 0.5, 0); g.lineTo(x + 0.5, h); g.stroke(); }
		for(let y = 40; y < h; y += 40) { g.beginPath(); g.moveTo(0, y + 0.5); g.lineTo(w, y + 0.5); g.stroke(); } };

	function drawHit(frame) {
		const [g, w, h] = canvasOf(frame, "hit"); if(!g) return;
		const css = cssOf(frame), p = tracks[sel].p, hit = lastHit[sel];
		moduleGrid(g, w, h, css);
		const gain = hit ? hit.velocity / 127 : 0.8;
		const decay = 0.02 + p[1] / 127 * 0.6, freq = 2 + p[0] / 127 * 20, amd = p[8] / 127, amf = 1 + p[9] / 6;
		g.beginPath(); g.strokeStyle = css("--wave"); g.lineWidth = 1;
		for(let x = 0; x <= w; ++x) {
			const s = x / w, env = gain * Math.exp(-s / decay) * (1 - amd * 0.5 * (1 + Math.sin(2 * Math.PI * amf * s)));
			const y = h / 2 - env * (h * 0.4) * Math.sin(2 * Math.PI * freq * s * (1 + (1 - s) * p[2] / 127));
			x ? g.lineTo(x, y) : g.moveTo(x, y);
		}
		g.stroke();
		g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
		for(let x = 0; x <= w; ++x) { const s = x / w, y = h / 2 - gain * Math.exp(-s / decay) * h * 0.4; x ? g.lineTo(x, y) : g.moveTo(x, y); }
		g.stroke();
	}

	function drawScreens(frame) {
		drawHit(frame);
		const css = cssOf(frame), t = tracks[sel], p = t.p;
		{	// Filter (band from FLTF, width FLTW, resonance FLTQ) and EQ peak (EQF, EQG)
			const [g, w, h] = canvasOf(frame, "filter");
			if(g) {
				moduleGrid(g, w, h, css);
				const lo = 20 * Math.pow(1000, p[12] / 127), hi = 20 * Math.pow(1000, Math.min(1, (p[12] + p[13]) / 127)), q = p[14] / 127;
				const eqf = 20 * Math.pow(1000, p[10] / 127), eqg = (p[11] - 64) / 64 * 12;
				g.beginPath(); g.strokeStyle = css("--accent"); g.lineWidth = 2;
				for(let x = 0; x <= w; ++x) {
					const f = 20 * Math.pow(1000, x / w);
					const hp = 1 / Math.sqrt(1 + Math.pow(lo / f, 4)), lp = 1 / Math.sqrt(1 + Math.pow(f / hi, 4));
					const res = 1 + q * 3 * (Math.exp(-Math.pow(Math.log(f / lo), 2) * 20) + Math.exp(-Math.pow(Math.log(f / hi), 2) * 20));
					const eq = Math.pow(10, eqg * Math.exp(-Math.pow(Math.log(f / eqf), 2) * 2) / 20);
					const y = h * 0.5 - 20 * Math.log10(Math.max(1e-4, hp * lp * res * eq)) / 24 * h * 0.42;
					x ? g.lineTo(x, y) : g.moveTo(x, y);
				}
				g.stroke();
				g.fillStyle = css("--ink"); g.fillRect(Math.log(eqf / 20) / Math.log(1000) * w - 3, h * 0.5 - eqg / 24 * h * 0.42 - 3, 6, 6);
				if(p[15] > 0) { g.fillStyle = css("--ink-dim"); g.font = `12px ${css("--font")}`; g.fillText(`SRR ${p[15]}`, w - 60, h - 10); }
			}
		}
		{	// LFO: periods at LFOS, scaled by LFOD, shape 1 mixed into shape 2 by LFOM
			const [g, w, h] = canvasOf(frame, "lfo");
			if(g) {
				moduleGrid(g, w, h, css);
				const depth = p[22] / 127, periods = 1 + p[21] / 127 * 5, mix = p[23] / 127;
				let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
				const steps = []; for(let i = 0; i < 64; ++i) steps.push(rnd() * 2 - 1);
				const shape = (s, ph) => { const f = ph - Math.floor(ph);
					return [1 - 4 * Math.abs(f - 0.5), 1 - 2 * f, f < 0.5 ? 1 : -1, 2 * f - 1, Math.exp(-4 * f) * 2 - 1, steps[Math.floor(ph * 2) % 64]][s]; };
				const wave = ph => (1 - mix) * shape(t.lfo.shape1, ph) + mix * shape(t.lfo.shape2, ph);
				g.beginPath(); g.strokeStyle = depth > 0 ? css("--accent") : css("--ink-faint"); g.lineWidth = 2;
				for(let x = 0; x <= w; ++x) { const y = h / 2 - wave(x / w * periods) * Math.max(depth, 0.04) * h * 0.4; x ? g.lineTo(x, y) : g.moveTo(x, y); }
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
		const body = view === "MIX" ? mixView() : view === "SORTIES" ? outputsView() : trackView();
		frame.innerHTML = `<div class="ui${showGrid ? " showgrid" : ""}" style="--font:${frame.dataset.font}">${top(view)}${body}</div>`;
		if(view === "PISTE" && !browserOpen) drawScreens(frame);
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
			if(t === sel) { const vel = frame.querySelector("[data-vel]"); if(vel) vel.textContent = velocity; drawHit(frame); }
			if(!reduceMotion) frame.querySelectorAll(`[data-playt="${t}"]`).forEach(n => { n.classList.add("trig"); setTimeout(() => n.classList.remove("trig"), 140); });
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
				for(let x = 0; x < w; ++x) { const a = h[Math.floor(x / w * h.length)], y = a * (ht / 2 - 4); if(y < 0.5) continue; g.moveTo(x + 0.5, ht / 2 - y); g.lineTo(x + 0.5, ht / 2 + y); }
				g.stroke();
				const newest = h[h.length - 1];
				if(newest > 0.02) { g.fillStyle = css("--accent"); const y = newest * (ht / 2 - 4); g.fillRect(w - 2, ht / 2 - y, 2, y * 2); }
			});
			frame.querySelectorAll("[data-vu]").forEach(b => b.style.height = Math.min(100, amps[+b.dataset.vu] * 100) + "%");
			frame.querySelectorAll("[data-ovu]").forEach(b => {
				const v = b.dataset.ovu === "main" ? Math.min(1, amps.reduce((s, a, t) => s + (separated.has(t) ? 0 : a), 0) * 0.6) : amps[+b.dataset.ovu];
				b.style.width = Math.min(100, v * 100) + "%"; });
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
	const machineList = MD_MACHINES.map(m => m.id);
	let drag = null;

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
		if(at("[data-playt]")) { const t = +at("[data-playt]").dataset.playt; if(t !== sel) { sel = t; renderAll(); } trigger(t, 100); return true; }
		if(at("[data-pick]")) { sel = +at("[data-pick]").dataset.pick; renderAll(); return true; }
		if(at("[data-step]")) { const i = machineList.indexOf(tracks[sel].machine), n = machineList.length;
			tracks[sel].machine = machineList[(i + +at("[data-step]").dataset.step + n) % n]; renderAll(); return true; }
		if(at("[data-browse]")) { browserOpen = !browserOpen; renderAll(); return true; }
		if(at("[data-close]")) { browserOpen = false; renderAll(); return true; }
		if(at("[data-mach]")) { tracks[sel].machine = +at("[data-mach]").dataset.mach; browserOpen = false; renderAll(); return true; }
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
	function onKey(e) {
		const el = document.activeElement; if(!el || !el.closest || !el.closest(".frame")) return;
		const c = control(el);
		if(c && (e.key === "ArrowUp" || e.key === "ArrowDown")) { c.set(clamp(c.get() + (e.key === "ArrowUp" ? 1 : -1) * (e.shiftKey ? 10 : 1))); renderAll(); e.preventDefault(); return; }
		if(e.key === "Enter" || e.key === " ") { if(act(el, e)) e.preventDefault(); return; }
		if(e.key === "Escape" && browserOpen) { browserOpen = false; renderAll(); }
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

	return { start, renderAll, setGrid: on => { showGrid = on; renderAll(); }, setDemo, onScaleChange: f => onScale = f, trigger };
})();
