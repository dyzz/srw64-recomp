import data from './release.json';

export type Platform = 'windows' | 'macos' | 'linux' | 'android' | 'hd';
export const release = data;
export const fileFor = (p: Platform) => data.files.find((f) => f.platform === p);
export const mb = (bytes: number) => `${(bytes / 1e6).toFixed(bytes >= 1e8 ? 0 : 1)} MB`;

export const PLATFORM_LABEL: Record<Platform, string> = {
  windows: 'Windows',
  macos: 'macOS',
  linux: 'Linux / Steam Deck',
  android: 'Android',
  hd: 'HD',
};
