// A .srw64mod wrapped for Thunderstore in the browser, by the rules of tools/mods/thunderstore.py
// (mod-packages.md §13.0): manifest.json, a 256×256 icon.png and README.md at the top, the
// package beside them as made. No DOM and no imports, so tests/test_unit_maker.py checks it
// against the Python script under Node.

export const DESCRIPTION_LIMIT = 250, NAME_LIMIT = 128, ICON_SIZE = 256;
export const NAME_RE = /^[A-Za-z0-9_]+$/;
// The game's HD pack, a dependency the game resolves itself.
const VIRTUAL = new Set(['srw64.hd']);
const LANGUAGE_NAMES: Record<string, string> = { en: 'English', ja: '日本語', 'zh-Hans': '简体中文', 'zh-Hant': '繁體中文', ko: '한국어' };

type Manifest = Record<string, any>;

// srw64.space-a -> space_a: the part after the author's prefix, in Thunderstore's letters.
export const defaultName = (id: string) => (id.includes('.') ? id.slice(id.indexOf('.') + 1) : id).replace(/[^A-Za-z0-9_]/g, '_').slice(0, NAME_LIMIT);

// A text field (a string, or {language: text}) in one language, else the first given.
export function textIn(value: unknown, language = 'en'): string {
  if (typeof value === 'string') return value;
  if (value && typeof value === 'object') { const v = value as Record<string, string>; const keys = Object.keys(v); if (keys.length) return v[language] || v[keys[0]]; }
  return '';
}

export function shortDescription(manifest: Manifest): string {
  const text = textIn(manifest.description).split(/\s+/).filter(Boolean).join(' ') || textIn(manifest.name);
  // Python's len counts code points.
  const chars = [...text];
  return chars.length <= DESCRIPTION_LIMIT ? text : chars.slice(0, DESCRIPTION_LIMIT - 1).join('').trimEnd() + '…';
}

// Requirements other than the HD pack have no Thunderstore name here; the command-line tool
// takes them (--dep).
export const unmappedRequires = (manifest: Manifest) => Object.keys(manifest.requires ?? {}).filter((id) => !VIRTUAL.has(id));

export function manifestFor(manifest: Manifest, name: string, website = ''): Manifest {
  return { name, version_number: manifest.version, website_url: website, description: shortDescription(manifest), dependencies: [] };
}

// README.md from mod.json: the names and descriptions in every language it has.
export function readme(manifest: Manifest, packageFile: string): string {
  const name = textIn(manifest.name);
  const lines: string[] = [`# ${name}`, ''];
  const description = manifest.description;
  if (typeof description === 'string') lines.push(description, '');
  const names: Record<string, string> = manifest.name && typeof manifest.name === 'object' ? manifest.name : {};
  if (description && typeof description === 'object')
    for (const [language, text] of Object.entries(description as Record<string, string>)) {
      const title = names[language] ?? '';
      const heading = LANGUAGE_NAMES[language] ?? language;
      lines.push(`## ${heading}` + (title && title !== name ? `: ${title}` : ''), '', text, '');
    }
  lines.push('## Installing', '',
    'This is a mod for Marchwind64, the Super Robot Wars 64 recompilation.',
    'Download it with **Manual Download**, then drag the zip onto the game window, choose it on the MOD page,',
    "or put it in the `mods` folder of the game's data folder. Mod managers (r2modman, Gale) are not needed or supported.",
    '',
    `Version 0.5.3 and earlier do not open this zip: take \`${packageFile}\` out of it and use that file the same way.`,
    '');
  const facts = [`- Package: \`${manifest.id}\` ${manifest.version} (\`${packageFile}\`)`];
  if (manifest.game) facts.push(`- Game version: \`${manifest.game}\``);
  for (const [key, label] of [['requires', 'Requires'], ['optional', 'Works with'], ['conflicts', 'Conflicts with']] as const)
    if (manifest[key] && Object.keys(manifest[key]).length)
      facts.push(`- ${label}: ` + Object.entries(manifest[key] as Record<string, string>).map(([i, r]) => (VIRTUAL.has(i) ? 'the HD pack' : `\`${i}\``) + ` ${r}`).join(', '));
  if (manifest.authors?.length) facts.push('- Authors: ' + manifest.authors.join(', '));
  if (manifest.license) facts.push(`- License: ${manifest.license}`);
  lines.push('## Details', '', ...facts, '');
  return lines.join('\n');
}

// The zip's entries in order, the package stored as made.
export function entries(manifest: Manifest, name: string, icon: Uint8Array, packageFile: string, packageBytes: Uint8Array, encode: (s: string) => Uint8Array) {
  return [
    ['manifest.json', encode(JSON.stringify(manifestFor(manifest, name), null, 2) + '\n')],
    ['icon.png', icon],
    ['README.md', encode(readme(manifest, packageFile))],
    [packageFile, packageBytes],
  ] as [string, Uint8Array][];
}
export const zipName = (team: string, name: string, version: string) => [team, name, version].filter(Boolean).join('-') + '.zip';
