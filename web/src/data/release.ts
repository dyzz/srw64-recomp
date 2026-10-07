import data from './release.json';

export type Platform = 'windows' | 'macos' | 'linux' | 'android' | 'hd';
export const release = data;
// The HD pack has versions of its own (dates) and its own GitHub release; an empty
// version is a pack from before that, released with the app.
export const hd = data.hd;
export const fileFor = (p: Platform) => (p === 'hd' ? data.hd : data.files.find((f) => f.platform === p));
// Every download, the HD pack last.
export const allFiles = [...data.files, { platform: 'hd', ...data.hd }];
export const mb = (bytes: number) => `${(bytes / 1e6).toFixed(bytes >= 1e8 ? 0 : 1)} MB`;

export const PLATFORM_LABEL: Record<Platform, string> = {
  windows: 'Windows',
  macos: 'macOS',
  linux: 'Linux / Steam Deck',
  android: 'Android',
  hd: 'HD',
};
