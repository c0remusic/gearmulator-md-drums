// MD Drums v4 mockup: one shared state, drawn into every element with class "frame" (data-view: PISTE, MIX or
// SORTIES; data-font: the font family). v4-test.html has one frame, v4-var-font.html one per candidate font.
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
	let sel = 0;
	// As if the host had taken outputs 01 and 02 (tracks 1 and 2).
	const separated = new Set([0, 1]);

	function knobSvg(size, value) {
		const r = size / 2 - 4, c = size / 2, a0 = 0.75 * Math.PI, sweep = 1.5 * Math.PI, a = a0 + sweep * value / 127;
		const p = t => [c + r * Math.cos(t), c + r * Math.sin(t)];
		const arc = (from, to) => { const [x0, y0] = p(from), [x1, y1] = p(to); return `M${x0} ${y0} A${r} ${r} 0 ${to - from > Math.PI ? 1 : 0} 1 ${x1} ${y1}`; };
		const [hx, hy] = [c + (r - 6) * Math.cos(a), c + (r - 6) * Math.sin(a)];
		return `<svg width="${size}" height="${size}"><path d="${arc(a0, a0 + sweep)}" stroke="var(--track)" stroke-width="3.5" fill="none" stroke-linecap="round"/>
			${value > 0 ? `<path d="${arc(a0, a)}" stroke="var(--arc)" stroke-width="3.5" fill="none" stroke-linecap="round"/>` : ""}
			<circle cx="${c}" cy="${c}" r="${r - 7}" fill="var(--cell)"/>
			<line x1="${c}" y1="${c}" x2="${hx}" y2="${hy}" stroke="var(--ink)" stroke-width="2" stroke-linecap="round"/></svg>`;
	}

	const names = t => [...(synNames[t.machine] || genericSyn), ...efxNames, ...routeNames];

	function top(view) {
		const t = tracks[sel];
		const tabs = ["PISTE", "MIX", "SORTIES"].map(n => `<div class="tab${n === view ? " on" : ""}" data-view="${n}">${n}</div>`).join("");
		return `<div class="top"><div class="logo">MD <span>DRUMS</span></div><div class="lcd">KIT 01 · T${two(sel + 1)} · ${t.machine}</div>
			${tabs}<div class="grow"></div><span class="status">44,1 kHz · 0,5 ms</span><div class="meter"><i></i></div></div>`;
	}

	function trackView() {
		const t = tracks[sel], n = names(t);
		const lfoTarget = t.lfo.track === sel ? t.lfo.param : -1;
		const knob = i => `<div class="knob${i === lfoTarget ? " lfoTarget" : ""}" data-i="${i}">${knobSvg(46, t.p[i])}<span class="l">${n[i]}</span><span class="v">${t.p[i]}</span></div>`;
		const page = (title, from, side) => `<div class="page"><div><h4>${title}</h4><div class="knobs">${[0,1,2,3,4,5,6,7].map(k => knob(from + k)).join("")}</div></div>${side}</div>`;
		const rows = tracks.map((tr, i) => `<div class="trow${i === sel ? " sel" : ""}" data-t="${i}"><span class="n">${two(i + 1)}</span><span class="m">${tr.machine}</span>
			<div class="bar"><i style="width:${tr.level / 1.27}%"></i></div><div class="mute${tr.mute ? " on" : ""}" data-mute="${i}">M</div></div>`).join("");
		const destNames = names(tracks[t.lfo.track]);
		return `<div class="body"><div class="tracklist">${rows}</div><div class="detail">
			<div class="dhead"><span class="big">${two(sel + 1)}</span><div class="select">${t.machine}</div><span class="family">${families[t.machine.slice(0, 3)] || ""}</span>
				<div class="grow"></div><div class="knob" data-level="1">${knobSvg(40, t.level)}<span class="l">LEVEL</span></div><div class="mute${t.mute ? " on" : ""}" data-mute="${sel}">M</div></div>
			${page("SYN", 0, `<div class="side"><div class="screen" data-screen="hit"><canvas></canvas><span class="cap">FRAPPE</span><span class="note">dernière note · cliquer pour jouer</span></div></div>`)}
			${page("EFX", 8, `<div class="side"><div class="screen" data-screen="filter"><canvas></canvas><span class="cap">FILTRE · EQ · AM</span><span class="note">20 Hz – 20 kHz</span></div></div>`)}
			${page("ROUTING", 16, `<div class="side"><div class="screen" data-screen="lfo"><canvas></canvas><span class="cap">LFO → T${two(t.lfo.track + 1)} ${destNames[t.lfo.param]}</span><span class="note">${modes[t.lfo.mode]}</span>
				<div class="chips">${shapes.map((s, i) => `<span class="chip${i === t.lfo.shape1 ? " on" : ""}" data-shape="${i}">${s}</span>`).join("")}
				${modes.map((s, i) => `<span class="chip${i === t.lfo.mode ? " on" : ""}" data-mode="${i}">${s}</span>`).join("")}</div></div></div>`)}
		</div></div>`;
	}

	function mixView() {
		const strips = tracks.map((tr, i) => {
			const own = separated.has(i);
			return `<div class="strip${i === sel ? " sel" : ""}" data-t="${i}">
				<span class="n">${two(i + 1)}</span><span class="m">${tr.machine}</span>
				<div class="knob" data-mix="18" data-track="${i}">${knobSvg(34, tr.p[18])}<span class="l">PAN</span></div>
				<div class="dimmed" title="Effets maîtres à venir">
					<div class="knob" data-mix="19" data-track="${i}">${knobSvg(28, tr.p[19])}<span class="l">DEL</span></div>
					<div class="knob" data-mix="20" data-track="${i}">${knobSvg(28, tr.p[20])}<span class="l">REV</span></div></div>
				<div class="fwrap"><div class="vu"><i style="height:${tr.mute ? 0 : tr.level * 0.6}%"></i></div><div class="fader" data-fader="${i}"><div class="rail"></div>
					<div class="cap" style="bottom:calc(${tr.level / 127 * 100}% - 7px)"></div></div></div>
				<span class="v">${tr.level}</span>
				<div class="mute${tr.mute ? " on" : ""}" data-mute="${i}">M</div>
				<span class="dest${own ? " own" : ""}">${own ? "→ " + two(i + 1) : "MAIN"}</span></div>`;
		}).join("");
		return `<div class="mix">${strips}<div class="strip master"><span class="n">MAIN</span><span class="m">L · R</span>
			<div class="grow"></div><div class="fwrap"><div class="vu"><i style="height:58%"></i></div><div class="vu"><i style="height:55%"></i></div></div>
			<span class="v">−6,2</span><span class="note">sans effets maîtres</span></div></div>`;
	}

	function outputsView() {
		const level = tr => `<div class="lvl"><i style="width:${tr.mute ? 0 : tr.level * 0.6}%"></i></div>`;
		const rows = tracks.map((tr, i) => {
			const own = separated.has(i);
			return `<div class="orow${own ? " own" : ""}"><span>${two(i + 1)}</span><span>${tr.machine}</span><span class="out">${two(i + 1)}</span>
				<span class="pill${own ? " on" : ""}">${own ? "prise par l'hôte, hors du mix" : "libre, dans MAIN"}</span>${level(tr)}</div>`;
		}).join("");
		return `<div class="outputs"><div class="otable">
			<div class="orow ohead"><span>PISTE</span><span>MACHINE</span><span>SORTIE</span><span>ÉTAT</span><span>NIVEAU</span></div>
			<div class="orow main"><span>—</span><span>stéréo</span><span class="out">MAIN</span><span class="pill on">les pistes libres</span>${level({ level: 100 })}</div>
			${rows}</div>
			<div class="ohelp"><h4>SORTIES SÉPARÉES</h4>
			<p>Chaque piste a sa sortie mono, du même numéro : piste 01, sortie 01. Dans l'hôte, elles s'appellent <b>Out 01</b> à <b>Out 16</b> ; la sortie stéréo principale, <b>Main</b>.</p>
			<p>C'est l'hôte qui prend une sortie : dès qu'une piste y est branchée, elle quitte MAIN.</p>
			<h4>DANS LIVE</h4>
			<p>Créer une piste audio, puis dans « Audio From » : <b>MD Drums</b>, et dessous <b>Out 01</b> à <b>Out 16</b>. Monitor sur <b>In</b>.</p>
			<p>La sortie porte la piste après ses effets et son VOL, avant le PAN, comme les sorties individuelles du Machinedrum.</p>
			<h4>MAIN</h4><p>Stéréo : toutes les pistes libres, sans effets maîtres pour l'instant.</p></div></div>`;
	}

	function canvasOf(frame, name) {
		const box = frame.querySelector(`[data-screen="${name}"]`), c = box.querySelector("canvas");
		c.width = box.clientWidth * 2; c.height = box.clientHeight * 2;
		const g = c.getContext("2d"); g.scale(2, 2);
		return [g, box.clientWidth, box.clientHeight];
	}

	function drawScreens(frame) {
		const ui = frame.querySelector(".ui"), css = n => getComputedStyle(ui).getPropertyValue(n).trim();
		const grid = (g, w, h, xs, ys) => { g.strokeStyle = css("--grid"); g.lineWidth = 1;
			xs.forEach(x => { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, h); g.stroke(); });
			ys.forEach(y => { g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }); };
		const t = tracks[sel], p = t.p;
		{	// Hit: a decaying tone shaped by DEC (SYN2), pitch (SYN1) and the amplitude modulation
			const [g, w, h] = canvasOf(frame, "hit");
			grid(g, w, h, [w * .25, w * .5, w * .75], [h / 2]);
			const decay = 0.02 + p[1] / 127 * 0.6, freq = 2 + p[0] / 127 * 20, amd = p[8] / 127, amf = 1 + p[9] / 6;
			g.beginPath(); g.strokeStyle = css("--curve2"); g.lineWidth = 1.2;
			for(let x = 0; x <= w; ++x) {
				const s = x / w, env = Math.exp(-s / decay) * (1 - amd * 0.5 * (1 + Math.sin(2 * Math.PI * amf * s)));
				const y = h / 2 - env * (h * 0.38) * Math.sin(2 * Math.PI * freq * s * (1 + (1 - s) * p[2] / 127));
				x ? g.lineTo(x, y) : g.moveTo(x, y);
			}
			g.stroke();
			g.beginPath(); g.strokeStyle = css("--curve"); g.lineWidth = 1.5;
			for(let x = 0; x <= w; ++x) { const s = x / w, y = h / 2 - Math.exp(-s / decay) * h * 0.38; x ? g.lineTo(x, y) : g.moveTo(x, y); }
			g.stroke();
		}
		{	// Filter (band from FLTF, width FLTW, resonance FLTQ) and EQ peak (EQF, EQG)
			const [g, w, h] = canvasOf(frame, "filter");
			grid(g, w, h, [100, 1000, 10000].map(f => Math.log(f / 20) / Math.log(1000) * w), [h * 0.5]);
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
			g.fillStyle = css("--curve2"); g.beginPath(); g.arc(Math.log(eqf / 20) / Math.log(1000) * w, h * 0.5 - eqg / 24 * h * 0.42, 3, 0, 7); g.fill();
			if(p[15] > 0) { g.fillStyle = css("--warn"); g.font = `10px ${css("--font")}`; g.fillText(`SRR ${p[15]}`, w - 50, h - 8); }
		}
		{	// LFO: two periods of the shape at LFOS, scaled by LFOD
			const [g, w, h] = canvasOf(frame, "lfo");
			grid(g, w, h, [w / 2], [h * 0.42]);
			const depth = p[22] / 127, periods = 1 + p[21] / 127 * 5, shape = t.lfo.shape1;
			let seed = 7; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
			const steps = []; for(let i = 0; i < 64; ++i) steps.push(rnd() * 2 - 1);
			const wave = ph => { const f = ph - Math.floor(ph);
				return [1 - 4 * Math.abs(f - 0.5), 1 - 2 * f, f < 0.5 ? 1 : -1, 2 * f - 1, Math.exp(-4 * f) * 2 - 1, steps[Math.floor(ph * 2) % 64]][shape]; };
			g.beginPath(); g.strokeStyle = depth > 0 ? css("--curve") : css("--ink-faint"); g.lineWidth = 2;
			for(let x = 0; x <= w; ++x) { const y = h * 0.42 - wave(x / w * periods) * Math.max(depth, 0.04) * h * 0.3; x ? g.lineTo(x, y) : g.moveTo(x, y); }
			g.stroke();
		}
	}

	function renderFrame(frame) {
		const view = frame.dataset.view || "PISTE";
		const body = view === "MIX" ? mixView() : view === "SORTIES" ? outputsView() : trackView();
		frame.innerHTML = `<div class="ui" style="--font:${frame.dataset.font}">${top(view)}${body}</div>`;
		if(view === "PISTE") drawScreens(frame);
	}

	const renderAll = () => document.querySelectorAll(".frame").forEach(renderFrame);

	function flash(frame, i) {
		const row = frame.querySelector(`.trow[data-t="${i}"]`);
		if(row) { row.classList.add("hit"); setTimeout(() => row.classList.remove("hit"), 160); }
		const m = frame.querySelector(".meter i"); if(m) { m.style.width = (tracks[i].level / 1.27) + "%"; setTimeout(() => m.style.width = "0", 200); }
	}

	let drag = null;
	function onDown(e) {
		const frame = e.target.closest(".frame"); if(!frame) return;
		const tab = e.target.closest(".tab[data-view]"), trow = e.target.closest(".trow"), mute = e.target.closest("[data-mute]"), knob = e.target.closest(".knob"),
			chip = e.target.closest(".chip"), fader = e.target.closest("[data-fader]"), strip = e.target.closest(".strip[data-t]");
		if(tab) { frame.dataset.view = tab.dataset.view; renderFrame(frame); return; }
		if(mute) { const i = +mute.dataset.mute; tracks[i].mute = !tracks[i].mute; renderAll(); return; }
		if(chip) { const t = tracks[sel]; if(chip.dataset.shape) t.lfo.shape1 = +chip.dataset.shape; else t.lfo.mode = +chip.dataset.mode; renderAll(); return; }
		if(e.target.closest('[data-screen="hit"]')) { flash(frame, sel); return; }
		if(trow) { sel = +trow.dataset.t; renderAll(); flash(frame, sel); return; }
		// A drag: vertical, 1.5 px per step for knobs, the fader's own height for faders.
		if(fader) { const tr = tracks[+fader.dataset.fader]; drag = { y: e.clientY, start: tr.level, step: fader.clientHeight / 127, set: v => tr.level = v }; }
		else if(knob && knob.dataset.mix) { const tr = tracks[+knob.dataset.track], i = +knob.dataset.mix; drag = { y: e.clientY, start: tr.p[i], step: 1.5, set: v => tr.p[i] = v }; }
		else if(knob && knob.dataset.level) drag = { y: e.clientY, start: tracks[sel].level, step: 1.5, set: v => tracks[sel].level = v };
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
		const k = e.target.closest(".knob[data-i]"); if(!k) return;
		tracks[sel].p[+k.dataset.i] = defaults[+k.dataset.i]; renderAll();
	}

	// How far apart the widest and narrowest digit are in a font, at 12 px: 0 means a fixed digit width, so a changing
	// value does not shift its neighbours (RmlUi cannot ask a font for tabular figures).
	function digitSpread(font) {
		const g = document.createElement("canvas").getContext("2d");
		g.font = `12px ${font}`;
		const widths = "0123456789".split("").map(d => g.measureText(d).width);
		return Math.max(...widths) - Math.min(...widths);
	}

	function start() {
		document.addEventListener("pointerdown", onDown);
		document.addEventListener("pointermove", onMove);
		document.addEventListener("pointerup", () => drag = null);
		document.addEventListener("dblclick", onDouble);
		renderAll();
		// Redraw once the web fonts have loaded: the first pass may have used a fallback.
		if(document.fonts) document.fonts.ready.then(renderAll);
	}

	return { start, renderAll, digitSpread };
})();
