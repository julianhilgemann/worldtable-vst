// Module Worker: a Journey bounce off the main thread, so the dome keeps moving while it renders.
import { renderBounce } from './render.js';

self.onmessage = ({ data }) => {
  let shown = -1;
  const onProgress = (f) => {
    const pct = Math.floor(f * 100);
    if (pct !== shown) { shown = pct; self.postMessage({ progress: f }); }
  };
  const result = renderBounce({ ...data, onProgress });
  self.postMessage({ done: true, wav: result.wav, stats: result.stats, events: result.events }, [result.wav]);
};
