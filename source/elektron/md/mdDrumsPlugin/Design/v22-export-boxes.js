// Exports the boxes of the v22 mockup's Track view as the RmlUi skin's audit compares them (ticket 17 of the editor
// map): window pixels at 100 %, under keys the skin gives its elements as IDs. Serve this folder over HTTP, open
// v22-test.html, leave it as it starts (Track tab, track 01, arrows "caption", play "key"), run this file's text in its
// console (await (await fetch("v22-export-boxes.js")).text().then(eval)) and save the object as v22-boxes.txt: a line
// per key, "key x y width height", sorted by y then x. Keep the changes v22-boxes.txt's header names.
(() => {
	const ui = document.querySelector(".ui");
	const origin = ui.getBoundingClientRect(), scale = origin.width / 1296;
	const boxes = {};
	const put = (key, element) => {
		if(!element)
			throw new Error("no element for " + key);
		if(boxes[key])
			throw new Error("two elements for " + key);
		const r = element.getBoundingClientRect();
		const q = v => Math.round(v / scale * 100) / 100;
		boxes[key] = [q(r.left - origin.left), q(r.top - origin.top), q(r.width), q(r.height)];
	};
	const all = selector => [...ui.querySelectorAll(selector)];
	const one = selector => ui.querySelector(selector);
	const withLabel = text => all(".cell").find(c => c.querySelector(":scope > .label")?.textContent === text);
	const top = element => element.getBoundingClientRect().top - origin.top;

	// Top bar
	put("logo", one(".logo"));
	for(const view of ["TRACK", "MIX", "MASTER"])
		put("tab_" + view.toLowerCase(), one(`.tab[data-view="${view}"]`));
	put("size", one("[data-scale]"));
	put("main_label", withLabel("Main"));
	put("main_meter", one(".outbars"));

	// Rules, by their height on the window
	for(const rule of all(".rule"))
		put("rule_" + Math.round(top(rule) / scale), rule);

	// Head band
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

	// The editing area's surface, then the track list
	const surfaces = all(".bg.surface");
	put("surface", surfaces.find(s => s.getBoundingClientRect().left > origin.left));
	put("row_sel_bg", surfaces.find(s => s.getBoundingClientRect().left === origin.left));
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
	const bandOf = element => Math.floor((top(element) / scale - 176) / 216);
	for(const title of all(".btitle"))
		put(bands[bandOf(title)] + "_title", title);
	for(const band of [0, 1, 2])
		all(".grp").filter(g => bandOf(g) === band).sort((a, b) => a.getBoundingClientRect().left - b.getBoundingClientRect().left)
			.forEach((g, i) => put(`${bands[band]}_group${i + 1}`, g));
	const names = ["SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8", "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW",
		"FLTQ", "SRR", "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"];
	for(const name of names)
	{
		const knob = one("#t1_" + name);
		put("k_" + name, knob);
		const x = knob.getBoundingClientRect().left, band = bandOf(knob);
		const same = selector => all(selector).find(c => bandOf(c) === band && c.getBoundingClientRect().left === x);
		put("n_" + name, same(".kl"));
		put("v_" + name, same(".kv"));
	}

	// Screens, their titles, and the LFO's chips. The LFO's target and Assign are as wide as their text, which another
	// font engine sets differently: the audit checks their row and Assign's right edge instead.
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
	return boxes;
})()
