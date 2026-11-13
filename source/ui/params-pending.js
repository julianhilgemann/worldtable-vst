/* T4.2, browser mock only: the motion parameters the engine PR adds to spec/parameters.yaml
 * (lfo1_rate … env_trigger), so the mock page has them before params.js does. Each is added only
 * when params.js lacks it, so once the engine lands this file does nothing (and goes). Inside the
 * plug-in it never runs: there the parameters are the engine's. */
(function () {
  "use strict";
  if (window.__JUCE__ && window.__JUCE__.backend) return;
  const P = window.WT_PARAMS;
  /** JUCE's skew for a range whose middle is `mid` (tools/gen_params.py, `mid:`). */
  const skew = (min, max, mid) => Math.log(0.5) / Math.log((mid - min) / (max - min));
  const rate = (name, def) => ({ name, type: "float", group: "motion", min: 0.01, max: 2, default: def, skew: skew(0.01, 2, 0.14), unit: "Hz" });
  const time = (name, def) => ({ name, type: "float", group: "motion", min: 0.01, max: 10, default: def, skew: skew(0.01, 10, 0.316), unit: "s" });
  const SHAPES = ["Sine", "Triangle", "Smooth random", "Stepped random"];
  const MORE = {
    lfo1_rate: rate("LFO 1 Rate", 0.12),
    lfo1_shape: { name: "LFO 1 Shape", type: "choice", group: "motion", choices: SHAPES, default: 0 },
    lfo2_rate: rate("LFO 2 Rate", 0.03),
    lfo2_shape: { name: "LFO 2 Shape", type: "choice", group: "motion", choices: SHAPES, default: 2 },
    env_attack: time("Envelope Attack", 0.1),
    env_decay: time("Envelope Decay", 0.63),
    env_sustain: { name: "Envelope Sustain", type: "float", group: "motion", min: 0, max: 1, default: 0.35 },
    env_release: time("Envelope Release", 1.45),
    env_trigger: { name: "Envelope Trigger", type: "choice", group: "motion", choices: ["Onset", "Note", "Fire only"], default: 0 },
  };
  for (const id of Object.keys(MORE)) if (!P[id]) P[id] = MORE[id];
})();
