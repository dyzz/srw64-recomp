// Encodes one song's or line's PCM to Ogg Vorbis for the audio MOD makers (audio-tools.ts).
// The encoder is libvorbis compiled to WebAssembly (wasm-media-encoders, MIT); it runs here
// so the page stays responsive, and the page's own script policy never needs to allow
// WebAssembly.
import { createEncoder } from 'wasm-media-encoders';
import oggWasm from 'wasm-media-encoders/wasm/ogg?url';

type Job = { id: number; channels: Float32Array[]; sampleRate: number; quality: number };

let encoder: Awaited<ReturnType<typeof createEncoder<'audio/ogg'>>> | null = null;
const CHUNK = 4096;

self.onmessage = async ({ data }: MessageEvent<Job>) => {
  const { id, channels, sampleRate, quality } = data;
  try {
    encoder ??= await createEncoder('audio/ogg', oggWasm);
    encoder.configure({ channels: channels.length as 1 | 2, sampleRate, vbrQuality: quality });
    const parts: Uint8Array[] = [];
    let size = 0;
    const frames = channels[0].length;
    for (let at = 0; at < frames; at += CHUNK) {
      // The encoder owns the returned buffer until its next call: copy it out.
      const out = encoder.encode(channels.map((c) => c.subarray(at, at + CHUNK))).slice();
      parts.push(out); size += out.length;
      if ((at / CHUNK) % 256 === 0) self.postMessage({ id, progress: at / frames });
    }
    const last = encoder.finalize().slice();
    parts.push(last); size += last.length;
    const ogg = new Uint8Array(size);
    let o = 0;
    for (const p of parts) { ogg.set(p, o); o += p.length; }
    self.postMessage({ id, ogg }, [ogg.buffer]);
  } catch (error) {
    self.postMessage({ id, error: String(error) });
  }
};
