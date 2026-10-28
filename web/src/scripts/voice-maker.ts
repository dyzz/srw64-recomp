// The battle voice MOD maker (pages/[lang]/mods/voices.astro, docs/design/audio-mods.md §8).
// A package is mod.json, audio/voice/battle/<record>.ogg and audio/voice.json
// (docs/guide/audio-mods/README.md). Files are decoded by the browser, encoded to Ogg Vorbis
// in a worker unless they already are Vorbis, and zipped here; nothing leaves the machine.
import { vorbisEncoder, vorbisInfo } from './audio-tools';
import { el, fill, jsonBytes, openPackage, packageForm, readJson, say, writePackage, type Text } from './mod-package';
import { picker, type Pickable } from './picker';

type Lang = 'zh' | 'en' | 'ja';
type Line = [number, number, number, string, number];   // record, speaker, label, text, never shown
// One voiced line: the file that goes into the package once it is ready.
type Voice = { fileName: string; ogg?: Uint8Array; seconds: number; state: 'waiting' | 'decoding' | 'encoding' | 'ready' | 'error'; progress?: number };

const FIRST = 5799, LAST = 17346;      // battle quote records (src/host/music.hpp first_quote, last_quote)
const RATE = 44100;
const QUALITY = 4;                     // libvorbis quality, about 64 kbps for a mono voice
const LONG = 4;                        // seconds: past this the next line cuts it off

const size = (n: number) => n > 1 << 20 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.ceil(n / 1024))} KB`;
// An Ogg file's length from its last page's granule position (samples at its own rate).
function oggSeconds(b: Uint8Array, rate: number) {
  for (let i = b.length - 27; i >= 0; i--) {
    if (b[i] !== 0x4f || b[i + 1] !== 0x67 || b[i + 2] !== 0x67 || b[i + 3] !== 0x53) continue;
    const v = new DataView(b.buffer, b.byteOffset + i + 6, 8);
    return (v.getUint32(0, true) + v.getUint32(4, true) * 2 ** 32) / rate;
  }
  return 0;
}
// "7018.ogg", "7018_attack.wav", "voice 7018.mp3" → 7018: the first run of digits.
const recordOf = (name: string) => { const m = /^\D*?(\d{4,5})(?!\d)/.exec(name); return m ? Number(m[1]) : null; };

export function initVoiceMaker() {
  const root = document.querySelector<HTMLElement>('[data-voices]');
  const payload = document.getElementById('voices-data');
  if (!root || !payload) return;
  const { text, labels, lines, ja, counts, pick, series, locale, lang } = JSON.parse(payload.textContent!) as {
    text: Text; labels: string[]; lines: Line[]; ja: string[]; counts: Record<string, number>;
    pick: Pickable[]; series: { id: number | null; name: string }[]; locale: string; lang: Lang;
  };
  const form = packageForm(root, text, locale, text.game);
  const byRecord = new Map(lines.map((l, i) => [l[0], i]));
  const nameOf = new Map(pick.map((p) => [p.id, p.name[lang] || p.name.ja]));
  const voices = new Map<number, Voice>();
  // What an opened package held besides the voices, kept so exporting it again loses nothing.
  let keptFiles: Record<string, Uint8Array> = {};
  let speaker: number | null = null;

  // --- Audio --------------------------------------------------------------------------
  let ctx: AudioContext | null = null;
  const audio = () => (ctx ??= new AudioContext({ sampleRate: RATE }));
  const encode = vorbisEncoder();
  let playing: { record: number; source: AudioBufferSourceNode } | null = null;
  function stop() {
    const was = playing?.record;
    try { playing?.source.stop(); } catch {}
    playing = null;
    if (was !== undefined) renderRow(was);
  }
  async function play(record: number) {
    const v = voices.get(record);
    if (!v?.ogg) return;
    stop();
    const c = audio();
    await c.resume();
    const source = c.createBufferSource();
    source.buffer = await c.decodeAudioData(v.ogg.slice().buffer);
    const gain = c.createGain();
    gain.gain.value = 10 ** (Number(gainInput.value || 0) / 20);
    source.connect(gain).connect(c.destination);
    source.onended = () => { if (playing?.source === source) stop(); };
    source.start();
    playing = { record, source };
    renderRow(record);
  }

  // One file at a time, so a hundred dropped at once are not all decoded together.
  let chain = Promise.resolve();
  function add(record: number, file: File) {
    const v: Voice = { fileName: file.name, seconds: 0, state: 'waiting' };
    voices.set(record, v);
    renderRow(record); later();
    chain = chain.then(() => (voices.get(record) === v ? convert(record, file, v) : undefined));
  }
  async function convert(record: number, file: File, v: Voice) {
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      const vorbis = vorbisInfo(bytes.subarray(0, 65536));
      if (vorbis) {
        v.ogg = bytes; v.seconds = oggSeconds(bytes, vorbis.rate);
      } else {
        v.state = 'decoding'; renderRow(record);
        const buffer = await audio().decodeAudioData(bytes.slice().buffer);
        v.seconds = buffer.duration;
        v.state = 'encoding'; renderRow(record);
        const ogg = await encode(buffer, QUALITY, (p) => { v.progress = p; renderRow(record); });
        if (voices.get(record) !== v) return;
        v.ogg = ogg;
      }
      v.state = 'ready';
    } catch {
      if (voices.get(record) === v) v.state = 'error';
    }
    renderRow(record); later();
  }
  function remove(record: number) {
    if (playing?.record === record) stop();
    voices.delete(record);
    renderRow(record); later();
  }

  // --- Settings -----------------------------------------------------------------------
  const gainInput = root.querySelector<HTMLInputElement>('[data-gain]')!;
  const delayInput = root.querySelector<HTMLInputElement>('[data-delay]')!;

  // --- Many files at once -------------------------------------------------------------
  const bulk = root.querySelector<HTMLElement>('[data-bulk]')!;
  const bulkStatus = root.querySelector<HTMLElement>('[data-bulk-status]')!;
  function addMany(files: File[]) {
    let added = 0;
    const skipped: string[] = [];
    for (const f of files) {
      const r = recordOf(f.name);
      if (r === null || r < FIRST || r > LAST || !byRecord.has(r)) { skipped.push(f.name); continue; }
      add(r, f); added++;
    }
    const parts = [fill(text.bulkDone, { n: added })];
    if (skipped.length) parts.push(fill(text.bulkSkipped, { n: skipped.length, names: skipped.slice(0, 5).join('、') + (skipped.length > 5 ? '…' : '') }));
    say(bulkStatus, parts.join(' '), skipped.length > 0 && added === 0);
  }
  root.querySelector<HTMLInputElement>('[data-bulk-input]')!.addEventListener('change', (e) => {
    const input = e.target as HTMLInputElement;
    addMany([...(input.files ?? [])]); input.value = '';
  });
  bulk.addEventListener('dragover', (e) => { e.preventDefault(); bulk.classList.add('over'); });
  bulk.addEventListener('dragleave', () => bulk.classList.remove('over'));
  bulk.addEventListener('drop', (e) => { e.preventDefault(); bulk.classList.remove('over'); addMany([...(e.dataTransfer?.files ?? [])]); });

  // --- Speakers and their lines -------------------------------------------------------
  const voicedOf = (id: number) => { let n = 0; for (const r of voices.keys()) if (lines[byRecord.get(r)!][1] === id) n++; return n; };
  const speakers = picker(pick, series, lang, { allSeries: text.allSeries, search: text.search, none: text.none },
    (id) => { speaker = id; renderLines(); }, { badge: (id) => { const n = voicedOf(id); return n ? `${n}/${counts[id]}` : String(counts[id]); } });
  root.querySelector<HTMLElement>('[data-speaker-pick]')!.replaceChildren(speakers.node);
  const refreshBadges = () => { speakers.refresh(); speakers.choose(speaker); };
  // The badges and the summary, once however many files change at a time (a timer, not a
  // frame: frames stop while the tab is hidden, and conversions go on).
  let pending = false;
  function later() {
    if (pending) return;
    pending = true;
    setTimeout(() => { pending = false; refreshBadges(); renderVoiced(); }, 50);
  }

  const never = root.querySelector<HTMLInputElement>('[data-never]')!;
  never.addEventListener('change', () => renderLines());
  const box = root.querySelector<HTMLElement>('[data-lines]')!;

  function renderLines() {
    stop();
    if (speaker === null) { box.replaceChildren(el('p', { class: 'hint' }, text.pickFirst)); return; }
    const all = lines.filter((l) => l[1] === speaker);
    const shown = all.filter((l) => never.checked || !l[4] || voices.has(l[0]));
    box.replaceChildren(
      el('p', { class: 'hint' }, fill(text.linesOf, { who: nameOf.get(speaker) ?? String(speaker), n: shown.length }),
        all.length > shown.length ? ` · ${fill(text.hidden, { n: all.length - shown.length })}` : ''),
      el('ol', { class: 'voice-lines' }, ...shown.map((l) => el('li', { class: 'voice-line', id: `line-${l[0]}` }))));
    for (const l of shown) renderRow(l[0]);
  }

  function renderRow(record: number) {
    const li = document.getElementById(`line-${record}`);
    if (!li) return;
    const l = lines[byRecord.get(record)!], v = voices.get(record), i = byRecord.get(record)!;
    li.classList.toggle('done', v?.state === 'ready');
    li.classList.toggle('never', !!l[4]);
    const file = el('input', { type: 'file', accept: 'audio/*,.ogg,.flac,.m4a', hidden: true }) as HTMLInputElement;
    file.addEventListener('change', () => { const f = file.files?.[0]; if (f) add(record, f); });
    const on = playing?.record === record;
    const state = !v ? null
      : v.state === 'waiting' ? text.waiting
      : v.state === 'decoding' ? text.decoding
      : v.state === 'encoding' ? `${text.encoding} ${Math.round((v.progress ?? 0) * 100)}%`
      : v.state === 'error' ? `${v.fileName}: ${text.failed}`
      : `${v.fileName} · ${v.seconds.toFixed(1)}s · ${size(v.ogg!.length)}${v.seconds > LONG ? ` · ${text.long}` : ''}`;
    li.replaceChildren(
      el('span', { class: 'voice-no' }, String(record)),
      el('div', { class: 'voice-body' },
        el('span', { class: 'voice-scene' }, labels[l[2]]),
        el('span', { class: 'voice-text' }, l[3]),
        ja[i] && ja[i] !== l[3] ? el('span', { class: 'voice-ja', lang: 'ja' }, ja[i]) : null,
        state ? el('span', { class: `voice-state${v!.state === 'ready' ? (v!.seconds > LONG ? ' warn' : ' ok') : v!.state === 'error' ? ' err' : ''}` }, state) : null),
      el('div', { class: 'voice-actions' },
        v?.state === 'ready' ? el('button', { type: 'button', class: `mini play${on ? ' on' : ''}`, title: on ? text.stop : text.play,
          onclick: () => (on ? stop() : play(record)) }, on ? '■' : '▶') : null,
        el('label', { class: 'mini' }, text.choose, file),
        v ? el('button', { type: 'button', class: 'mini', onclick: () => remove(record) }, text.remove) : null));
    if (!li.dataset.drop) {
      li.dataset.drop = '1';
      li.addEventListener('dragover', (e) => { e.preventDefault(); li.classList.add('over'); });
      li.addEventListener('dragleave', () => li.classList.remove('over'));
      li.addEventListener('drop', (e) => { e.preventDefault(); li.classList.remove('over'); const f = e.dataTransfer?.files[0]; if (f) add(record, f); });
    }
  }

  // --- What is voiced -----------------------------------------------------------------
  const voicedBox = root.querySelector<HTMLElement>('[data-voiced]')!;
  function renderVoiced() {
    if (!voices.size) { voicedBox.replaceChildren(el('p', { class: 'hint' }, text.voicedNone)); return; }
    const by = new Map<number, number>();
    let bytes = 0;
    for (const [r, v] of voices) { const s = lines[byRecord.get(r)!][1]; by.set(s, (by.get(s) ?? 0) + 1); bytes += v.ogg?.length ?? 0; }
    voicedBox.replaceChildren(
      el('p', { class: 'hint' }, fill(text.voicedSum, { n: voices.size, who: by.size, size: size(bytes) })),
      el('div', { class: 'voice-who' }, ...[...by].sort((a, b) => b[1] - a[1]).map(([id, n]) =>
        el('button', { type: 'button', class: 'mini', onclick: () => { speaker = id; speakers.choose(id); renderLines(); box.scrollIntoView({ behavior: 'smooth' }); } },
          `${nameOf.get(id) ?? id} ${n}/${counts[id]}`))));
  }

  // --- Opening and exporting a package ------------------------------------------------
  const importStatus = root.querySelector<HTMLElement>('[data-import-status]')!;
  root.querySelector<HTMLInputElement>('[data-import]')!.addEventListener('change', async (e) => {
    const input = e.target as HTMLInputElement, f = input.files?.[0];
    input.value = '';
    if (!f) return;
    try {
      const { mod, files } = await openPackage(f);
      stop(); voices.clear(); keptFiles = {};
      form.fill(mod);
      const settings = readJson(files['audio/voice.json']) ?? {};
      gainInput.value = String(typeof settings.gain === 'number' ? settings.gain : 0);
      delayInput.value = String(Number.isInteger(settings.delay) ? settings.delay : 0);
      for (const [path, bytes] of Object.entries(files)) {
        if (path === 'mod.json' || path === 'audio/voice.json') continue;
        const m = /^audio\/voice\/battle\/(\d+)\.ogg$/.exec(path);
        const vorbis = m ? vorbisInfo(bytes.subarray(0, 65536)) : null;
        if (m && vorbis && byRecord.has(Number(m[1]))) voices.set(Number(m[1]), { fileName: `${m[1]}.ogg`, ogg: bytes, seconds: oggSeconds(bytes, vorbis.rate), state: 'ready' });
        else keptFiles[path] = bytes;
      }
      say(importStatus, fill(text.imported, { id: mod.id ?? f.name }));
      renderLines(); later();
    } catch (err) {
      say(importStatus, fill(text.errImport, { detail: (err as Error).message }), true);
    }
  });

  const exportStatus = root.querySelector<HTMLElement>('[data-export-status]')!;
  root.querySelector<HTMLButtonElement>('[data-export]')!.addEventListener('click', () => {
    const m = form.manifest();
    if (typeof m === 'string') return say(exportStatus, m, true);
    const busy = [...voices.values()].filter((v) => v.state !== 'ready' && v.state !== 'error').length;
    if (busy) return say(exportStatus, fill(text.errBusy, { n: busy }), true);
    const ready = [...voices].filter(([, v]) => v.state === 'ready' && v.ogg);
    if (!ready.length) return say(exportStatus, text.errEmpty, true);
    const gain = Number(gainInput.value || 0), delay = Number(delayInput.value || 0);
    if (!Number.isFinite(gain) || gain < -30 || gain > 12) return say(exportStatus, text.errGain, true);
    if (!Number.isInteger(delay) || delay < 0 || delay > 2000) return say(exportStatus, text.errDelay, true);
    const files: Record<string, Uint8Array> = { ...keptFiles };
    for (const [r, v] of ready) files[`audio/voice/battle/${r}.ogg`] = v.ogg!;
    if (gain || delay) files['audio/voice.json'] = jsonBytes({ schema: 'srw64.voice.v1', ...(gain ? { gain } : {}), ...(delay ? { delay } : {}) });
    writePackage(m.manifest, m.version, files, exportStatus, text);
  });

  renderLines();
  renderVoiced();
}
