import data from './release.json';

export type Platform = 'windows' | 'macos' | 'linux' | 'android' | 'switch' | 'hd' | 'switch-hd';
export const release = data;
// false while a release is taken down (its GitHub release a draft): no download links,
// the install page says it is being prepared. web/scripts/sync-release.mjs sets it true.
export const published = (data as { published?: boolean }).published !== false;
// The HD pack has versions of its own (1.0, 1.1 …) and its own GitHub release; an empty
// version is a pack from before that, released with the app.
export const hd = data.hd;
// The Switch's HD pack (the same pictures in the formats its GPU reads), in the HD pack's release.
type File = { name: string; bytes: number; sha256: string; url: string };
const switchHd = (data.hd as { switch?: File }).switch;
export const fileFor = (p: Platform) =>
  !published ? undefined : p === 'hd' ? data.hd : p === 'switch-hd' ? switchHd : data.files.find((f) => f.platform === p);
// Every download, the HD packs last.
export const allFiles = published
  ? [...data.files, { platform: 'hd', ...data.hd }, ...(switchHd ? [{ platform: 'switch-hd', ...switchHd }] : [])]
  : [];
// Netdisk mirrors of the same files for mainland China, where GitHub downloads often fail:
// share links with the passcode in ?pwd= (sync-release.mjs --quark / --baidu).
export type Mirror = { id: 'quark' | 'baidu'; url: string; passcode: string | null };
export const mirrors: Mirror[] = !published
  ? []
  : (['quark', 'baidu'] as const).flatMap((id) => {
      const url = (data as Record<string, unknown>)[id];
      return typeof url === 'string' && url ? [{ id, url, passcode: new URL(url).searchParams.get('pwd') }] : [];
    });
export const mb = (bytes: number) => `${(bytes / 1e6).toFixed(bytes >= 1e8 ? 0 : 1)} MB`;

export const PLATFORM_LABEL: Record<Platform, string> = {
  windows: 'Windows',
  macos: 'macOS',
  linux: 'Linux / Steam Deck',
  android: 'Android',
  switch: 'Nintendo Switch',
  hd: 'HD',
  'switch-hd': 'Switch HD',
};
