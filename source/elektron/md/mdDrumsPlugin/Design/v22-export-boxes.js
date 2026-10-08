// Exports the boxes of the v22 mockup's views as the RmlUi skin's audit (mdDrumsSkinTest) compares them: window pixels
// at 100 %, under keys the skin gives its elements as IDs, a section per view ("[track]", "[mix]", "[master]",
// "[kits]", "[browser]", "[lfomenu]"). v22-export.html, served over HTTP from this folder, runs it on the mockup as it
// starts (Track tab, track 01, arrows "caption", play "key") and shows in #out the text v22-boxes.txt holds.
// Elements as wide as their text (the LFO's target and Assign, the LFO menu's Done) are left out: another font engine
// sets them differently, and the audit checks their row and right edge instead.
(async () => {
	const frame = document.querySelector(".frame");
	const views = {};

	// Changed after approval, kept in every export: the Track tab's editing area starts at x 240, column 3's end, for
	// 16 px of room on both sides of the knobs (the user, 2026-10-08)
	const changed = { surface: [240, 176, 1056, 648], row_sel_bg: [0, 176, 240, 40], rule_391: [240, 391, 1056, 1],
		rule_607: [240, 607, 1056, 1] };

	const begin = view => {
		const ui = frame.querySelector(".ui");
		const origin = ui.getBoundingClientRect(), scale = origin.width / 1296;
		const boxes = views[view] = views[view] || {};
		const put = (key, element) => {
			if(!element)
				throw new Error(view + ": no element for " + key);
			if(boxes[key])
				throw new Error(view + ": two elements for " + key);
			const r = element.getBoundingClientRect();
			const q = v => Math.round(v / scale * 100) / 100;
			boxes[key] = changed[key] && view === "track" ? changed[key]
				: [q(r.left - origin.left), q(r.top - origin.top), q(r.width), q(r.height)];
		};
		const all = selector => [...ui.querySelectorAll(selector)];
		const one = selector => ui.querySelector(selector);
		const top = element => (element.getBoundingClientRect().top - origin.top) / scale;
		const left = element => (element.getBoundingClientRect().left - origin.left) / scale;
		const byX = list => list.sort((a, b) => left(a) - left(b));
		const withLabel = text => all(".cell").find(c => c.querySelector(":scope > .label")?.textContent === text);
		return { put, all, one, top, left, byX, withLabel };
	};
	// The mockup acts on pointerdown
	const click = selector =>
	{
		const e = frame.querySelector(selector);
		if(!e)
			throw new Error("no " + selector);
		e.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, button: 0 }));
		document.dispatchEvent(new PointerEvent("pointerup", { bubbles: true }));
	};
	const setView = view => { frame.dataset.view = view; MD.renderAll(); };

	// ---- Track ----
	{
		const { put, all, one, top, withLabel } = begin("track");
		put("logo", one(".logo"));
		for(const view of ["TRACK", "MIX", "MASTER"])
			put("tab_" + view.toLowerCase(), one(`.tab[data-view="${view}"]`));
		put("size", one("[data-scale]"));
		put("main_label", withLabel("Main"));
		put("main_meter", one(".outbars"));
		for(const rule of all(".rule"))
			put("rule_" + Math.round(top(rule)), rule);

		put("kit", one(".kitbar"));
		put("kit_save", one(".kitsave"));
		put("tracks_label", withLabel("Tracks"));
		put("caption", all(".cell").find(c => c.querySelector(":scope > .label")?.textContent.startsWith("Track ")));
		put("track_prev", one('[data-trackstep="-1"]'));
		put("track_next", one('[data-trackstep="1"]'));
		put("machine", one("#t1_machine"));
		put("family", one(".kindline"));
		put("output", all(".meta.inset").find(c => !c.classList.contains("kindline")));
		put("velocity_label", withLabel("Velocity"));
		put("velocity", one(".velv"));
		put("velocity_bar", one(".velbar"));
		put("play", one(".playkey"));

		const surfaces = all(".bg.surface"), x0 = frame.querySelector(".ui").getBoundingClientRect().left;
		put("surface", surfaces.find(s => s.getBoundingClientRect().left > x0));
		put("row_sel_bg", surfaces.find(s => s.getBoundingClientRect().left === x0));
		put("row_sel_mark", one(".selbar"));
		for(let t = 1; t <= 16; ++t)
		{
			put(`row${t}_num`, one(`[data-playt="${t - 1}"]`));
			put(`row${t}_name`, one(`[data-pick="${t - 1}"]`));
			put(`row${t}_scope`, one(`[data-scope="${t - 1}"]`).parentElement);
			put(`row${t}_mute`, one(`#t${t}_mute`));
			put(`row${t}_solo`, one(`#t${t}_solo`));
		}

		// Bands: titles, groups (left to right), knobs, names and values by the parameter they show
		const bands = ["syn", "efx", "routing"];
		const bandOf = element => Math.floor((top(element) - 176) / 216);
		for(const title of all(".btitle"))
			put(bands[bandOf(title)] + "_title", title);
		for(const band of [0, 1, 2])
			all(".grp").filter(g => bandOf(g) === band).sort((a, b) => a.getBoundingClientRect().left - b.getBoundingClientRect().left)
				.forEach((g, i) => put(`${bands[band]}_group${i + 1}`, g));
		const names = ["SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8", "AMD", "AMF", "EQF", "EQG", "FLTF",
			"FLTW", "FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"];
		for(const name of names)
		{
			const knob = one("#t1_" + name);
			put("k_" + name, knob);
			const x = knob.getBoundingClientRect().left, band = bandOf(knob);
			const same = selector => all(selector).find(c => bandOf(c) === band && c.getBoundingClientRect().left === x);
			put("n_" + name, same(".kl"));
			put("v_" + name, same(".kv"));
		}

		for(const screen of ["hit", "filter", "lfo"])
		{
			const element = one(`[data-screen="${screen}"]`);
			put("screen_" + screen, element);
			put(`screen_${screen}_title`, element.querySelector(".stitle"));
		}
		for(const [row, attribute] of [["shape1", "data-shape1"], ["shape2", "data-shape2"], ["mode", "data-mode"]])
		{
			const element = one(row === "mode" ? "#t1_lfoMode" : "#t1_lfoS" + row.slice(1));
			put("lfo_" + row + "_label", element.querySelector(".label"));
			for(const chip of element.querySelectorAll(`[${attribute}]`))
				put(`lfo_${row}_${chip.getAttribute(attribute)}`, chip);
		}
	}

	// ---- Mix ----
	setView("MIX");
	{
		const { put, all, one, byX } = begin("mix");
		put("mix_surface", all(".bg.surface")[0]);
		put("strip_sel_bg", all(".bg").find(b => b.getAttribute("style").includes("--chrome")));
		put("strip_sel_bar", one(".stripsel"));
		byX(all(".sepline")).forEach((s, i) => put(`strip_sep${i + 2}`, s));
		const panLabels = byX(all(".cell").filter(c => c.querySelector(":scope > .label")?.textContent === "Pan"));
		const levels = byX(all(".cell.c.value"));
		for(let t = 1; t <= 16; ++t)
		{
			put(`strip${t}_num`, one(`.sn[data-playt="${t - 1}"]`));
			put(`strip${t}_name`, one(`[data-strip="${t - 1}"]`));
			put(`strip${t}_pan`, one(`#t${t}_PAN`));
			put(`strip${t}_pan_label`, panLabels[t - 1]);
			put(`strip${t}_meter`, one(`[data-vu="${t - 1}"]`).parentElement);
			put(`strip${t}_fader`, one(`#t${t}_level`));
			put(`strip${t}_level`, levels[t - 1]);
			put(`strip${t}_mute`, one(`#t${t}_mute`));
			put(`strip${t}_solo`, one(`#t${t}_solo`));
			put(`strip${t}_out`, one(`#t${t}_out`));
		}
	}

	// ---- Master ----
	setView("MASTER");
	{
		const { put, all, one, top, left, byX } = begin("master");
		put("master_surface", all(".bg.surface")[0]);
		for(const rule of all(".rule"))
			put("rule_" + Math.round(top(rule)), rule);
		const fx = [["echo", "RHYTHM ECHO"], ["reverb", "GATE BOX"], ["eq", "EQ"], ["dyn", "DYNAMIX"]];
		const names = { echo: ["TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"], reverb: ["DVOL", "PRED", "DEC",
			"DAMP", "HP", "LP", "GATE", "LEV"], eq: ["LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"], dyn: ["ATCK", "REL",
			"TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"] };
		const titles = all(".btitle"), roles = all(".fxrole");
		for(const [key, title] of fx)
		{
			const t = titles.find(e => e.textContent === title);
			put(`${key}_title`, t);
			const x0 = left(t), y0 = top(t) - 28;
			put(`${key}_role`, roles.find(r => left(r) === x0 && Math.abs(top(r) - (y0 + 60)) < 1));
			byX(all(".grp").filter(g => Math.abs(top(g) - (y0 + 116)) < 1 && left(g) >= x0 && left(g) < x0 + 640))
				.forEach((g, i) => put(`${key}_group${i + 1}`, g));
			for(const name of names[key])
			{
				const knob = one(`#m_${key}_${name}`), kx = left(knob);
				put(`k_${key}_${name}`, knob);
				put(`n_${key}_${name}`, all(".kl").find(c => left(c) === kx && Math.abs(top(c) - (y0 + 212)) < 1));
				put(`v_${key}_${name}`, all(".kv").find(c => left(c) === kx && Math.abs(top(c) - (y0 + 228)) < 1));
			}
		}
		const sendsTitle = titles.find(e => e.textContent === "Sends");
		put("sends_title", sendsTitle);
		const y = top(sendsTitle) - 28;
		put("sends_role", roles.find(r => Math.abs(top(r) - (y + 28)) < 1));
		const sendNames = byX(all(".sm").filter(c => Math.abs(top(c) - (y + 92)) < 1));
		for(let t = 1; t <= 16; ++t)
		{
			put(`send${t}_num`, one(`.sn[data-playt="${t - 1}"]`));
			put(`send${t}_name`, sendNames[t - 1]);
			for(const name of ["DEL", "REV"])
			{
				const knob = one(`#t${t}_${name}`), kx = left(knob);
				put(`k_${name}_${t}`, knob);
				put(`n_${name}_${t}`, all(".kl").find(c => left(c) === kx && Math.abs(top(c) - (y + 164)) < 1));
				put(`v_${name}_${t}`, all(".kv").find(c => left(c) === kx && Math.abs(top(c) - (y + 180)) < 1));
			}
		}
	}

	// ---- Kits, over the Track tab; the note as a second click (Delete) shows it ----
	setView("TRACK");
	click("[data-kitopen]");
	{
		const { put, all, one, top } = begin("kits");
		put("kit", one(".kitbar"));
		put("kit_save", one(".kitsave"));
		put("kits_title", one(".bhead"));
		put("kits_actions", one(".bact"));
		for(const rule of all(".rule"))
			put("rule_" + Math.round(top(rule)), rule);
		for(let i = 1; i <= 64; ++i)
			put(`slot${i}`, one(`[data-kitslot="${i - 1}"]`));
		put("slot_sel_bg", one(".bg.surface"));
		put("slot_play_mark", one(".selbar"));
		put("kits_sep", one(".sepline"));
		put("kit_caption", all(".cell.r24").find(c => c.textContent.startsWith("Slot")));
		put("kit_name_big", one("[data-kitname]"));
		put("kit_tracks", all(".grp").find(g => g.textContent === "Tracks"));
		all(".kitm").sort((a, b) => a.textContent.localeCompare(b.textContent)).forEach((m, t) => put(`kitm${t + 1}`, m));
		put("kit_actions", one(".kact"));
	}
	click("[data-kitdelete]");
	begin("kits").put("kit_note", frame.querySelector(".cell.hint"));
	click("[data-kitcancel]");
	click("[data-kitopen]");

	// ---- The machine browser, over the bands ----
	click("[data-browse]");
	{
		const { put, all, one, byX } = begin("browser");
		put("browser_title", one(".bhead"));
		put("browser_actions", one(".bact"));
		const families = ["GND", "TRX", "EFM", "E12", "P-I", "ROM"], fams = all(".grp.fam");
		for(const f of families)
			put(`fam_${f}`, fams.find(e => e.firstChild.textContent === f));
		put("preview_group", fams.find(e => e.textContent === "Preview"));
		byX(all(".kind")).forEach((k, i) => put(`kind_${families[i]}`, k));
		for(const m of all("[data-mach]"))
			put(`mach_${m.dataset.mach}`, m);
		put("preview_name", one(".pvname"));
		put("preview_listen", one(".pvauto"));
		const screen = one('[data-screen="preview"]');
		put("screen_preview", screen);
		put("screen_preview_title", screen.querySelector(".stitle"));
	}
	click("[data-cancel]");

	// ---- The LFO's target menu, over the LFO screen ----
	click("#t1_lfoTarget");
	{
		const { put, all, one } = begin("lfomenu");
		put("lfomenu", one(".lfomenu"));
		put("lfomenu_head", one(".mhead"));
		for(const t of all("[data-lfotrack]"))
			put(`lfomenu_track${+t.dataset.lfotrack + 1}`, t);
		for(const p of all("[data-lfoparam]"))
			put(`lfomenu_param${+p.dataset.lfoparam + 1}`, p);
	}
	click(".mhead .done");

	const header = ["# The v22 mockup's boxes by view (v22-export-boxes.js), \"key x y width height\" in window pixels.",
		"# Changed since approval: surface, row_sel_bg, rule_391 and rule_607 start at x 240, not 248 (16 px on both",
		"# sides of the knobs; the user, 2026-10-08)."];
	const sections = Object.entries(views).map(([view, boxes]) => [`[${view}]`, ...Object.keys(boxes)
		.sort((a, b) => boxes[a][1] - boxes[b][1] || boxes[a][0] - boxes[b][0]).map(k => `${k} ${boxes[k].join(" ")}`)]);
	return [...header, ...sections.flat()].join("\n") + "\n";
})()
