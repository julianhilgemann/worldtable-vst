// T6.14: evaluates the page's EQ curve (UI.eqDb in source/ui/widgets.js) for EqTests.cpp, which
// compares it with the engine (Eq::responseDb). Loads params.js, bridge.js (the browser mock: no
// backend) and widgets.js into a bare `window`, nothing else.
//   node eq_page_curve.js '{"fs":48000,"freqs":[…],"settings":[{"eq1_freq":110,…},…]}'
// prints [[dB at each freq] per setting] as JSON.
"use strict";
const fs = require("fs"), path = require("path"), vm = require("vm");

const ui = path.join(__dirname, "..", "..", "source", "ui");
const win = { devicePixelRatio: 1, console };
win.window = win;
vm.createContext(win);
for (const file of ["params.js", "bridge.js", "widgets.js"])
  vm.runInContext(fs.readFileSync(path.join(ui, file), "utf8"), win, { filename: file });

const req = JSON.parse(process.argv[2]);
// a setting maps parameter ids to values; UI.eqDb takes { bands: [shelf, bell, bell], cutHz } (T6.12)
const params = (s) => ({ bands: [{ hz: s.eq1_freq, gain: s.eq1_gain }, { hz: s.eq2_freq, gain: s.eq2_gain, q: s.eq2_q },
  { hz: s.eq3_freq, gain: s.eq3_gain, q: s.eq3_q }], cutHz: s.eq4_freq });
const out = req.settings.map((s) => req.freqs.map((f) => win.UI.eqDb(f, params(s), s.fs || req.fs)));
process.stdout.write(JSON.stringify(out));
