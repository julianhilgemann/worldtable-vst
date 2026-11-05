// AudioWorklet: the engine on the audio thread (ADR-0007). The audio callback steps the sky;
// the page only reads the snapshots posted here, the way the plug-in's UI reads SkySnapshots.
// Messages in: seek, speed, sound, sky. Messages out: { snap, events, load } at ~50 Hz.
/* global sampleRate, registerProcessor, AudioWorkletProcessor */

import { SoundEngine } from './engine.js';

const clock = () => (globalThis.performance ? globalThis.performance.now() : Date.now());

class SkyProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    try {
      const t0 = clock();
      this.engine = new SoundEngine({ ...options.processorOptions, sampleRate });
      this.port.postMessage({ ready: true, ms: clock() - t0 });
    } catch (err) {
      this.fail(err);
    }
    this.postEvery = Math.round(sampleRate / 50);
    this.since = this.postEvery;
    this.busyMs = 0; this.frames = 0; this.load = 0;
    this.port.onmessage = ({ data }) => {
      const e = this.engine;
      if (data.type === 'seek') e.seek(data.ms);
      else if (data.type === 'speed') e.setSpeed(data.speed);
      else if (data.type === 'sound') e.setSound(data.sound);
      else if (data.type === 'sky') e.rebuildSky(data);
    };
  }

  /** Report once and go silent: the page shows the message instead of a frozen sky. */
  fail(err) {
    if (!this.failed) this.port.postMessage({ error: `${err.message}\n${err.stack}` });
    this.failed = true;
  }

  process(_inputs, outputs) {
    if (this.failed) return true;
    const out = outputs[0];
    const L = out[0], R = out[1] || out[0];
    const t0 = clock();
    try {
      this.engine.process(L, R, L.length);
    } catch (err) {
      L.fill(0); R.fill(0);
      this.fail(err);
      return true;
    }
    this.busyMs += clock() - t0;
    this.frames += L.length;
    if (this.frames >= sampleRate) {                                  // share of real time spent here
      this.load = this.busyMs / ((1000 * this.frames) / sampleRate);
      this.busyMs = 0; this.frames = 0;
    }
    this.since += L.length;
    if (this.since >= this.postEvery) {
      this.since = 0;
      this.port.postMessage({ snap: this.engine.snapshot(), events: this.engine.takeOutbox(), load: this.load });
    }
    return true;
  }
}

registerProcessor('worldtable-sky', SkyProcessor);
