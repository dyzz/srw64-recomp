// The BGM MOD maker (pages/[lang]/mods/bgm.astro, docs/design/audio-mods.md §9). A package is
// mod.json, audio/music.json and audio/music/<song>.ogg (docs/guide/audio-mods/README.md).
// Files are decoded by the browser, encoded to Ogg Vorbis in a worker unless they already
// are Vorbis, and zipped here; nothing leaves the player's machine.
import { strFromU8, strToU8, unzipSync, zipSync, type Zippable } from 'fflate';
import { audioKit, newClip, type Clip, type LoopMode } from './audio-clip';

type Lang = 'zh' | 'en' | 'ja' | 'ko';
type Names = { ja: string; zh: string; en: string; ko?: string };
type SongInfo = { song: number; row: number; title: string; karaoke: boolean; fixed: string | null; stageMaps: number; pilots: number[]; units: number[] };
type Named = { id: number; name: Names; song: number };
type Data = { songs: SongInfo[]; pilots: Named[]; units: Named[] };
type Text = Record<string, any>;
type Titles = { 'zh-Hans': string; en: string; ja: string };

// One replaced song (audio-clip.ts): loop points are samples at the Ogg file's own rate, as
// music.json wants.
type Track = Clip & { song: number; titles: Titles };

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
  const nameOf = (n: Names) => n[lang] || (lang === 'ko' ? n.en : '') || n.ja;
  const songInfo = new Map(data.songs.map((s) => [s.song, s]));
  const pilotName = new Map(data.pilots.map((p) => [p.id, nameOf(p.name)]));
  const tracks = new Map<number, Track>();
  const battle = { pilot: new Map<number, number>(), unit: new Map<number, number>() };
  // What an opened package held besides the music, kept so exporting it again loses nothing.
  let keptManifest: Record<string, unknown> = {};
  let keptFiles: Record<string, Uint8Array> = {};
  let keptTracks: Record<string, unknown> = {};
  let keptBattle: Record<string, Record<string, unknown>> = {};
  let open: number | null = null;

  // --- Audio (audio-clip.ts) -----------------------------------------------------------
  const kit = audioKit({ rate: RATE, peaks: PEAKS });
  const stop = kit.stop;
  const play = (t: Track, from: number | 'seam' = 0) => kit.play(t, from);
  kit.watch((clip, was) => { for (const c of [clip, was]) if (c) markPlaying(Number(c.key)); });
  async function addFile(song: number, blob: Blob, fileName: string, settings?: Partial<Track>) {
    stop();
    const track = { ...newClip(String(song), 'music', blob, fileName, { loop: song === JINGLE ? 'once' : 'whole' }),
      song, titles: { 'zh-Hans': '', en: '', ja: '' }, ...settings } as Track;
    tracks.set(song, track);
    renderSong(song);
    await kit.load(track, QUALITY, () => {
      if (tracks.get(song) !== track) return;
      if (track.state === 'encoding' && track.progress !== undefined) renderState(song); else renderSong(song);
    }, !!settings?.loop);
  }
  // Each row's play button and the open editor's playhead follow what plays.
  function markPlaying(song: number) {
    const b = document.querySelector<HTMLButtonElement>(`#song-${song} [data-play]`);
    if (b) { const on = kit.playing()?.key === String(song); b.textContent = on ? '■' : '▶'; b.title = on ? text.stop : text.play; b.classList.toggle('on', on); }
  }

  // --- Song list ----------------------------------------------------------------------
  const list = root.querySelector<HTMLOListElement>('[data-songs]')!;
  function uses(s: SongInfo) {
    const out: string[] = [];
    if (s.fixed) out.push(text.usesFixed[s.fixed]);
    if (s.stageMaps) out.push(fill(text.usesStage, { n: s.stageMaps }));
    if (s.pilots.length) {
      const names = [...new Set(s.pilots.map((p) => pilotName.get(p)).filter(Boolean))];
      out.push(fill(text.usesPilots, { names: names.slice(0, 4).join(lang === 'en' || lang === 'ko' ? ', ' : '、') + (names.length > 4 ? fill(text.usesMore, { n: names.length - 4 }) : '') }));
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
    const ready = t?.state === 'ready';
    const playButton = ready ? el('button', { type: 'button', class: 'mini play', 'data-play': '', title: text.play,
      onclick: () => { if (kit.playing() === t) stop(); else if (t) play(t); } }, '▶') : null;
    const actions = el('div', { class: 'song-actions' },
      playButton,
      el('label', { class: 'mini' }, text.choose, file),
      t ? el('button', { type: 'button', class: 'mini', onclick: () => { stop(); tracks.delete(song); if (open === song) open = null; if (t) kit.forget(t); renderSong(song); } }, text.remove) : null,
      ready ? el('span', { class: 'song-fold', 'aria-hidden': 'true' }, open === song ? '▴' : '▾') : null);
    // A ready song's whole head opens and closes its editor; the buttons in it keep their own clicks.
    const head = el('div', { class: `song-head${ready ? ' openable' : ''}` },
      el('span', { class: 'song-no' }, String(s.row)),
      el('span', { class: 'song-title' }, s.title),
      actions,
      el('span', { class: 'song-uses' }, uses(s) + (s.karaoke ? ` · ${text.karaoke}` : '')),
      el('span', { class: 'song-state' }));
    if (ready) {
      const toggle = () => { open = open === song ? null : song; rerenderOpen(song); };
      Object.assign(head, { tabIndex: 0, title: open === song ? text.close : text.edit });
      head.setAttribute('role', 'button');
      head.setAttribute('aria-expanded', String(open === song));
      head.addEventListener('click', (e) => { if (!(e.target as Element).closest('button, label, input')) toggle(); });
      head.addEventListener('keydown', (e) => { if ((e.key === 'Enter' || e.key === ' ') && e.target === head) { e.preventDefault(); toggle(); } });
    }
    li.replaceChildren(head);
    renderState(song);
    markPlaying(song);
    if (ready && open === song) li.append(editor(t!));
  }
  function rerenderOpen(song: number) {
    for (const k of new Set([song, ...tracks.keys()])) renderSong(k);
  }

  // --- Editor -------------------------------------------------------------------------
  function editor(t: Track) {
    const titleInput = (key: keyof Titles, label: string) => el('label', { class: 'field' }, label,
      el('input', { value: t.titles[key], oninput: (e: Event) => { t.titles[key] = (e.target as HTMLInputElement).value; } }));
    const others = [...tracks.values()].filter((o) => o !== t && o.level !== undefined).map((o) => o.level!);
    return kit.editor(t, { ...text, trimStart: '', trimEnd: '', trimHint: '' }, { others, root: root!, extra: [
      el('div', { class: 'row' }, el('strong', {}, text.titles)),
      el('div', { class: 'row' }, titleInput('zh-Hans', '中文'), titleInput('en', 'English'), titleInput('ja', '日本語'))] });
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
    const b: Record<string, Record<string, unknown>> = {};
    for (const kind of ['pilot', 'unit'] as const) {
      const all = { ...(keptBattle[kind] ?? {}), ...Object.fromEntries(battle[kind]) };
      if (Object.keys(all).length) b[kind] = all;
    }
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
      stop(); tracks.clear(); battle.pilot.clear(); battle.unit.clear(); open = null;
      keptManifest = mod; keptFiles = {}; keptTracks = {}; keptBattle = {};
      field('id').value = mod.id ?? '';
      field('version').value = mod.version ?? '1.0.0';
      for (const l of ['zh-Hans', 'en', 'ja']) field(`name-${l}`).value = typeof mod.name === 'string' ? (l === 'en' ? mod.name : '') : mod.name?.[l] ?? '';
      field('authors').value = (mod.authors ?? []).join(', ');
      field('license').value = LICENSES.includes(mod.license) ? mod.license : '';
        field('description').value = typeof mod.description === 'string' ? mod.description : mod.description?.[locale] ?? '';
      for (const kind of ['pilot', 'unit'] as const)
        for (const [k, v] of Object.entries(music.battle?.[kind] ?? {})) {
          if (/^\d+$/.test(k) && Number.isInteger(v)) battle[kind].set(Number(k), v as number);
          else (keptBattle[kind] ??= {})[k] = v;
        }
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
