# 05 — UI mock (Phase 2)

## Why mock first
The mock is how you show an agent *exactly* what you mean. It also fixes the
parameter list from the user's side: every knob in the mock must exist in
`parameters.yaml`, and vice versa (a script checks this).

## Tooling recommendation
Build the mock as **plain HTML/CSS/JS in `mock/`** (or a Claude Design canvas
first, then port). Reasons:
- Agents iterate on HTML extremely fast; you review in a browser.
- With JUCE 8's WebView UI, this same code becomes the real editor in Phase 3 —
  no throwaway work.
- Fake data: the mock reads `tests/fixtures/` JSON so you see real-looking motion.

## Screen layout (v1 proposal, 1000 × 640, resizable)

```
┌───────────────────────────────────────────────────────────────────────┐
│ WORLDTABLE   [Preset ▾]   ● Live  ○ Frozen   Focus: Global ▾   ⚙   │
├──────────────────────────────┬────────────────────────────────────────┤
│  WORLD                       │  SOUND                                 │
│  ┌────────────────────────┐  │  Osc A  [3D wavetable view]  Pos ◯     │
│  │  globe / map with live │  │  Osc B  [3D wavetable view]  Pos ◯     │
│  │  events (quakes, buoys)│  │  A/B ◯  Unison ◯  Detune ◯  Noise ◯    │
│  └────────────────────────┘  │  Filter: Cutoff ◯ Res ◯ Drive ◯        │
│  D1 quake.energy  ▁▂▅▃▁ ◯    │  Space: Delay ◯  Reverb Size ◯  Mix ◯  │
│  D2 kp.index      ▁▁▂▂▃ ◯    │                                        │
│  D3 wind.speed    ▃▄▅▄▃ ◯    ├────────────────────────────────────────┤
│  … (sparkline + amount)      │  MOTION                                │
│                              │  LFO1–4: shape · rate · (data→rate)    │
│                              │  Mod matrix (collapsible, 16 rows)     │
├──────────────────────────────┴────────────────────────────────────────┤
│ Listen ● / Play ○   Root C ▾  Scale Dorian ▾   Sensitivity ◯   Time ×1 ▾ │
│ "Last update: M5.2 Tonga 4 min ago → bass weight +12 %"   (event ticker) │
└───────────────────────────────────────────────────────────────────────┘
```

## Interaction requirements
- Hovering a data channel highlights everything it modulates (and vice versa).
- Event ticker explains in words what just changed the sound (the "legible" principle).
- Drag a D-channel onto any knob → creates a matrix slot (Vital/Bitwig-style).
- Freeze toggle shows the timestamp of the frozen world.
- Look: dark, calm, low contrast; motion slow (no flashing). Monochrome + one accent.

## Phase-2 exit criteria
- `mock/index.html` runs locally with fixture data animating.
- Every control maps to a param ID from `parameters.yaml` (checked by `tools/check_mock_params.py`).
- You have clicked through it and written review notes in `spec/05-ui-review.md`.
