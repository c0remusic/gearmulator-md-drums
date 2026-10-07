// MD Drums v5 mockup: the v4 state and behaviour, drawn on one 40 px module grid (v5-ui.css). Every element with class
// "frame" gets a UI (data-view: PISTE, MIX or SORTIES; data-font: the font family).
"use strict";

const MD = (() => {
	const machines = ["TRX-BD","TRX-SD","TRX-XT","TRX-CP","TRX-RS","TRX-CB","TRX-CH","TRX-OH","TRX-CY","TRX-MA","TRX-CL","TRX-XC","TRX-B2","TRX-S2","EFM-BD","EFM-SD"];
	const families = { TRX: "Analogique modélisée", EFM: "FM électronique" };
	const synNames = {
		"TRX-BD": ["PTCH","DEC","RAMP","RDEC","STRT","NOIS","HARM","CLIP"],
		"TRX-SD": ["PTCH","DEC","BUMP","BENV","SNAP","TONE","TUNE","CLIP"],
		"EFM-BD": ["PTCH","DEC","RAMP","RDEC","MOD","MFRQ","MDEC","MFB"] };
	const genericSyn = ["SYN1","SYN2","SYN3","SYN4","SYN5","SYN6","SYN7","SYN8"];
	const efxNames = ["AMD","AMF","EQF","EQG","FLTF","FLTW","FLTQ","SRR"];
	const routeNames = ["DIST","VOL","PAN","DEL","REV","LFOS","LFOD","LFOM"];
	const shapes = ["TRI","SAW","SQR","RMP","EXP","RND"];
	const modes = ["FREE","TRIG","HOLD"];
	const defaults = [64,64,64,64,64,64,64,64, 0,0,64,64,0,127,0,0, 0,100,64,0,0,64,0,0];
	const two = n => String(n).padStart(2, "0");

	const tracks = machines.map((m, t) => ({ machine: m, level: [100,96,88,90,70,64,82,76,60,58,66,72,100,92,84,80][t], mute: t === 12,
		p: defaults.map((d, i) => i < 8 ? (d + t * 9 + i * 13) % 128 : d), lfo: { track: t, param: 12, shape1: 0, mode: 0 } }));
	tracks[0].p = [52,78,96,40,64,12,30,8, 0,0,64,64,18,70,40,0, 0,100,64,0,0,64,38,0];
	let sel = 0, showGrid = false, lastNote = null;
	// As if the host had taken outputs 01 and 02 (tracks 1 and 2).
	const separated = new Set([0, 1]);

	// A flat knob: a 270 degree track, the value as a white arc, a pointer. No body, no shading.
	function knobSvg(size, value) {
		const r = size / 2 - 3, c = size / 2, a0 = 0.75 * Math.PI, sweep = 1.5 * Math.PI, a = a0 + sweep * value / 127;
		const p = (t, rr) => [c + rr * Math.cos(t), c + rr * Math.sin(t)];
		const arc = (from, to) => { const [x0, y0] = p(from, r), [x1, y1] = p(to, r); return `M${x0} ${y0} A${r} ${r} 0 ${to - from > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
		const [x0, y0] = p(a, r * 0.35), [x1, y1] = p(a, r - 4);
		return `<svg width="${size}" height="${size}"><path d="${arc(a0, a0 + sweep)}" stroke="var(--track)" stroke-width="2" fill="none"/>
			${value > 0 ? `<path d="${arc(a0, a)}" stroke="var(--ink)" stroke-width="2.5" fill="none"/>` : ""}
			<line x1="${x0}" y1="${y0}" x2="${x1}" y2="${y1}" stroke="var(--ink)" stroke-width="2"/></svg>`;
	}

	const names = t => [...(synNames[t.machine] || genericSyn), ...efxNames, ...routeNames];
	const meter = level => `<i><b style="width:${Math.min(100, level)}%"></b></i>`;
	const mute = (i, on, row) => `<div class="cell mute${on ? " on" : ""}" data-mute="${i}"${row ? ` style="grid-row:${row}"` : ""}><i>M</i></div>`;

	function top(view) {
		const t = tracks[sel];
		const tabs = ["PISTE", "MIX", "SORTIES"].map(n => `<div class="cell tab rr${n === view ? " on" : ""}" style="grid-column: span 2" data-view="${n}">${n}</div>`).join("");
		return `<div class="top"><div class="cell logo rr">MD<span>DRUMS</span></div><div class="cell status rr num">KIT 01 · T${two(sel + 1)} · ${t.machine}</div>
			${tabs}<div class="cell rate rr num" style="border-left:1px solid var(--rule)">44,1 kHz · 0,5 ms</div>
			<div class="cell out"><span class="cap">Main</span><div class="bars" data-meter>${meter(0)}${meter(0)}</div></div></div>`;
	}

	function trackView() {
		const t = tracks[sel], n = names(t);
		const lfoTarget = t.lfo.track === sel ? t.lfo.param : -1;
		const rows = tracks.map((tr, i) => `<div class="trow${i === sel ? " sel" : ""}" data-t="${i}"><div class="cell n num">${two(i + 1)}</div><div class="cell m">${tr.machine}</div>
			<div class="cell lv"><i><b style="width:${tr.level / 1.27}%"></b></i></div>${mute(i, tr.mute)}</div>`).join("");
		const page = (cls, title, from, screen) => `<div class="pg ${cls}"><div class="cell title"><b>${title}</b><span>${title === "SYN" ? t.machine : ""}</span></div>
			${[0,1,2,3,4,5,6,7].map(k => `<div class="cell knob k${k + 1}" data-i="${from + k}">${knobSvg(48, t.p[from + k])}</div>
				<div class="cell klabel k${k + 1}${from + k === lfoTarget ? " lfo" : ""}">${n[from + k]}</div>
				<div class="cell kvalue k${k + 1} num">${t.p[from + k]}</div>`).join("")}${screen}</div>`;
		const own = separated.has(sel);
		const destNames = names(tracks[t.lfo.track]);
		return `<div class="tracks"><div class="cell head cap">Pistes · kit 01</div>${rows}<div class="cell foot">16 pistes · ${separated.size} sorties prises par l'hôte</div></div>
			<div class="detail">
			<div class="dhead"><div class="cell big num">${two(sel + 1)}</div><div class="cell machine">${t.machine}</div>
				<div class="cell family">${families[t.machine.slice(0, 3)] || ""}</div>
				<div class="cell route"><span class="cap">Sortie</span><b class="${own ? "own" : ""}">${own ? "→ " + two(sel + 1) + " · hors du mix" : "MAIN"}</b></div>
				<div class="cell lvl" data-level="1" style="flex-direction:column;justify-content:center;cursor:ns-resize">${knobSvg(40, t.level)}<span class="cap" style="letter-spacing:1px">Level ${t.level}</span></div>
				${mute(sel, t.mute)}</div>
			${page("syn", "SYN", 0, `<div class="screen" data-screen="hit"><div class="stitle"><span class="cap">Frappe</span><span class="note">dernière note · cliquer pour jouer</span></div><canvas></canvas></div>`)}
			${page("efx", "EFX", 8, `<div class="screen" data-screen="filter"><div class="stitle"><span class="cap">Filtre · EQ · AM</span><span class="note num">20 Hz – 20 kHz</span></div><canvas></canvas></div>`)}
			${page("rte", "ROUTING", 16, `<div class="screen lfo" data-screen="lfo"><div class="stitle"><span class="cap">LFO → T${two(t.lfo.track + 1)} ${destNames[t.lfo.param]}</span><span class="note">${modes[t.lfo.mode]}</span></div><canvas></canvas>
				<div class="chips">${shapes.map((s, i) => `<div class="cell chip${i === t.lfo.shape1 ? " on" : ""}" data-shape="${i}">${s}</div>`).join("")}${modes.map((s, i) => `<div class="cell chip${i === t.lfo.mode ? " on" : ""}" data-mode="${i}">${s}</div>`).join("")}<div class="cell chip gap"></div></div></div>`)}
			<div class="cell dfoot"><span>Notes <b>36–51</b> → pistes <b>01–16</b></span><span>Dernière note <b class="num">${lastNote ? `${lastNote.note} · vél. ${lastNote.velocity}` : "—"}</b></span></div>
			</div>`;
	}

	function mixView() {
		const strips = tracks.map((tr, i) => {
			const own = separated.has(i);
			return `<div class="strip${i === sel ? " sel" : ""}" data-t="${i}">
				<div class="cell n num" style="grid-row:1">${two(i + 1)}</div><div class="cell m" style="grid-row:2">${tr.machine}</div>
				<div class="cell pan" data-mix="18" data-track="${i}" style="cursor:ns-resize">${knobSvg(36, tr.p[18])}<span>PAN</span></div>
				<div class="cell send" style="grid-row:5" data-mix="19" data-track="${i}" title="Effets maîtres à venir">${knobSvg(24, tr.p[19])}<span>DEL</span></div>
				<div class="cell send" style="grid-row:6" data-mix="20" data-track="${i}" title="Effets maîtres à venir">${knobSvg(24, tr.p[20])}<span>REV</span></div>
				<div class="cell fz"><div class="vu"><b style="height:${tr.mute ? 0 : tr.level * 0.6}%"></b></div><div class="fader" data-fader="${i}"><div class="rail"></div>
					<div class="capf" style="bottom:calc(${tr.level / 127 * 100}% - 5px)"></div></div></div>
				<div class="cell val num">${tr.level}</div>${mute(i, tr.mute, 17)}
				<div class="cell dest${own ? " own" : ""}">${own ? "→ " + two(i + 1) : "MAIN"}</div></div>`;
		}).join("");
		return `<div class="mix">${strips}</div>`;
	}

	function outputsView() {
		const rows = tracks.map((tr, i) => {
			const own = separated.has(i);
			return `<div class="orow${own ? " own" : ""}"><div class="cell num">${two(i + 1)}</div><div class="cell">${tr.machine}</div><div class="cell out num">${two(i + 1)}</div>
				<div class="cell state">${own ? "Prise par l'hôte · hors du mix" : "Libre · dans MAIN"}</div><div class="cell lvl">${meter(tr.mute ? 0 : tr.level * 0.6)}</div></div>`;
		}).join("");
		return `<div class="otable">
			<div class="orow ohead"><div class="cell cap">Piste</div><div class="cell cap">Machine</div><div class="cell cap">Sortie</div><div class="cell cap">État</div><div class="cell cap">Niveau</div></div>
			<div class="orow main"><div class="cell">—</div><div class="cell">Stéréo</div><div class="cell out">MAIN</div><div class="cell state">Les pistes libres</div><div class="cell lvl">${meter(60)}</div></div>
			${rows}</div>
			<div class="ohelp">
			<div class="cell h h1 cap">Sorties séparées</div>
			<p class="t1">Chaque piste a sa sortie mono, du même numéro : piste 01, sortie 01. Dans l'hôte, elles s'appellent <b>Out 01</b> à <b>Out 16</b> ; la sortie stéréo principale, <b>Main</b>. C'est l'hôte qui prend une sortie : dès qu'une piste y est branchée, elle quitte MAIN.</p>
			<div class="cell h h2 cap">Dans Live</div>
			<p class="t2">Créer une piste audio, puis dans « Audio From » : <b>MD Drums</b>, et dessous <b>Out 01</b> à <b>Out 16</b>. Monitor sur <b>In</b>. La sortie porte la piste après ses effets et son VOL, avant le PAN, comme les sorties individuelles du Machinedrum.</p>
			<div class="cell h h3 cap">Main</div>
			<p class="t3">Stéréo : toutes les pistes libres, sans effets maîtres pour l'instant.</p></div>`;
	}

	function canvasOf(frame, name) {
		const c = frame.querySelector(`[data-screen="${name}"] canvas`);
		const w = c.clientWidth, h = c.clientHeight;
		c.width = w * 2; c.height = h * 2;
		const g = c.getContext("2d"); g.scale(2, 2);
		return [g, w, h];
	}

	function drawScreens(frame) {
		const ui = frame.querySelector(".ui"), css = n => getComputedStyle(ui).getPropertyValue(n).trim();
		// The screens' own grid falls on the module grid: lines every 40 px.
		const moduleGrid = (g, w, h) => { g.strokeStyle = css("--grid"); g.lineWidth = 1;
			for(let x = 40; x < w; x += 40) { g.beginPath(); g.moveTo(x + 0.5, 0); g.lineTo(x + 0.5, h); g.stroke(); }
			for(let y = 40; y < h; y += 40) { g.beginPath(); g.moveTo(0, y + 0.5); g.lineTo(w, y + 0.5); g.stroke(); } };
		const t = tracks[sel], p = t.p;
		{	// Hit: a decaying tone shaped by DEC (SYN2), pitch (SYN1) and the amplitude modulation
			const [g, w, h] = canvasOf(frame, "hit");
			moduleGrid(g, w, h);
			const decay = 0.02 + p[1] / 127 * 0.6, freq = 2 + p[0] / 127 * 20, amd = p[8] / 127, amf = 1 + p[9] / 6;
			g.beginPath(); g.strokeStyle = css("--curve2"); g.lineWidth = 1.2;
			for(let x = 0; x <= w; ++x) {
				const s = x / w, env = Math.exp(-s / decay) * (1 - amd * 0.5 * (1 + Math.sin(2 * Math.PI * amf * s)));
				const y = h / 2 - env * (h * 0.4) * Math.sin(2 * Math.PI * freq * s * (1 + (1 - s) * p[2] / 127));
				x ? g.lineTo(x, y) : g.moveTo(x, y);
			}
			g.stroke();
			g.beginPath(); g.strokeStyle = css("--curve"); g.lineWidth = 1.5;
			for(let x = 0; x <= w; ++x) { const s = x / w, y = h / 2 - Math.exp(-s / decay) * h * 0.4; x ? g.lineTo(x, y) : g.moveTo(x, y); }
			g.stroke();
		}
		{	// Filter (band from FLTF, width FLTW, resonance FLTQ) and EQ peak (EQF, EQG)
			const [g, w, h] = canvasOf(frame, "filter");
			moduleGrid(g, w, h);
			const lo = 20 * Math.pow(1000, p[12] / 127), hi = 20 * Math.pow(1000, Math.min(1, (p[12] + p[13]) / 127)), q = p[14] / 127;
			const eqf = 20 * Math.pow(1000, p[10] / 127), eqg = (p[11] - 64) / 64 * 12;
			g.beginPath(); g.strokeStyle = css("--curve"); g.lineWidth = 2;
			for(let x = 0; x <= w; ++x) {
				const f = 20 * Math.pow(1000, x / w);
				const hp = 1 / Math.sqrt(1 + Math.pow(lo / f, 4)), lp = 1 / Math.sqrt(1 + Math.pow(f / hi, 4));
				const res = 1 + q * 3 * (Math.exp(-Math.pow(Math.log(f / lo), 2) * 20) + Math.exp(-Math.pow(Math.log(f / hi), 2) * 20));
				const eq = Math.pow(10, eqg * Math.exp(-Math.pow(Math.log(f / eqf), 2) * 2) / 20);
				const y = h * 0.5 - 20 * Math.log10(Math.max(1e-4, hp * lp * res * eq)) / 24 * h * 0.42;
				x ? g.lineTo(x, y) : g.moveTo(x, y);
			}
			g.stroke();
			g.fillStyle = css("--curve2"); g.fillRect(Math.log(eqf / 20) / Math.log(1000) * w - 3, h * 0.5 - eqg / 24 * h * 0.42 - 3, 6, 6);
			if(p[15] > 0) { g.fillStyle = css("--warn"); g.font = `11px ${css("--font")}`; g.fillText(`SRR ${p[15]}`, w - 56, h - 10); }
		}
		{	// LFO: two periods of the shape at LFOS, scaled by LFOD
			const [g, w, h] = canvasOf(frame, "lfo");
			moduleGrid(g, w, h);
			const depth = p[22] / 127, periods = 1 + p[21] / 127 * 5, shape = t.lfo.shape1;
			let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
			const steps = []; for(let i = 0; i < 64; ++i) steps.push(rnd() * 2 - 1);
			const wave = ph => { const f = ph - Math.floor(ph);
				return [1 - 4 * Math.abs(f - 0.5), 1 - 2 * f, f < 0.5 ? 1 : -1, 2 * f - 1, Math.exp(-4 * f) * 2 - 1, steps[Math.floor(ph * 2) % 64]][shape]; };
			g.beginPath(); g.strokeStyle = depth > 0 ? css("--curve") : css("--ink-faint"); g.lineWidth = 2;
			for(let x = 0; x <= w; ++x) { const y = h / 2 - wave(x / w * periods) * Math.max(depth, 0.04) * h * 0.4; x ? g.lineTo(x, y) : g.moveTo(x, y); }
			g.stroke();
		}
	}

	function renderFrame(frame) {
		const view = frame.dataset.view || "PISTE";
		const body = view === "MIX" ? mixView() : view === "SORTIES" ? outputsView() : trackView();
		frame.innerHTML = `<div class="ui${showGrid ? " showgrid" : ""}" style="--font:${frame.dataset.font}">${top(view)}${body}</div>`;
		if(view === "PISTE") drawScreens(frame);
	}

	const renderAll = () => document.querySelectorAll(".frame").forEach(renderFrame);

	function play(frame, track) {
		lastNote = { note: 36 + track, velocity: 100 };
		renderAll();
		const row = frame.querySelector(`.trow[data-t="${track}"]`);
		if(row) { row.classList.add("hit"); setTimeout(() => row.classList.remove("hit"), 160); }
		const bars = frame.querySelectorAll("[data-meter] b");
		bars.forEach(b => b.style.width = (tracks[track].level / 1.27 * 0.7) + "%");
		setTimeout(() => bars.forEach(b => b.style.width = "0"), 200);
	}

	let drag = null;
	function onDown(e) {
		const frame = e.target.closest(".frame"); if(!frame) return;
		const at = s => e.target.closest(s);
		const tab = at(".tab[data-view]"), trow = at(".trow"), muteCell = at("[data-mute]"), chip = at("[data-shape],[data-mode]"),
			fader = at("[data-fader]"), mix = at("[data-mix]"), level = at("[data-level]"), knob = at("[data-i]"), strip = at(".strip[data-t]");
		if(tab) { frame.dataset.view = tab.dataset.view; renderFrame(frame); return; }
		if(muteCell) { const i = +muteCell.dataset.mute; tracks[i].mute = !tracks[i].mute; renderAll(); return; }
		if(chip) { const t = tracks[sel]; if(chip.dataset.shape) t.lfo.shape1 = +chip.dataset.shape; else t.lfo.mode = +chip.dataset.mode; renderAll(); return; }
		if(at('[data-screen="hit"]')) { play(frame, sel); return; }
		if(trow) { sel = +trow.dataset.t; play(frame, sel); return; }
		// A drag: vertical, 1.5 px per step for knobs, the fader's own height for faders.
		if(fader) { const tr = tracks[+fader.dataset.fader]; drag = { y: e.clientY, start: tr.level, step: fader.getBoundingClientRect().height / 127, set: v => tr.level = v }; }
		else if(mix) { const tr = tracks[+mix.dataset.track], i = +mix.dataset.mix; drag = { y: e.clientY, start: tr.p[i], step: 1.5, set: v => tr.p[i] = v }; }
		else if(level) drag = { y: e.clientY, start: tracks[sel].level, step: 1.5, set: v => tracks[sel].level = v };
		else if(knob) { const i = +knob.dataset.i; drag = { y: e.clientY, start: tracks[sel].p[i], step: 1.5, set: v => tracks[sel].p[i] = v }; }
		if(strip) sel = +strip.dataset.t;
		if(drag) e.preventDefault();
		if(strip && !drag) renderAll();
	}
	function onMove(e) {
		if(!drag) return;
		drag.set(Math.max(0, Math.min(127, Math.round(drag.start + (drag.y - e.clientY) / drag.step))));
		renderAll();
	}
	function onDouble(e) {
		const k = e.target.closest("[data-i]"); if(!k) return;
		tracks[sel].p[+k.dataset.i] = defaults[+k.dataset.i]; renderAll();
	}

	function start() {
		document.addEventListener("pointerdown", onDown);
		document.addEventListener("pointermove", onMove);
		document.addEventListener("pointerup", () => drag = null);
		document.addEventListener("dblclick", onDouble);
		renderAll();
		if(document.fonts) document.fonts.ready.then(renderAll);
	}

	const setGrid = on => { showGrid = on; renderAll(); };
	return { start, renderAll, setGrid };
})();
