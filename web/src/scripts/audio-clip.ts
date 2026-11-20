// One audio file in a maker page, shared by the BGM maker (bgm-maker.ts) and the unit MOD maker
// (unit-maker.ts: a new character's battle theme, battle voices): decoded by the browser, Vorbis
// kept as it is and anything else encoded in a worker (audio-tools.ts), its waveform drawn with
// the part that plays marked. A song loops (the whole file, a section, or plays once); a voice
// keeps a section (its silence cut) at a gain, baked into the file it is encoded to. Nothing
// leaves the player's machine.
import { vorbisEncoder, vorbisInfo } from './audio-tools';

export type LoopMode = 'whole' | 'range' | 'once';
export type ClipKind = 'music' | 'voice';
export type ClipState = 'decoding' | 'encoding' | 'ready' | 'error';
// Samples are at the file's own rate: music.json's loop points are, and so are a voice's cuts.
export interface Clip {
  key: string;                // the caller's (a song number, a line, a record)
  kind: ClipKind;
  fileName: string;
  source: Blob;               // what the player gave (or the file an opened package held)
  ogg?: Uint8Array;           // the file that goes into the package
  passthrough: boolean;       // already Vorbis
  rate: number;
  frames: number;
  loop: LoopMode;             // music; a voice plays its section once
  start: number;              // music: the loop section; a voice: the part kept
  end: number;
  gain: number;               // dB: music.json's for a song, baked into a voice
  peaks?: Float32Array;       // min/max pairs for the waveform
  level?: number;             // average level, dBFS
  state: ClipState;
  progress?: number;
  baked?: boolean;            // a voice's file holds its cut and gain
}
export interface ClipText {
  play: string; stop: string; seam: string; gain: string; loudness: string; loudnessOthers: string; seekHint: string;
  loop: string; loopWhole: string; loopRange: string; loopOnce: string; loopStart: string; loopEnd: string; loopHint: string;
  trimStart: string; trimEnd: string; trimHint: string;
}

export function audioKit(options: { rate?: number; peaks?: number } = {}) {
  const RATE = options.rate ?? 44100, PEAKS = options.peaks ?? 1600;
  let ctx: AudioContext | null = null;
  const audio = () => (ctx ??= new AudioContext({ sampleRate: RATE }));
  const encoder = vorbisEncoder();
  const decode = async (blob: Blob) => audio().decodeAudioData(await blob.arrayBuffer());

  function measure(buffer: AudioBuffer) {
    const chans = [...Array(Math.min(2, buffer.numberOfChannels))].map((_, c) => buffer.getChannelData(c));
    const n = buffer.length, peaks = new Float32Array(PEAKS * 2);
    let sum = 0, count = 0;
    for (let i = 0; i < PEAKS; i++) {
      let lo = 1, hi = -1;
      const a = Math.floor((i * n) / PEAKS), z = Math.max(a + 1, Math.floor(((i + 1) * n) / PEAKS));
      for (let j = a; j < z; j++) {
        let v = 0;
        for (const c of chans) v += c[j];
        v /= chans.length;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if ((j & 3) === 0) { sum += v * v; count++; }
      }
      peaks[2 * i] = lo; peaks[2 * i + 1] = hi;
    }
    return { peaks, level: 10 * Math.log10(sum / Math.max(1, count) + 1e-12) };
  }
  // The zero crossing nearest a sample (within 10 ms), so a cut or a loop does not click.
  function snap(buffer: AudioBuffer, samples: number, rate: number) {
    const ch = buffer.getChannelData(0), ratio = buffer.sampleRate / rate;
    const i = Math.round(samples * ratio), reach = Math.round(buffer.sampleRate * 0.01);
    for (let d = 0; d <= reach; d++) {
      for (const j of [i - d, i + d]) {
        if (j > 0 && j < ch.length && Math.sign(ch[j - 1]) !== Math.sign(ch[j])) return Math.round(j / ratio);
      }
    }
    return samples;
  }

  // The last decoded clip, for playing and snapping without decoding again.
  let preview: { clip: Clip; buffer: AudioBuffer } | null = null;
  async function bufferOf(clip: Clip) {
    if (preview?.clip === clip) return preview.buffer;
    // A voice baked to its cut plays from what it was cut from.
    const buffer = await decode(clip.kind === 'voice' || !clip.ogg ? clip.source : new Blob([clip.ogg as BlobPart]));
    preview = { clip, buffer };
    return buffer;
  }
  const forget = (clip: Clip) => { if (preview?.clip === clip) preview = null; };

  // A file in: measured, then kept (Vorbis) or encoded. `changed` follows each step. A song's
  // loop tags in the file (LOOPSTART/LOOPEND) are its loop unless `loopGiven`.
  async function load(clip: Clip, quality: number, changed: () => void, loopGiven = false) {
    try {
      const head = new Uint8Array(await clip.source.slice(0, 65536).arrayBuffer());
      const vorbis = vorbisInfo(head);
      clip.passthrough = !!vorbis;
      clip.rate = vorbis?.rate ?? RATE;
      if (vorbis?.loop && clip.kind === 'music' && !loopGiven) { clip.loop = 'range'; [clip.start, clip.end] = vorbis.loop; }
      const buffer = await decode(clip.source);
      Object.assign(clip, measure(buffer));
      clip.frames = clip.passthrough ? Math.round(buffer.duration * clip.rate) : buffer.length;
      if (!clip.end || clip.end > clip.frames) clip.end = clip.frames;
      if (clip.start >= clip.end) clip.start = 0;
      preview = { clip, buffer };
      if (clip.passthrough) {
        clip.ogg = new Uint8Array(await clip.source.arrayBuffer());
        clip.baked = true;
      } else {
        clip.state = 'encoding'; changed();
        clip.ogg = await encoder(buffer, quality, (p) => { clip.progress = p; changed(); });
        clip.baked = true;
      }
      clip.state = 'ready';
    } catch {
      clip.state = 'error';
    }
    changed();
  }
  // A voice with its cut or gain changed: encoded again from what it was cut from, the gain in it.
  async function bake(clip: Clip, quality: number, changed: () => void) {
    if (clip.kind !== 'voice' || clip.baked) return;
    const whole = clip.start === 0 && clip.end === clip.frames && clip.gain === 0;
    if (whole && clip.passthrough) { clip.ogg = new Uint8Array(await clip.source.arrayBuffer()); clip.baked = true; return; }
    const buffer = await bufferOf(clip);
    const ratio = buffer.sampleRate / clip.rate;
    const from = Math.round(clip.start * ratio), to = Math.max(from + 1, Math.min(buffer.length, Math.round(clip.end * ratio)));
    const cut = audio().createBuffer(buffer.numberOfChannels, to - from, buffer.sampleRate);
    const g = 10 ** (clip.gain / 20);
    for (let c = 0; c < buffer.numberOfChannels; c++) {
      const src = buffer.getChannelData(c).subarray(from, to), dst = cut.getChannelData(c);
      for (let i = 0; i < src.length; i++) dst[i] = Math.max(-1, Math.min(1, src[i] * g));
    }
    clip.state = 'encoding'; changed();
    clip.ogg = await encoder(cut, quality, (p) => { clip.progress = p; changed(); });
    clip.state = 'ready'; clip.baked = true;
    changed();
  }

  // --- Playing ---------------------------------------------------------------------
  let source: AudioBufferSourceNode | null = null;
  // What plays: the clip, where in it the source started (seconds) and when (context time),
  // and the part it runs.
  let now: { clip: Clip; offset: number; at: number; start: number; end: number; loop: boolean; duration: number } | null = null;
  const watchers = new Set<(clip: Clip | null, was: Clip | null) => void>();
  const frames = new Set<() => void>();
  const tell = (was: Clip | null) => { for (const w of watchers) w(now?.clip ?? null, was); };
  function stop() {
    try { source?.stop(); } catch {}
    source = null;
    const was = now?.clip ?? null; now = null;
    if (was) tell(was);
  }
  function position() {
    if (!now || !ctx) return null;
    let p = now.offset + (ctx.currentTime - now.at);
    if (now.loop && p >= now.end) p = now.start + ((p - now.start) % Math.max(0.001, now.end - now.start));
    return Math.min(p, now.duration);
  }
  function tick() { for (const f of frames) f(); if (now) requestAnimationFrame(tick); }
  // From `from` seconds, or 3 seconds before a song's loop end to hear the seam.
  async function play(clip: Clip, from: number | 'seam' = 0) {
    stop();
    const buffer = await bufferOf(clip);
    const c = audio();
    await c.resume();
    const s = c.createBufferSource();
    s.buffer = buffer;
    const gain = c.createGain();
    gain.gain.value = 10 ** (clip.gain / 20);
    s.connect(gain).connect(c.destination);
    const voice = clip.kind === 'voice';
    const ranged = voice || clip.loop === 'range';
    const start = ranged ? clip.start / clip.rate : 0;
    const end = ranged ? clip.end / clip.rate : buffer.duration;
    const loop = !voice && clip.loop !== 'once';
    if (loop) { s.loop = true; s.loopStart = start; s.loopEnd = end; }
    const offset = from === 'seam' ? Math.max(start, end - 3) : Math.max(voice ? start : 0, Math.min(from, buffer.duration - 0.01));
    if (voice) s.start(0, offset, Math.max(0.01, end - offset)); else s.start(0, offset);
    s.onended = () => { if (source === s) stop(); };
    source = s;
    const was = now?.clip ?? null;
    now = { clip, offset, at: c.currentTime, start, end, loop, duration: voice ? end : buffer.duration };
    tell(was);
    tick();
  }
  const playing = () => now?.clip ?? null;

  // --- The editor ------------------------------------------------------------------
  // `others`: the other clips' levels, to match this one to them. `edited`: a cut or a gain
  // changed (a voice's file then has to be encoded again, bake).
  function editor(clip: Clip, text: ClipText, options: { others?: number[]; edited?: () => void; extra?: Node[]; root?: Element } = {}) {
    const el = make;
    const canvas = el('canvas', { class: 'wave' }) as HTMLCanvasElement;
    const css = getComputedStyle(options.root ?? document.documentElement);
    const color = (v: string) => css.getPropertyValue(v).trim() || '#888';
    const voice = clip.kind === 'voice';
    const ranged = () => voice || clip.loop === 'range';
    const edited = () => { if (voice) clip.baked = false; options.edited?.(); };
    const draw = () => {
      if (!canvas.isConnected) { frames.delete(draw); return; }
      const w = (canvas.width = canvas.clientWidth * devicePixelRatio), h = (canvas.height = canvas.clientHeight * devicePixelRatio);
      const g = canvas.getContext('2d')!;
      g.clearRect(0, 0, w, h);
      const x = (samples: number) => (samples / Math.max(1, clip.frames)) * w;
      if (ranged()) { g.fillStyle = color('--cyan-soft'); g.fillRect(x(clip.start), 0, x(clip.end) - x(clip.start), h); }
      g.fillStyle = color('--muted');
      const p = clip.peaks;
      if (p) for (let i = 0; i < w; i++) {
        const k = Math.min(PEAKS - 1, Math.floor((i / w) * PEAKS));
        const lo = p[2 * k], hi = p[2 * k + 1];
        g.fillRect(i, ((1 - hi) / 2) * h, 1, Math.max(1, ((hi - lo) / 2) * h));
      }
      if (ranged()) {
        g.fillStyle = color('--cyan'); g.fillRect(x(clip.start) - devicePixelRatio, 0, 2 * devicePixelRatio, h);
        g.fillStyle = color('--gold'); g.fillRect(x(clip.end) - devicePixelRatio, 0, 2 * devicePixelRatio, h);
      }
      const head = now?.clip === clip ? position() : null;
      if (head !== null) { g.fillStyle = color('--text'); g.fillRect(x(head * clip.rate) - devicePixelRatio / 2, 0, devicePixelRatio, h); }
    };
    frames.add(draw);
    // Dragging a range line; on release it snaps to the nearest zero crossing.
    let dragging: 'start' | 'end' | null = null;
    const at = (e: PointerEvent) => Math.round(((e.clientX - canvas.getBoundingClientRect().left) / canvas.clientWidth) * clip.frames);
    const near = (s: number, samples: number) => Math.abs(s - samples) / clip.frames * canvas.clientWidth <= 8;
    // Near a line (within 8 pixels) a press moves it; anywhere else it plays from there.
    canvas.addEventListener('pointerdown', (e) => {
      const s = at(e);
      if (ranged() && (near(s, clip.start) || near(s, clip.end))) {
        dragging = Math.abs(s - clip.start) <= Math.abs(s - clip.end) ? 'start' : 'end';
        canvas.setPointerCapture(e.pointerId);
        move(e);
        return;
      }
      play(clip, Math.max(0, s) / clip.rate);
    });
    canvas.addEventListener('pointermove', (e) => {
      if (dragging || !ranged()) { canvas.style.cursor = dragging ? 'ew-resize' : 'pointer'; return; }
      const s = at(e);
      canvas.style.cursor = near(s, clip.start) || near(s, clip.end) ? 'ew-resize' : 'pointer';
    });
    const move = (e: PointerEvent) => {
      if (!dragging) return;
      const s = Math.max(0, Math.min(clip.frames, at(e)));
      if (dragging === 'start') clip.start = Math.min(s, clip.end - 1); else clip.end = Math.max(s, clip.start + 1);
      draw(); syncInputs();
    };
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', async () => {
      if (!dragging) return;
      const which = dragging; dragging = null;
      const buffer = await bufferOf(clip);
      const snapped = snap(buffer, which === 'start' ? clip.start : clip.end, clip.rate);
      if (which === 'start' && snapped < clip.end) clip.start = snapped;
      if (which === 'end' && snapped > clip.start) clip.end = snapped;
      draw(); syncInputs(); edited();
    });

    const seconds = (samples: number) => (samples / clip.rate).toFixed(3);
    const startInput = el('input', { type: 'number', step: '0.001', min: '0' }) as HTMLInputElement;
    const endInput = el('input', { type: 'number', step: '0.001', min: '0' }) as HTMLInputElement;
    const syncInputs = () => { startInput.value = seconds(clip.start); endInput.value = seconds(clip.end); };
    const fromInput = () => {
      const s = Math.round(Number(startInput.value) * clip.rate), e = Math.round(Number(endInput.value) * clip.rate);
      if (Number.isFinite(s) && Number.isFinite(e) && s >= 0 && e <= clip.frames && s < e) { clip.start = s; clip.end = e; edited(); }
      syncInputs(); draw();
    };
    startInput.addEventListener('change', fromInput);
    endInput.addEventListener('change', fromInput);
    syncInputs();

    const range = el('div', { class: 'row' },
      el('label', { class: 'field' }, voice ? text.trimStart : text.loopStart, startInput),
      el('label', { class: 'field' }, voice ? text.trimEnd : text.loopEnd, endInput),
      el('span', { class: 'hint' }, voice ? text.trimHint : text.loopHint));
    range.hidden = !ranged();
    const seamButton = el('button', { type: 'button', class: 'mini', disabled: clip.loop === 'once', onclick: () => play(clip, 'seam') }, text.seam) as HTMLButtonElement;
    const modes = voice ? null : el('div', { class: 'row' }, el('strong', {}, text.loop),
      ...(['whole', 'range', 'once'] as LoopMode[]).map((m) => el('label', { class: 'radio' },
        el('input', { type: 'radio', name: `loop-${clip.key}`, checked: clip.loop === m, onchange: () => {
          clip.loop = m; range.hidden = !ranged(); seamButton.disabled = m === 'once'; stop(); draw(); options.edited?.();
        } }), text[m === 'whole' ? 'loopWhole' : m === 'range' ? 'loopRange' : 'loopOnce'])));

    const others = options.others ?? [];
    const average = others.length ? others.reduce((a, l) => a + l, 0) / others.length : null;
    const levelText = () => `${text.loudness} ${((clip.level ?? -90) + clip.gain).toFixed(1)} dBFS` + (average === null ? '' : ` · ${text.loudnessOthers} ${average.toFixed(1)} dBFS`);
    const level = el('span', { class: 'hint' }, levelText());
    const gain = el('input', { type: 'number', step: '0.5', min: '-24', max: '12', value: String(clip.gain), onchange: (e: Event) => {
      const v = Number((e.target as HTMLInputElement).value);
      clip.gain = Number.isFinite(v) ? Math.max(-24, Math.min(12, v)) : 0;
      (e.target as HTMLInputElement).value = String(clip.gain);
      level.textContent = levelText();
      edited();
    } });

    const box = el('div', { class: 'editor' },
      canvas, modes, range,
      el('div', { class: 'row' },
        el('button', { type: 'button', class: 'mini', onclick: () => play(clip) }, text.play), voice ? null : seamButton,
        el('button', { type: 'button', class: 'mini', onclick: stop }, text.stop),
        el('label', { class: 'field' }, text.gain, gain), level),
      el('p', { class: 'hint' }, text.seekHint),
      ...(options.extra ?? []));
    requestAnimationFrame(draw);
    new ResizeObserver(draw).observe(canvas);
    return box;
  }

  return { audio, decode, measure, snap, load, bake, forget, play, stop, playing, position, editor,
    // Called with the clip that plays now (or null) and the one that played before.
    watch: (f: (clip: Clip | null, was: Clip | null) => void) => watchers.add(f),
    // Called every frame while something plays (a playhead).
    onFrame: (f: () => void) => frames.add(f) };
}

// Battle quote records a voice may have (src/host/music.hpp first_quote, last_quote), and the
// record a voice file is named after: "7018.ogg", "7018_attack.wav", "voice 7018.mp3" → 7018,
// the first run of four or five digits.
export const FIRST_RECORD = 5799, LAST_RECORD = 17346;
export const recordOfFile = (name: string) => { const m = /^\D*?(\d{4,5})(?!\d)/.exec(name); return m ? Number(m[1]) : null; };

// A clip for a file the player gave, not loaded yet (audioKit load).
export function newClip(key: string, kind: ClipKind, source: Blob, fileName: string, settings: Partial<Clip> = {}): Clip {
  return { key, kind, fileName, source, passthrough: false, rate: 44100, frames: 0, loop: 'whole', start: 0, end: 0, gain: 0, state: 'decoding', ...settings };
}

function make<K extends keyof HTMLElementTagNameMap>(tag: K, props: Record<string, any> = {}, ...kids: (Node | string | null)[]) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k in e) (e as any)[k] = v;
    else e.setAttribute(k, v);
  }
  for (const k of kids) if (k !== null) e.append(k);
  return e;
}
