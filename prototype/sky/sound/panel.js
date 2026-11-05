// SOUND block of the sketch page: audio on/off, harmony controls, tuning, browser bounce.
// While sound is on, the sky lives in the AudioWorklet and the page reads its posted snapshots.

import { SOUND_DEFAULTS } from './engine.js';
import { DEFAULTS } from '../model.js';
import { MOODS, KEY_NAMES } from './harmony.js';

const TUNING = [
  ['progression', 0, 1, 0.05], ['register', -12, 12, 1], ['spread', 1, 3, 0.25],
  ['red', 0, 2, 0.05], ['green', 0, 2, 0.05], ['violet', 0, 2, 0.05],
  ['redT60', 2, 40, 0.5], ['greenT60', 1, 20, 0.5], ['violetT60', 0.5, 10, 0.1],
  ['bed', 0, 1, 0.01], ['rise', 0, 2, 0.05], ['strike', 0, 3, 0.05],
  ['detuneCents', 0, 10, 0.5], ['driftCents', 0, 15, 0.5], ['gainSlewS', 0.5, 6, 0.1], ['xfadeS', 2, 40, 1],
  ['width', 0, 1, 0.05], ['reverbMix', 0, 1, 0.01], ['reverbT60', 1, 30, 0.5],
];

/** The worklet's sky, seen from the page. */
class AudioSky {
  static async start(options, volume) {
    const ctx = new AudioContext({ latencyHint: 'playback' });
    await ctx.audioWorklet.addModule(new URL('./worklet.js', import.meta.url));
    const node = new AudioWorkletNode(ctx, 'worldtable-sky', {
      numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: options,
    });
    const gain = ctx.createGain();
    gain.gain.value = 0;
    const sky = new AudioSky(ctx, node, gain);                         // listen before anything can fail
    node.connect(gain).connect(ctx.destination);
    gain.gain.setTargetAtTime(volume, ctx.currentTime, 0.3);           // fade in, never a jump
    if (ctx.state !== 'running') await ctx.resume();
    return sky;
  }

  constructor(ctx, node, gain) {
    Object.assign(this, { ctx, node, gain, snap: null, events: [], load: 0, error: null });
    node.onprocessorerror = () => { this.error ??= 'the audio processor stopped (see console)'; };
    node.port.onmessage = ({ data }) => {
      if (data.error) { this.error = data.error; console.error('worklet:', data.error); return; }
      if (data.ready) return;                                         // constructed; snapshots follow
      this.snap = data.snap; this.load = data.load;
      this.events.push(...data.events);
    };
  }

  post(message) { this.node.port.postMessage(message); }
  takeEvents() { const e = this.events; this.events = []; return e; }
  setVolume(v) { this.gain.gain.setTargetAtTime(v, this.ctx.currentTime, 0.1); }
  async setPlaying(on) { if (on) await this.ctx.resume(); else await this.ctx.suspend(); }

  async stop() {
    this.gain.gain.setTargetAtTime(0, this.ctx.currentTime, 0.08);
    await new Promise((r) => setTimeout(r, 450));
    this.node.port.onmessage = null;
    await this.ctx.close();
  }
}

/**
 * @param host { $, state, getStart() → { track, startMs, fast }, currentMs(), onStop(snap), writeHash() }
 */
export class SoundPanel {
  constructor(host) {
    this.host = host;
    this.audio = null;
    this.lastReason = '';
    const { $, state } = host;
    state.sound ??= {};
    this.volume = 0.8;

    $('sound').addEventListener('click', () => (this.audio ? this.stop() : this.start()));
    $('volume').addEventListener('input', (e) => { this.volume = Number(e.target.value); this.audio?.setVolume(this.volume); });

    for (const m of MOODS) $('mood').append(new Option(m, m, false, m === this.value('mood')));
    $('mood').addEventListener('change', (e) => this.set({ mood: e.target.value }));
    KEY_NAMES.forEach((name, i) => $('root').append(new Option(name, i, false, i === this.value('key'))));
    $('root').addEventListener('change', (e) => this.set({ key: Number(e.target.value) }));

    document.querySelectorAll('.voices button').forEach((b) => b.addEventListener('click', async () => {
      this.set({ voicesPerNode: Number(b.dataset.vpn) }, false);
      this.markVoices();
      if (this.audio) { await this.stop(); await this.start(); }     // a new bank size needs a new engine
    }));
    this.markVoices();

    const box = $('sound-tuning');
    for (const [key, min, max, step] of TUNING) {
      const wrap = document.createElement('label');
      wrap.className = 'slider';
      wrap.innerHTML = `<span>${key}</span><output>${this.value(key)}</output><input type="range" min="${min}" max="${max}" step="${step}" value="${this.value(key)}">`;
      const input = wrap.querySelector('input'), out = wrap.querySelector('output');
      input.addEventListener('input', () => { out.textContent = input.value; this.set({ [key]: Number(input.value) }); });
      box.append(wrap);
    }
    $('sound-copy').addEventListener('click', async () => {
      const text = JSON.stringify(state.sound, null, 2);
      $('sound-constants').textContent = text;
      try { await navigator.clipboard.writeText(text); } catch { /* shown below instead */ }
    });
    $('bounce').addEventListener('click', () => this.bounce());
  }

  value(key) { return this.host.state.sound[key] ?? SOUND_DEFAULTS[key]; }

  markVoices() {
    document.querySelectorAll('.voices button').forEach((b) =>
      b.setAttribute('aria-pressed', String(Number(b.dataset.vpn) === this.value('voicesPerNode'))));
  }

  /** Change sound constants: kept in the hash, sent to the worklet (debounced while dragging). */
  set(patch, send = true) {
    Object.assign(this.host.state.sound, patch);
    this.host.writeHash();
    if (!send || !this.audio) return;
    this.queued = { ...this.queued, ...patch };
    clearTimeout(this.timer);
    this.timer = setTimeout(() => { this.audio?.post({ type: 'sound', sound: this.queued }); this.queued = null; }, 120);
  }

  async start() {
    const { $, state } = this.host;
    $('sound').disabled = true;
    try {
      const { track, startMs, fast } = this.host.getStart();
      this.audio = await AudioSky.start({
        track, seed: state.seed, params: state.params, sound: state.sound, startMs, speed: state.speed, fast,
      }, this.volume);
      if (!state.playing) await this.audio.setPlaying(false);
      $('sound').textContent = 'sound off';
      $('sound').setAttribute('aria-pressed', 'true');
    } catch (err) {
      $('r-chord').textContent = `audio failed: ${err.message}`;
      this.audio = null;
    }
    $('sound').disabled = false;
  }

  async stop() {
    const { $ } = this.host;
    const audio = this.audio;
    this.audio = null;
    if (audio.snap) this.host.onStop(audio.snap);                      // the page's model takes over here
    $('sound').textContent = 'sound on';
    $('sound').setAttribute('aria-pressed', 'false');
    $('r-chord').textContent = '-';
    $('r-load').textContent = '';
    await audio.stop();
  }

  /** Called every frame by the page while sound is on. */
  readout() {
    const { $ } = this.host;
    const snap = this.audio?.snap;
    if (!snap) {
      const a = this.audio;
      if (a) $('r-chord').textContent = a.error ?? `waiting for audio (${a.ctx.state}, ${a.ctx.currentTime.toFixed(1)} s)`;
      return;
    }
    $('r-chord').textContent = `${snap.chord}  ${snap.roman}`;
    $('r-chord-why').textContent = this.lastReason;
    $('r-load').textContent = `audio ${(100 * this.audio.load).toFixed(0)} % · ${snap.voices}×3 voices`;
  }

  noteEvents(events) {
    for (const e of events) if (e.type === 'chord') this.lastReason = e.reason;
  }

  bounce() {
    const { $, state } = this.host;
    if (this.worker) return;
    const { track } = this.host.getStart();
    const startMs = this.host.currentMs();
    const voices = (state.params.nodes ?? DEFAULTS.nodes) * this.value('voicesPerNode');
    const name = `T1.3-${state.day}-${new Date(startMs).toISOString().slice(11, 16).replace(':', '')}`
      + `-${state.speed}x-s${state.seed}-n${voices}.wav`;
    this.worker = new Worker(new URL('./bounce-worker.js', import.meta.url), { type: 'module' });
    $('bounce').disabled = true;
    $('bounce-status').textContent = 'rendering 0 %';
    this.worker.onmessage = ({ data }) => {
      if (!data.done) { $('bounce-status').textContent = `rendering ${Math.round(100 * data.progress)} %`; return; }
      const url = URL.createObjectURL(new Blob([data.wav], { type: 'audio/wav' }));
      const link = $('bounce-link');
      link.href = url; link.download = name; link.textContent = `download ${name}`; link.hidden = false;
      const s = data.stats;
      $('bounce-status').textContent = `peak ${s.peakDb.toFixed(1)} dBFS · RMS ${s.rmsDb.toFixed(1)} · `
        + `${data.events.filter((e) => e.type === 'chord').length} chord changes · ${s.realtimeFactor.toFixed(0)}× real time`;
      $('bounce').disabled = false;
      this.worker.terminate(); this.worker = null;
    };
    this.worker.onerror = (err) => {
      $('bounce-status').textContent = `bounce failed: ${err.message}`;
      $('bounce').disabled = false; this.worker = null;
    };
    this.worker.postMessage({
      track, seed: state.seed, params: state.params, sound: state.sound, startMs, speed: state.speed,
      seconds: 300, sampleRate: 48000,
    });
  }
}
