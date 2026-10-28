// What the audio MOD makers share (scripts/bgm-maker.ts, voice-maker.ts): reading an Ogg
// Vorbis file's header, and encoding PCM to Ogg Vorbis in a worker (vorbis-worker.ts), one
// job at a time. Everything stays in the browser.

// Ogg Vorbis files go into the package as they are: no second lossy pass.
export function vorbisInfo(b: Uint8Array): { rate: number; channels: number; loop?: [number, number] } | null {
  const at = (s: string, i: number) => [...s].every((c, k) => b[i + k] === c.charCodeAt(0));
  if (b.length < 64 || !at('OggS', 0)) return null;
  const first = 27 + b[26];
  if (b[first] !== 1 || !at('vorbis', first + 1)) return null;
  const view = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const channels = b[first + 11], rate = view.getUint32(first + 12, true);
  // LOOPSTART / LOOPLENGTH in the comment header, as many game music packs carry them.
  let loop: [number, number] | undefined;
  for (let i = first; i < Math.min(b.length - 16, 65536); i++) {
    if (b[i] !== 3 || !at('vorbis', i + 1)) continue;
    try {
      let p = i + 7;
      p += 4 + view.getUint32(p, true);
      const count = view.getUint32(p, true); p += 4;
      const tags: Record<string, string> = {};
      for (let k = 0; k < count && k < 200; k++) {
        const len = view.getUint32(p, true); p += 4;
        if (len > 4096 || p + len > b.length) break;
        const s = new TextDecoder().decode(b.subarray(p, p + len)); p += len;
        const eq = s.indexOf('=');
        if (eq > 0) tags[s.slice(0, eq).toUpperCase()] = s.slice(eq + 1);
      }
      const start = Number(tags.LOOPSTART), length = Number(tags.LOOPLENGTH);
      if (Number.isInteger(start) && Number.isInteger(length) && length > 0) loop = [start, start + length];
    } catch {}
    break;
  }
  return channels >= 1 && channels <= 2 && rate > 0 ? { rate, channels, loop } : null;
}

// One worker, one job at a time: a song's PCM is the largest thing a page holds. `progress`
// hears 0..1 as the job goes.
export function vorbisEncoder() {
  const worker = new Worker(new URL('./vorbis-worker.ts', import.meta.url), { type: 'module' });
  let queue = Promise.resolve();
  let jobId = 0;
  return (buffer: AudioBuffer, quality: number, progress?: (p: number) => void): Promise<Uint8Array> => {
    const run = () => new Promise<Uint8Array>((resolve, reject) => {
      const id = ++jobId;
      const channels = [...Array(Math.min(2, buffer.numberOfChannels))].map((_, c) => buffer.getChannelData(c).slice());
      worker.onmessage = ({ data: m }) => {
        if (m.id !== id) return;
        if (m.progress !== undefined) { progress?.(m.progress); return; }
        if (m.error) reject(new Error(m.error)); else resolve(m.ogg);
      };
      worker.postMessage({ id, channels, sampleRate: buffer.sampleRate, quality }, channels.map((c) => c.buffer));
    });
    const job = queue.then(run, run);
    queue = job.then(() => {}, () => {});
    return job;
  };
}
