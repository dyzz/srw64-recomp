// The BGM MOD maker (pages/[lang]/mods/bgm.astro, docs/design/audio-mods.md §9). A package is
// mod.json, audio/music.json and audio/music/<song>.ogg (docs/guide/audio-mods/README.md).
// Files are decoded by the browser, encoded to Ogg Vorbis in a worker unless they already
// are Vorbis, and zipped here; nothing leaves the player's machine.
import { strFromU8, strToU8, unzipSync, zipSync, type Zippable } from 'fflate';

type Lang = 'zh' | 'en' | 'ja';
type Names = { ja: string; zh: string; en: string };
type SongInfo = { song: number; row: number; title: string; karaoke: boolean; fixed: string | null; stageMaps: number; pilots: number[]; units: number[] };
type Named = { id: number; name: Names; song: number };
type Data = { songs: SongInfo[]; pilots: Named[]; units: Named[] };
type Text = Record<string, any>;
type LoopMode = 'whole' | 'range' | 'once';
type Titles = { 'zh-Hans': string; en: string; ja: string };

// One replaced song. Loop points are samples at the Ogg file's own rate, as music.json wants.
type Track = {
  song: number;
  fileName: string;
  source: Blob;               // what the player gave (or the packaged .ogg)
  ogg?: Uint8Array;           // the file that goes into the package
  passthrough: boolean;       // already Vorbis: packaged as it is
  rate: number;
  frames: number;
  loop: LoopMode;
  start: number;
  end: number;
  gain: number;
  titles: Titles;
  peaks?: Float32Array;       // min/max pairs for the waveform
  level?: number;             // average level, dBFS
  state: 'decoding' | 'encoding' | 'ready' | 'error';
  progress?: number;
};

const JINGLE = 0x26;          // plays once unless music.json loops it (music.hpp jingle_song)
const RATE = 44100;           // the rate the page decodes and encodes at
const QUALITY = 6;            // libvorbis quality, about 190 kbps for stereo
const PEAKS = 1600;
const LICENSES = ['CC-BY-4.0', 'CC-BY-SA-4.0', 'CC-BY-NC-4.0', 'CC-BY-NC-SA-4.0', 'CC0-1.0', 'MIT', 'GPL-3.0-or-later'];

export function initBgmMaker() {
  const root = document.querySelector<HTMLElement>('[data-bgm]');
  const payload = document.getElementById('bgm-data');
  if (!root || !payload) return;
  // lang: the page's text language (zh on the Traditional page too); locale: the MOD text the
  // description is written in (zh-Hant there).
  const { data, text, lang, locale } = JSON.parse(payload.textContent!) as { data: Data; text: Text; lang: Lang; locale: string };
  const fill = (s: string, v: Record<string, string | number>) => s.replace(/\{(\w+)\}/g, (_, k) => String(v[k] ?? ''));
  const nameOf = (n: Names) => n[lang] || n.ja;
  const songInfo = new Map(data.songs.map((s) => [s.song, s]));
  const pilotName = new Map(data.pilots.map((p) => [p.id, nameOf(p.name)]));
  const tracks = new Map<number, Track>();
  const battle = { pilot: new Map<number, number>(), unit: new Map<number, number>() };
  // What an opened package held besides the music, kept so exporting it again loses nothing.
  let keptManifest: Record<string, unknown> = {};
  let keptFiles: Record<string, Uint8Array> = {};
  let keptTracks: Record<string, unknown> = {};
  let open: number | null = null;
  let preview: { song: number; buffer: AudioBuffer } | null = null;

  // --- Audio --------------------------------------------------------------------------
  let ctx: AudioContext | null = null;
  const audio = () => (ctx ??= new AudioContext({ sampleRate: RATE }));
  let playing: AudioBufferSourceNode | null = null;
  // What plays, to draw its playhead and mark its row: the song, where in it the source
  // started (seconds) and when (context time), and the loop it runs.
  let now: { song: number; offset: number; at: number; start: number; end: number; loop: boolean; duration: number } | null = null;
  const stop = () => {
    try { playing?.stop(); } catch {}
    playing = null;
    const was = now?.song; now = null;
    if (was !== undefined) markPlaying(was);
  };
  // The position now, in seconds, following the loop once the source has passed its end.
  function position() {
    if (!now || !ctx) return null;
    let p = now.offset + (ctx.currentTime - now.at);
    if (now.loop && p >= now.end) p = now.start + ((p - now.start) % Math.max(0.001, now.end - now.start));
    return Math.min(p, now.duration);
  }

  async function decode(blob: Blob): Promise<AudioBuffer> {
    return audio().decodeAudioData(await blob.arrayBuffer());
  }

  // Ogg Vorbis files go into the package as they are: no second lossy pass.
  function vorbisInfo(b: Uint8Array): { rate: number; channels: number; loop?: [number, number] } | null {
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

  // One worker, one song at a time: a song's PCM is the largest thing the page holds.
  const worker = new Worker(new URL('./vorbis-worker.ts', import.meta.url), { type: 'module' });
  let queue = Promise.resolve();
  let jobId = 0;
  function encode(track: Track, buffer: AudioBuffer): Promise<Uint8Array> {
    const run = () => new Promise<Uint8Array>((resolve, reject) => {
      const id = ++jobId;
      const channels = [...Array(Math.min(2, buffer.numberOfChannels))].map((_, c) => buffer.getChannelData(c).slice());
      worker.onmessage = ({ data: m }) => {
        if (m.id !== id) return;
        if (m.progress !== undefined) { track.progress = m.progress; renderState(track.song); return; }
        if (m.error) reject(new Error(m.error)); else resolve(m.ogg);
      };
      worker.postMessage({ id, channels, sampleRate: buffer.sampleRate, quality: QUALITY }, channels.map((c) => c.buffer));
    });
    const job = queue.then(run, run);
    queue = job.then(() => {}, () => {});
    return job;
  }

  async function addFile(song: number, blob: Blob, fileName: string, settings?: Partial<Track>) {
    stop();
    const head = new Uint8Array(await blob.slice(0, 65536).arrayBuffer());
    const vorbis = vorbisInfo(head);
    const track: Track = {
      song, fileName, source: blob, passthrough: !!vorbis, rate: vorbis?.rate ?? RATE, frames: 0,
      loop: song === JINGLE ? 'once' : 'whole', start: 0, end: 0, gain: 0, titles: { 'zh-Hans': '', en: '', ja: '' },
      state: 'decoding', ...settings,
    };
    if (vorbis?.loop && !settings?.loop) { track.loop = 'range'; [track.start, track.end] = vorbis.loop; }
    tracks.set(song, track);
    renderSong(song);
    try {
      const buffer = await decode(blob);
      if (tracks.get(song) !== track) return;
      Object.assign(track, measure(buffer));
      track.frames = track.passthrough ? Math.round(buffer.duration * track.rate) : buffer.length;
      if (!track.end || track.end > track.frames) track.end = track.frames;
      if (track.start >= track.end) track.start = 0;
      preview = { song, buffer };
      if (track.passthrough) {
        track.ogg = new Uint8Array(await blob.arrayBuffer());
      } else {
        track.state = 'encoding'; renderSong(song);
        track.ogg = await encode(track, buffer);
        if (tracks.get(song) !== track) return;
      }
      track.state = 'ready';
    } catch {
      track.state = 'error';
    }
    renderSong(song);
  }

  async function previewBuffer(track: Track) {
    if (preview?.song === track.song) return preview.buffer;
    preview = { song: track.song, buffer: await decode(track.ogg ? new Blob([track.ogg as BlobPart]) : track.source) };
    return preview.buffer;
  }

  // From `from` seconds, or 3 seconds before the loop's end to hear the seam.
  async function play(track: Track, from: number | 'seam' = 0) {
    stop();
    const buffer = await previewBuffer(track);
    const c = audio();
    await c.resume();
    const source = c.createBufferSource();
    source.buffer = buffer;
    const gain = c.createGain();
    gain.gain.value = 10 ** (track.gain / 20);
    source.connect(gain).connect(c.destination);
    const start = track.loop === 'range' ? track.start / track.rate : 0;
    const end = track.loop === 'range' ? track.end / track.rate : buffer.duration;
    const loop = track.loop !== 'once';
    if (loop) { source.loop = true; source.loopStart = start; source.loopEnd = end; }
    const offset = from === 'seam' ? Math.max(start, end - 3) : Math.max(0, Math.min(from, buffer.duration - 0.01));
    source.start(0, offset);
    source.onended = () => { if (playing === source) stop(); };
    playing = source;
    now = { song: track.song, offset, at: c.currentTime, start, end, loop, duration: buffer.duration };
    markPlaying(track.song);
    tick();
  }
  // Each row's play button and the open editor's playhead follow what plays.
  function markPlaying(song: number) {
    const b = document.querySelector<HTMLButtonElement>(`#song-${song} [data-play]`);
    if (b) { const on = now?.song === song; b.textContent = on ? '■' : '▶'; b.title = on ? text.stop : text.play; b.classList.toggle('on', on); }
  }
  let redraw: (() => void) | null = null;
  function tick() { if (!now) { redraw?.(); return; } redraw?.(); requestAnimationFrame(tick); }

  // --- Song list ----------------------------------------------------------------------
  const list = root.querySelector<HTMLOListElement>('[data-songs]')!;
  function uses(s: SongInfo) {
    const out: string[] = [];
    if (s.fixed) out.push(text.usesFixed[s.fixed]);
    if (s.stageMaps) out.push(fill(text.usesStage, { n: s.stageMaps }));
    if (s.pilots.length) {
      const names = [...new Set(s.pilots.map((p) => pilotName.get(p)).filter(Boolean))];
      out.push(fill(text.usesPilots, { names: names.slice(0, 4).join('、') + (names.length > 4 ? fill(text.usesMore, { n: names.length - 4 }) : '') }));
    }
    if (s.units.length) out.push(fill(text.usesUnits, { n: s.units.length }));
    return out.length ? out.join(' · ') : text.usesNone;
  }

  function el<K extends keyof HTMLElementTagNameMap>(tag: K, props: Record<string, any> = {}, ...kids: (Node | string | null)[]) {
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

  for (const s of data.songs) {
    const li = el('li', { class: 'song', id: `song-${s.song}` });
    li.dataset.song = String(s.song);
    li.addEventListener('dragover', (e) => { e.preventDefault(); li.classList.add('over'); });
    li.addEventListener('dragleave', () => li.classList.remove('over'));
    li.addEventListener('drop', (e) => {
      e.preventDefault(); li.classList.remove('over');
      const f = e.dataTransfer?.files[0];
      if (f) { open = s.song; addFile(s.song, f, f.name); }
    });
    list.append(li);
    renderSong(s.song);
  }

  function renderState(song: number) {
    const t = tracks.get(song), line = document.querySelector(`#song-${song} .song-state`);
    if (!line) return;
    line.className = 'song-state' + (t?.state === 'ready' ? ' ok' : t?.state === 'error' ? ' err' : '');
    line.textContent = !t ? text.drop
      : t.state === 'decoding' ? text.decoding
      : t.state === 'encoding' ? `${text.encoding} ${Math.round((t.progress ?? 0) * 100)}%`
      : t.state === 'error' ? `${t.fileName}: ${text.failed}`
      : `${t.passthrough ? text.kept : text.ready} · ${t.fileName} · ${(t.frames / t.rate).toFixed(1)}s`;
  }

  function renderSong(song: number) {
    const s = songInfo.get(song)!, t = tracks.get(song), li = document.getElementById(`song-${song}`)!;
    li.classList.toggle('done', t?.state === 'ready');
    const file = el('input', { type: 'file', accept: 'audio/*,.ogg,.flac,.m4a', hidden: true, onchange: (e: Event) => {
      const f = (e.target as HTMLInputElement).files?.[0];
      if (f) { open = song; addFile(song, f, f.name); }
    } });
    const playButton = t?.state === 'ready' ? el('button', { type: 'button', class: 'mini play', 'data-play': '', title: text.play,
      onclick: () => { if (now?.song === song) stop(); else if (t) play(t); } }, '▶') : null;
    const actions = el('div', { class: 'song-actions' },
      playButton,
      el('label', { class: 'mini' }, text.choose, file),
      t?.state === 'ready' ? el('button', { type: 'button', class: 'mini', onclick: () => { open = open === song ? null : song; rerenderOpen(song); } }, open === song ? text.close : text.edit) : null,
      t ? el('button', { type: 'button', class: 'mini', onclick: () => { stop(); tracks.delete(song); if (open === song) open = null; if (preview?.song === song) preview = null; renderSong(song); } }, text.remove) : null);
    li.replaceChildren(el('div', { class: 'song-head' },
      el('span', { class: 'song-no' }, String(s.row)),
      el('span', { class: 'song-title' }, s.title),
      actions,
      el('span', { class: 'song-uses' }, uses(s) + (s.karaoke ? ` · ${text.karaoke}` : '')),
      el('span', { class: 'song-state' })));
    renderState(song);
    markPlaying(song);
    if (t?.state === 'ready' && open === song) li.append(editor(t));
  }
  function rerenderOpen(song: number) {
    for (const k of new Set([song, ...tracks.keys()])) renderSong(k);
  }

  // --- Editor -------------------------------------------------------------------------
  function editor(t: Track) {
    const canvas = el('canvas', { class: 'wave' }) as HTMLCanvasElement;
    const css = getComputedStyle(root!);
    const color = (v: string) => css.getPropertyValue(v).trim();
    const draw = () => {
      const w = (canvas.width = canvas.clientWidth * devicePixelRatio), h = (canvas.height = canvas.clientHeight * devicePixelRatio);
      const g = canvas.getContext('2d')!;
      g.clearRect(0, 0, w, h);
      const x = (samples: number) => (samples / Math.max(1, t.frames)) * w;
      if (t.loop === 'range') { g.fillStyle = color('--cyan-soft'); g.fillRect(x(t.start), 0, x(t.end) - x(t.start), h); }
      g.fillStyle = color('--muted');
      const p = t.peaks!;
      for (let i = 0; i < w; i++) {
        const k = Math.min(PEAKS - 1, Math.floor((i / w) * PEAKS));
        const lo = p[2 * k], hi = p[2 * k + 1];
        g.fillRect(i, ((1 - hi) / 2) * h, 1, Math.max(1, ((hi - lo) / 2) * h));
      }
      if (t.loop === 'range') {
        g.fillStyle = color('--cyan'); g.fillRect(x(t.start) - devicePixelRatio, 0, 2 * devicePixelRatio, h);
        g.fillStyle = color('--gold'); g.fillRect(x(t.end) - devicePixelRatio, 0, 2 * devicePixelRatio, h);
      }
      const head = now?.song === t.song ? position() : null;
      if (head !== null) { g.fillStyle = color('--text'); g.fillRect(x(head * t.rate) - devicePixelRatio / 2, 0, devicePixelRatio, h); }
    };
    redraw = draw;
    // Dragging a loop line; on release it snaps to the nearest zero crossing.
    let dragging: 'start' | 'end' | null = null;
    const at = (e: PointerEvent) => Math.round(((e.clientX - canvas.getBoundingClientRect().left) / canvas.clientWidth) * t.frames);
    // Near a loop line (within 8 pixels) a press moves it; anywhere else it plays from there.
    canvas.addEventListener('pointerdown', (e) => {
      const s = at(e), near = (samples: number) => Math.abs(s - samples) / t.frames * canvas.clientWidth <= 8;
      if (t.loop === 'range' && (near(t.start) || near(t.end))) {
        dragging = Math.abs(s - t.start) <= Math.abs(s - t.end) ? 'start' : 'end';
        canvas.setPointerCapture(e.pointerId);
        move(e);
        return;
      }
      play(t, Math.max(0, s) / t.rate);
    });
    canvas.addEventListener('pointermove', (e) => {
      if (dragging || t.loop !== 'range') { canvas.style.cursor = dragging ? 'ew-resize' : 'pointer'; return; }
      const s = at(e), near = (samples: number) => Math.abs(s - samples) / t.frames * canvas.clientWidth <= 8;
      canvas.style.cursor = near(t.start) || near(t.end) ? 'ew-resize' : 'pointer';
    });
    const move = (e: PointerEvent) => {
      if (!dragging) return;
      const s = Math.max(0, Math.min(t.frames, at(e)));
      if (dragging === 'start') t.start = Math.min(s, t.end - 1); else t.end = Math.max(s, t.start + 1);
      draw(); syncInputs();
    };
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', async () => {
      if (!dragging) return;
      const which = dragging; dragging = null;
      const buffer = await previewBuffer(t);
      const snapped = snap(buffer, which === 'start' ? t.start : t.end, t.rate);
      if (which === 'start' && snapped < t.end) t.start = snapped;
      if (which === 'end' && snapped > t.start) t.end = snapped;
      draw(); syncInputs();
    });

    const seconds = (samples: number) => (samples / t.rate).toFixed(3);
    const startInput = el('input', { type: 'number', step: '0.001', min: '0' }) as HTMLInputElement;
    const endInput = el('input', { type: 'number', step: '0.001', min: '0' }) as HTMLInputElement;
    const syncInputs = () => { startInput.value = seconds(t.start); endInput.value = seconds(t.end); };
    const fromInput = () => {
      const s = Math.round(Number(startInput.value) * t.rate), e = Math.round(Number(endInput.value) * t.rate);
      if (Number.isFinite(s) && Number.isFinite(e) && s >= 0 && e <= t.frames && s < e) { t.start = s; t.end = e; }
      syncInputs(); draw();
    };
    startInput.addEventListener('change', fromInput);
    endInput.addEventListener('change', fromInput);
    syncInputs();

    const range = el('div', { class: 'row' },
      el('label', { class: 'field' }, text.loopStart, startInput),
      el('label', { class: 'field' }, text.loopEnd, endInput),
      el('span', { class: 'hint' }, text.loopHint));
    range.hidden = t.loop !== 'range';
    const seamButton = el('button', { type: 'button', class: 'mini', disabled: t.loop === 'once', onclick: () => play(t, 'seam') }, text.seam) as HTMLButtonElement;
    const modes = el('div', { class: 'row' }, el('strong', {}, text.loop),
      ...(['whole', 'range', 'once'] as LoopMode[]).map((m) => el('label', { class: 'radio' },
        el('input', { type: 'radio', name: `loop-${t.song}`, checked: t.loop === m, onchange: () => {
          t.loop = m; range.hidden = m !== 'range'; seamButton.disabled = m === 'once'; stop(); draw();
        } }), text[m === 'whole' ? 'loopWhole' : m === 'range' ? 'loopRange' : 'loopOnce'])));

    const others = [...tracks.values()].filter((o) => o !== t && o.level !== undefined);
    const average = others.length ? others.reduce((a, o) => a + o.level!, 0) / others.length : null;
    const level = el('span', { class: 'hint' }, `${text.loudness} ${(t.level! + t.gain).toFixed(1)} dBFS` + (average === null ? '' : ` · ${text.loudnessOthers} ${average.toFixed(1)} dBFS`));
    const gain = el('input', { type: 'number', step: '0.5', min: '-24', max: '12', value: String(t.gain), onchange: (e: Event) => {
      const v = Number((e.target as HTMLInputElement).value);
      t.gain = Number.isFinite(v) ? Math.max(-24, Math.min(12, v)) : 0;
      (e.target as HTMLInputElement).value = String(t.gain);
      level.textContent = `${text.loudness} ${(t.level! + t.gain).toFixed(1)} dBFS` + (average === null ? '' : ` · ${text.loudnessOthers} ${average.toFixed(1)} dBFS`);
    } });
    const titleInput = (key: keyof Titles, label: string) => el('label', { class: 'field' }, label,
      el('input', { value: t.titles[key], oninput: (e: Event) => { t.titles[key] = (e.target as HTMLInputElement).value; } }));

    const box = el('div', { class: 'editor' },
      canvas, modes, range,
      el('div', { class: 'row' },
        el('button', { type: 'button', class: 'mini', onclick: () => play(t) }, text.play), seamButton,
        el('button', { type: 'button', class: 'mini', onclick: stop }, text.stop),
        el('label', { class: 'field' }, text.gain, gain), level),
      el('p', { class: 'hint' }, text.seekHint),
      el('div', { class: 'row' }, el('strong', {}, text.titles)),
      el('div', { class: 'row' }, titleInput('zh-Hans', '中文'), titleInput('en', 'English'), titleInput('ja', '日本語')));
    requestAnimationFrame(draw);
    new ResizeObserver(draw).observe(canvas);
    return box;
  }

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

  // --- Battle songs -------------------------------------------------------------------
  const battleBox = root.querySelector<HTMLElement>('[data-battle]')!;
  const songLabel = (n: number) => { const s = songInfo.get(n); return s ? `${s.row}. ${s.title}` : String(n); };
  function renderBattle() {
    battleBox.replaceChildren(...(['pilot', 'unit'] as const).map((kind) => {
      const people = kind === 'pilot' ? data.pilots.filter((p) => p.song > 0) : data.units;
      const who = el('select', {}, ...people.map((p) => el('option', { value: String(p.id) },
        `${nameOf(p.name)}（${text.original}: ${songLabel(p.song)}）`))) as HTMLSelectElement;
      const song = el('select', {}, ...data.songs.map((s) => el('option', { value: String(s.song) }, `${s.row}. ${s.title}`))) as HTMLSelectElement;
      const map = battle[kind];
      const names = new Map(people.map((p) => [p.id, nameOf(p.name)]));
      return el('div', {},
        el('div', { class: 'row' },
          el('label', { class: 'field' }, text[kind], who),
          el('label', { class: 'field' }, text.song, song),
          el('button', { type: 'button', class: 'mini', onclick: () => { map.set(Number(who.value), Number(song.value)); renderBattle(); } }, text.add)),
        el('div', { class: 'battle-list' }, ...[...map].sort((a, b) => a[0] - b[0]).map(([id, n]) =>
          el('div', { class: 'battle-item' }, `${names.get(id) ?? id} → ${songLabel(n)}`,
            el('button', { type: 'button', class: 'mini', onclick: () => { map.delete(id); renderBattle(); } }, text.remove)))));
    }));
  }
  renderBattle();

  // --- Package ------------------------------------------------------------------------
  const field = (k: string) => root.querySelector<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>(`[data-f="${k}"]`)!;
  const validId = (s: string) => s.length > 0 && s.length <= 64 && s !== '.' && s !== '..' && /^[A-Za-z0-9._-]+$/.test(s);
  const parseVersion = (s: string) => {
    const parts = s.split('.');
    if (parts.length < 1 || parts.length > 3 || parts.some((p) => !/^\d{1,6}$/.test(p))) return null;
    return [...parts.map(Number), 0, 0].slice(0, 3).join('.');
  };

  function manifest(): { manifest: Record<string, unknown>; version: string } | string {
    const id = field('id').value.trim(), version = parseVersion(field('version').value.trim());
    if (!validId(id)) return text.errId;
    if (!version) return text.errVersion;
    const name: Record<string, string> = {};
    for (const l of ['zh-Hans', 'en', 'ja']) { const v = field(`name-${l}`).value.trim(); if (v) name[l] = v; }
    if (!Object.keys(name).length) return text.errName;
    const license = field('license').value;
    if (!LICENSES.includes(license)) return text.errLicense;
    const out: Record<string, unknown> = { ...keptManifest, schema: 'srw64.mod.v1', id, version, name, license };
    const authors = field('authors').value.split(/[,，、]/).map((a) => a.trim()).filter(Boolean);
    if (authors.length) out.authors = authors; else delete out.authors;
    const description = field('description').value.trim();
    if (description) out.description = { ...(typeof keptManifest.description === 'object' ? keptManifest.description as object : {}), [locale]: description };
    else delete out.description;
    return { manifest: out, version };
  }

  function musicJson() {
    const out: Record<string, any> = { schema: 'srw64.music.v1' };
    const trackSettings: Record<string, any> = { ...keptTracks };
    for (const t of tracks.values()) {
      const s: Record<string, unknown> = {};
      if (t.loop === 'once') s.loop = false;
      else if (t.loop === 'range' && (t.start > 0 || t.end < t.frames)) s.loop = [t.start, t.end];
      else if (t.song === JINGLE) s.loop = true;
      if (t.gain) s.gain = t.gain;
      const titles = Object.fromEntries(Object.entries(t.titles).filter(([, v]) => v.trim()));
      if (Object.keys(titles).length) s.title = titles;
      if (Object.keys(s).length) trackSettings[t.song] = s; else delete trackSettings[t.song];
    }
    if (Object.keys(trackSettings).length) out.tracks = trackSettings;
    const b: Record<string, Record<string, number>> = {};
    for (const kind of ['pilot', 'unit'] as const) if (battle[kind].size) b[kind] = Object.fromEntries(battle[kind]);
    if (Object.keys(b).length) out.battle = b;
    return out;
  }

  const exportStatus = root.querySelector<HTMLElement>('[data-export-status]')!;
  const say = (where: HTMLElement, message: string, error = false) => { where.textContent = message; where.classList.toggle('err', error); };
  const size = (n: number) => n > 1 << 20 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.ceil(n / 1024)} KB`;

  root.querySelector('[data-export]')!.addEventListener('click', () => {
    const m = manifest();
    if (typeof m === 'string') return say(exportStatus, m, true);
    if ([...tracks.values()].some((t) => t.state === 'decoding' || t.state === 'encoding')) return say(exportStatus, text.errBusy, true);
    const ready = [...tracks.values()].filter((t) => t.state === 'ready' && t.ogg);
    const hasOld = Object.keys(keptFiles).some((p) => p.startsWith('audio/music/'));
    if (!ready.length && !battle.pilot.size && !battle.unit.size && !hasOld) return say(exportStatus, text.errEmpty, true);
    say(exportStatus, text.exporting);
    const files: Zippable = {};
    for (const [path, bytes] of Object.entries(keptFiles)) files[path] = [bytes, { level: 0 }];
    files['mod.json'] = strToU8(JSON.stringify(m.manifest, null, 2) + '\n');
    files['audio/music.json'] = strToU8(JSON.stringify(musicJson(), null, 2) + '\n');
    for (const t of ready) files[`audio/music/${t.song}.ogg`] = [t.ogg!, { level: 0 }];
    const zip = zipSync(files, { level: 6 });
    const name = `${m.manifest.id}-${m.version}.srw64mod`;
    const url = URL.createObjectURL(new Blob([zip as BlobPart], { type: 'application/zip' }));
    const a = el('a', { href: url, download: name });
    document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
    say(exportStatus, fill(text.exported, { file: name, size: size(zip.length) }));
  });

  // Opening a package: the form, its music and settings, and whatever else it holds.
  const importStatus = root.querySelector<HTMLElement>('[data-import-status]')!;
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (e) => {
    const input = e.target as HTMLInputElement, file = input.files?.[0];
    input.value = '';
    if (!file) return;
    try {
      const entries = unzipSync(new Uint8Array(await file.arrayBuffer()));
      const names = Object.keys(entries).filter((n) => !n.endsWith('/') && !n.startsWith('__MACOSX/') && !n.endsWith('.DS_Store'));
      const top = names.includes('mod.json') ? '' : names.find((n) => /^[^/]+\/mod\.json$/.test(n))?.replace('mod.json', '');
      if (top === undefined) throw new Error('mod.json');
      const read = (p: string) => entries[top + p];
      const mod = JSON.parse(strFromU8(read('mod.json')));
      const music = read('audio/music.json') ? JSON.parse(strFromU8(read('audio/music.json'))) : {};
      stop(); tracks.clear(); battle.pilot.clear(); battle.unit.clear(); open = null; preview = null;
      keptManifest = mod; keptFiles = {}; keptTracks = {};
      field('id').value = mod.id ?? '';
      field('version').value = mod.version ?? '1.0.0';
      for (const l of ['zh-Hans', 'en', 'ja']) field(`name-${l}`).value = typeof mod.name === 'string' ? (l === 'en' ? mod.name : '') : mod.name?.[l] ?? '';
      field('authors').value = (mod.authors ?? []).join(', ');
      field('license').value = LICENSES.includes(mod.license) ? mod.license : '';
        field('description').value = typeof mod.description === 'string' ? mod.description : mod.description?.[locale] ?? '';
      for (const kind of ['pilot', 'unit'] as const)
        for (const [k, v] of Object.entries(music.battle?.[kind] ?? {})) if (Number.isInteger(v)) battle[kind].set(Number(k), v as number);
      renderBattle();
      const loaded: Promise<void>[] = [];
      for (const n of names) {
        const rel = n.slice(top.length);
        const m = /^audio\/music\/(\d{1,3})\.ogg$/i.exec(rel);
        const song = m ? Number(m[1]) : NaN;
        if (m && songInfo.has(song)) {
          const s = music.tracks?.[song] ?? {};
          const settings: Partial<Track> = { gain: typeof s.gain === 'number' ? s.gain : 0 };
          if (s.loop === false) settings.loop = 'once';
          else if (s.loop === true) settings.loop = 'whole';
          else if (Array.isArray(s.loop) && s.loop.length === 2) { settings.loop = 'range'; settings.start = s.loop[0]; settings.end = s.loop[1]; }
          const titles = typeof s.title === 'string' ? { en: s.title } : s.title ?? {};
          settings.titles = { 'zh-Hans': titles['zh-Hans'] ?? '', en: titles.en ?? '', ja: titles.ja ?? '' };
          loaded.push(addFile(song, new Blob([entries[n] as BlobPart]), rel.split('/').pop()!, settings));
        } else if (rel !== 'mod.json' && rel !== 'audio/music.json') {
          keptFiles[rel] = entries[n];
        }
      }
      // Settings for songs whose file this package does not carry (a dependency's, say).
      for (const [k, v] of Object.entries(music.tracks ?? {})) if (!names.includes(`${top}audio/music/${k}.ogg`)) keptTracks[k] = v;
      say(importStatus, fill(text.imported, { id: mod.id ?? file.name, n: loaded.length }));
      await Promise.all(loaded);
    } catch (error) {
      say(importStatus, fill(text.errImport, { detail: String((error as Error).message ?? error) }), true);
    }
  });

  addEventListener('beforeunload', (e) => { if (tracks.size) e.preventDefault(); });
}
