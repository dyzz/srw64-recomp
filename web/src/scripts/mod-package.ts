// What the MOD makers share (scripts/data-maker.ts, names-maker.ts, voice-maker.ts): the package form
// (components/ModPackageForm.astro) as mod.json, opening a package, writing one. Everything
// stays in the browser.
import { strFromU8, strToU8, unzipSync, zipSync, type Zippable } from 'fflate';

export const LICENSES = ['CC-BY-4.0', 'CC-BY-SA-4.0', 'CC-BY-NC-4.0', 'CC-BY-NC-SA-4.0', 'CC0-1.0', 'MIT', 'GPL-3.0-or-later'];
export type Text = Record<string, string>;

export const fill = (s: string, v: Record<string, string | number>) => s.replace(/\{(\w+)\}/g, (_, k) => String(v[k] ?? ''));

export function el<K extends keyof HTMLElementTagNameMap>(tag: K, props: Record<string, any> = {}, ...kids: (Node | string | null | false | undefined)[]) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (v === undefined || v === null || v === false) continue;
    if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else if (k === 'class') node.className = v;
    else if (k === 'value' && 'value' in node) (node as HTMLInputElement).value = String(v);
    else if (k === 'checked' && 'checked' in node) (node as HTMLInputElement).checked = Boolean(v);
    else node.setAttribute(k, v === true ? '' : String(v));
  }
  for (const kid of kids) if (kid !== null && kid !== undefined && kid !== false) node.append(kid);
  return node;
}

export function say(where: HTMLElement, message: string, error = false) {
  where.textContent = message;
  where.classList.toggle('err', error);
}

const size = (n: number) => n > 1 << 20 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.max(1, Math.ceil(n / 1024))} KB`;
const validId = (s: string) => s.length > 0 && s.length <= 64 && s !== '.' && s !== '..' && /^[A-Za-z0-9._-]+$/.test(s);
const parseVersion = (s: string) => {
  const parts = s.split('.');
  if (parts.length < 1 || parts.length > 3 || parts.some((p) => !/^\d{1,6}$/.test(p))) return null;
  return [...parts.map(Number), 0, 0].slice(0, 3).join('.');
};

// The form's mod.json. `game` is the version the package's content needs; a package that
// already asked for more keeps its own.
export function packageForm(root: HTMLElement, text: Text, locale: string, game: string) {
  const field = (k: string) => root.querySelector<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>(`[data-f="${k}"]`)!;
  let kept: Record<string, any> = {};
  return {
    manifest(): { manifest: Record<string, unknown>; version: string } | string {
      const id = field('id').value.trim(), version = parseVersion(field('version').value.trim());
      if (!validId(id)) return text.errId;
      if (!version) return text.errVersion;
      const name: Record<string, string> = {};
      for (const l of ['zh-Hans', 'en', 'ja']) { const v = field(`name-${l}`).value.trim(); if (v) name[l] = v; }
      if (!Object.keys(name).length) return text.errName;
      const license = field('license').value;
      if (!LICENSES.includes(license)) return text.errLicense;
      const out: Record<string, unknown> = { ...kept, schema: 'srw64.mod.v1', id, version, name, license };
      const authors = field('authors').value.split(/[,，、]/).map((a) => a.trim()).filter(Boolean);
      if (authors.length) out.authors = authors; else delete out.authors;
      const description = field('description').value.trim();
      if (description) out.description = { ...(typeof kept.description === 'object' ? kept.description : {}), [locale]: description };
      else delete out.description;
      const wanted = /^>=\s*(\d+)\.(\d+)(?:\.(\d+))?$/.exec(String(kept.game ?? ''));
      const need = game.split('.').map(Number);
      const theirs = wanted ? [Number(wanted[1]), Number(wanted[2]), Number(wanted[3] ?? 0)] : null;
      const newer = theirs && (theirs[0] - need[0] || theirs[1] - need[1] || theirs[2] - need[2]) > 0;
      out.game = newer ? kept.game : `>=${game}`;
      return { manifest: out, version };
    },
    fill(mod: Record<string, any>) {
      kept = mod;
      field('id').value = mod.id ?? '';
      field('version').value = mod.version ?? '1.0.0';
      for (const l of ['zh-Hans', 'en', 'ja']) field(`name-${l}`).value = typeof mod.name === 'string' ? (l === 'en' ? mod.name : '') : mod.name?.[l] ?? '';
      field('authors').value = (mod.authors ?? []).join(', ');
      field('license').value = LICENSES.includes(mod.license) ? mod.license : '';
      field('description').value = typeof mod.description === 'string' ? mod.description : mod.description?.[locale] ?? '';
    },
  };
}

// A package's files by their path inside it (an archive with one top folder is unwrapped).
export async function openPackage(file: File): Promise<{ mod: Record<string, any>; files: Record<string, Uint8Array> }> {
  const entries = unzipSync(new Uint8Array(await file.arrayBuffer()));
  const names = Object.keys(entries).filter((n) => !n.endsWith('/') && !n.startsWith('__MACOSX/') && !n.endsWith('.DS_Store'));
  const top = names.includes('mod.json') ? '' : names.find((n) => /^[^/]+\/mod\.json$/.test(n))?.replace('mod.json', '');
  if (top === undefined) throw new Error('mod.json');
  const files: Record<string, Uint8Array> = {};
  for (const n of names) if (n.startsWith(top)) files[n.slice(top.length)] = entries[n];
  return { mod: JSON.parse(strFromU8(files['mod.json'])), files };
}

export const readJson = (bytes?: Uint8Array) => bytes ? JSON.parse(strFromU8(bytes)) : undefined;
export const jsonBytes = (value: unknown) => strToU8(JSON.stringify(value, null, 2) + '\n');

// Zip `files` with mod.json and offer the package; says what it wrote.
export function writePackage(manifest: Record<string, unknown>, version: string, files: Record<string, Uint8Array>, status: HTMLElement, text: Text) {
  const zippable: Zippable = { 'mod.json': jsonBytes(manifest) };
  for (const [path, bytes] of Object.entries(files)) zippable[path] = /\.(png|ogg)$/.test(path) ? [bytes, { level: 0 }] : bytes;
  const zip = zipSync(zippable, { level: 6 });
  const name = `${manifest.id}-${version}.srw64mod`;
  const url = URL.createObjectURL(new Blob([zip as BlobPart], { type: 'application/zip' }));
  const a = el('a', { href: url, download: name });
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60_000);
  say(status, fill(text.exported, { file: name, size: size(zip.length) }));
}

// Matches for a search box over named entries: an exact number first, then names.
export function search<T extends { id: number; name: { ja: string; zh: string; en: string } }>(list: T[], query: string, limit = 30): T[] {
  const q = query.trim().toLowerCase();
  if (!q) return [];
  if (/^\d+$/.test(q)) return list.filter((x) => String(x.id) === q || String(x.id).startsWith(q)).slice(0, limit);
  return list.filter((x) => `${x.name.ja} ${x.name.zh} ${x.name.en}`.toLowerCase().includes(q)).slice(0, limit);
}
