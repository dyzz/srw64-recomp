// Writes src/data/release.json (the site's download data and /latest.json)
// from a build_release.py output directory:
//
//   node scripts/sync-release.mjs ../build/release/0.3.5-cdb4020 [--quark <share url>]
//
// Download links point at the GitHub release for the tag; the Quark share link
// is added by hand once the files are uploaded there. The date is the tag's
// commit date. `hd` is the HD pack the build names: a new one with its own release
// (hd-<version>), or the one already listed here when the pack has not changed;
// build_release.py compares the next pack's content_sha256 with it.
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [dir, ...rest] = process.argv.slice(2);
if (!dir) throw new Error('usage: sync-release.mjs <build/release/VERSION-COMMIT> [--quark URL]');
const quarkAt = rest.indexOf('--quark');
const quark = quarkAt >= 0 ? rest[quarkAt + 1] : null;

const build = JSON.parse(readFileSync(join(dir, 'release.json'), 'utf8'));
const repo = 'dyzz/srw64-recomp';
const date = execFileSync('git', ['log', '-1', '--format=%cs', build.commit], { encoding: 'utf8' }).trim();

const platformOf = (name) =>
  /macos/.test(name) ? 'macos' : /linux/.test(name) ? 'linux' : /windows/.test(name) ? 'windows' : /android|\.apk$/.test(name) ? 'android' : 'other';

const files = Object.entries(build.artifacts).map(([name, a]) => ({
  platform: platformOf(name),
  name,
  bytes: a.bytes,
  sha256: a.sha256,
  url: `https://github.com/${repo}/releases/download/${build.tag}/${name}`,
}));

// The HD pack (build_release.py), without its build-only flag.
const { changed: _changed, ...hd } = build.hd;
if (!hd.version || !hd.content_sha256 || !hd.url) throw new Error('release.json has no versioned HD pack');

const out = {
  published: true,
  version: build.version,
  tag: build.tag,
  commit: build.commit,
  date,
  github: `https://github.com/${repo}/releases/tag/${build.tag}`,
  quark,
  files,
  hd,
};
writeFileSync(new URL('../src/data/release.json', import.meta.url), JSON.stringify(out, null, 2) + '\n');
console.log(`release.json: ${out.version} (${date}), ${files.length} files, HD ${hd.version}${build.hd.changed ? ' (new)' : ''}`);
