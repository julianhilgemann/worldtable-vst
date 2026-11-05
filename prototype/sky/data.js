// Track loading and preparation (spec/02-data-sources.md "From readings to the Sky Model").
// In the plug-in this work belongs to the data thread (TrackBuilder); the model only samples.

export const DAYS = [
  { id: '2024-05-10', label: '10–11 May 2024 · Gannon storm (Kp 9)' },
  { id: '2025-01-19', label: '19–20 Jan 2025 · moderate, substorms' },
  { id: '2025-01-25', label: '25–26 Jan 2025 · quiet' },
];

/** Values used when a channel has no data at all, and what long gaps decay toward. */
export const CLIMATOLOGY = { kp: 2, v: 400, n: 5, bz: 0, by: 0 };

const SHORT_GAP_MIN = 10;      // gaps up to this long are interpolated
const DECAY_TAU_MIN = 60;      // longer gaps: hold the last value, decay toward climatology
const MIN_MS = 60000;

/** Browser: fetch the track JSON relative to this module. Node: pass a reader. */
export async function loadDay(dayId, readJson) {
  if (readJson) return prepareTrack(await readJson(dayId));
  const url = new URL(`../../resources/sky/${dayId}/track.json`, import.meta.url);
  const response = await fetch(url);
  if (!response.ok) throw new Error(`track ${dayId}: HTTP ${response.status}`);
  return prepareTrack(await response.json());
}

/** Raw track.json → gap-filled Float64Arrays + validity + quiet-time H baseline. */
export function prepareTrack(raw) {
  const length = raw.length;
  const ch = {}, valid = {};
  const baselineH = quietBaseline(raw.channels.h);
  const clim = { ...CLIMATOLOGY, h: baselineH };
  for (const [name, values] of Object.entries(raw.channels)) {
    const out = new Float64Array(length);
    const ok = new Uint8Array(length);
    for (let i = 0; i < length; i++) ok[i] = values[i] === null ? 0 : 1;
    fillGaps(values, out, clim[name]);
    ch[name] = out;
    valid[name] = ok;
  }
  return {
    day: raw.day,
    startMs: Date.parse(raw.start),
    stepMs: raw.step_s * 1000,
    length,
    endMs: Date.parse(raw.start) + length * raw.step_s * 1000,
    ch, valid, baselineH,
  };
}

function fillGaps(values, out, climate) {
  const n = values.length;
  let i = 0;
  let last = null, lastIndex = -1;
  while (i < n) {
    if (values[i] !== null) { out[i] = values[i]; last = values[i]; lastIndex = i; i++; continue; }
    let j = i;
    while (j < n && values[j] === null) j++;
    const gap = j - i;
    const next = j < n ? values[j] : null;
    for (let k = i; k < j; k++) {
      if (last !== null && next !== null && gap <= SHORT_GAP_MIN) {
        out[k] = last + (next - last) * (k - lastIndex) / (j - lastIndex);
      } else if (last !== null) {
        const w = Math.exp(-(k - lastIndex) / DECAY_TAU_MIN);
        out[k] = climate + (last - climate) * w;
      } else {
        out[k] = climate;
      }
    }
    i = j;
  }
}

/** Median H of the calmest 6 h stretch (lowest standard deviation): the station's quiet level. */
function quietBaseline(h) {
  const win = 360, hop = 30;
  let best = null, bestStd = Infinity;
  for (let s = 0; s + win <= h.length; s += hop) {
    const seg = h.slice(s, s + win).filter((x) => x !== null);
    if (seg.length < win * 0.8) continue;
    const mean = seg.reduce((a, b) => a + b, 0) / seg.length;
    const std = Math.sqrt(seg.reduce((a, b) => a + (b - mean) ** 2, 0) / seg.length);
    if (std < bestStd) { bestStd = std; best = seg; }
  }
  if (!best) best = h.filter((x) => x !== null);
  const sorted = [...best].sort((a, b) => a - b);
  return sorted[sorted.length >> 1];
}

/** Linear interpolation of a channel at a UTC time (ms); clamped to the track. */
export function sample(track, name, tMs) {
  const x = (tMs - track.startMs) / track.stepMs;
  const a = track.ch[name];
  if (x <= 0) return a[0];
  if (x >= track.length - 1) return a[track.length - 1];
  const i = Math.floor(x);
  return a[i] + (a[i + 1] - a[i]) * (x - i);
}

/** H deviation from the quiet level (nT). */
export function sampleDH(track, tMs) {
  return sample(track, 'h', tMs) - track.baselineH;
}

/** Change of H over the last `minutes` (nT): a substorm bay is strongly negative. */
export function sampleDHdt(track, tMs, minutes = 5) {
  return sample(track, 'h', tMs) - sample(track, 'h', tMs - minutes * MIN_MS);
}
