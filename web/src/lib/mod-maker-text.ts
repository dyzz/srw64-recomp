// Texts the MOD makers share (pages/[lang]/mods/data.astro, names.astro and the package
// form, components/ModPackageForm.astro), in one page language.
import { tl, type Lang } from '../i18n';
import { release } from '../data/release';

const L = <T>(lang: Lang, v: { zh: T; en: T; ja: T }) => v[tl(lang)];

export const LICENSES = ['CC-BY-4.0', 'CC-BY-SA-4.0', 'CC-BY-NC-4.0', 'CC-BY-NC-SA-4.0', 'CC0-1.0', 'MIT', 'GPL-3.0-or-later'];

export const packageText = (lang: Lang) => ({
  package: L(lang, { zh: '包信息', en: 'Package', ja: 'パッケージ情報' }),
  id: L(lang, { zh: 'ID（作者.名字，发布后不要再改）', en: 'ID (author.name; keep it once released)', ja: 'ID（作者.名前。公開後は変更しない）' }),
  version: L(lang, { zh: '版本', en: 'Version', ja: 'バージョン' }),
  name: L(lang, { zh: '名称', en: 'Name', ja: '名前' }),
  authors: L(lang, { zh: '作者（多人用逗号分隔）', en: 'Authors (comma-separated)', ja: '作者（複数はカンマ区切り）' }),
  license: L(lang, { zh: '许可证', en: 'License', ja: 'ライセンス' }),
  description: L(lang, { zh: '说明（可选）', en: 'Description (optional)', ja: '説明（任意）' }),
  import: L(lang, { zh: '打开已有的 .srw64mod 继续编辑', en: 'Open an existing .srw64mod', ja: '既存の .srw64mod を開く' }),
  export: L(lang, { zh: '导出', en: 'Export', ja: '書き出し' }),
  exportButton: L(lang, { zh: '导出 .srw64mod', en: 'Export .srw64mod', ja: '.srw64mod を書き出す' }),
  exported: L(lang, { zh: '已导出 {file}（{size}）。把它拖进游戏窗口即可安装。', en: 'Exported {file} ({size}). Drag it into the game window to install.', ja: '{file}（{size}）を書き出しました。ゲームのウィンドウにドラッグするとインストールされます。' }),
  errId: L(lang, { zh: 'ID 只能用字母、数字和 . _ -，最多 64 个字符', en: 'The ID may use letters, digits and . _ - (up to 64)', ja: 'ID に使えるのは英数字と . _ -（64 文字まで）です' }),
  errVersion: L(lang, { zh: '版本写成 1.0.0 这样的格式', en: 'Write the version like 1.0.0', ja: 'バージョンは 1.0.0 の形式で書いてください' }),
  errName: L(lang, { zh: '至少填一种语言的名称', en: 'Give a name in at least one language', ja: '名前を少なくとも一つの言語で入力してください' }),
  errLicense: L(lang, { zh: '请选择许可证', en: 'Choose a license', ja: 'ライセンスを選んでください' }),
  errImport: L(lang, { zh: '无法打开这个包：{detail}', en: 'This package could not be opened: {detail}', ja: 'このパッケージを開けません：{detail}' }),
  imported: L(lang, { zh: '已打开 {id}', en: 'Opened {id}', ja: '{id} を開きました' }),
  search: L(lang, { zh: '输入名字或编号查找', en: 'Search by name or number', ja: '名前か番号で検索' }),
  remove: L(lang, { zh: '移除', en: 'Remove', ja: '削除' }),
  reset: L(lang, { zh: '恢复原版', en: 'Reset', ja: '元に戻す' }),
  original: L(lang, { zh: '原版', en: 'Original', ja: '原作' }),
  none: L(lang, { zh: '没有符合的条目', en: 'Nothing matches', ja: '該当なし' }),
  pilot: L(lang, { zh: '人物', en: 'Pilot', ja: 'キャラクター' }),
  unit: L(lang, { zh: '机体', en: 'Unit', ja: 'ユニット' }),
  weapon: L(lang, { zh: '武器', en: 'Weapon', ja: '武器' }),
  game: L(lang, { zh: '需要游戏版本', en: 'Game version needed', ja: '必要なゲームバージョン' }),
});

// Above a maker while the latest public release is older than the version its packages
// need (the form writes game ">=need"); sync-release bringing that release in removes it.
const older = (a: string, b: string) => {
  const x = a.split('.').map(Number), y = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if ((x[i] ?? 0) !== (y[i] ?? 0)) return (x[i] ?? 0) < (y[i] ?? 0);
  return false;
};
export const versionNotice = (lang: Lang, need: string) =>
  !older(release.version, need) ? null : L(lang, {
    zh: `这个工具做出的包需要游戏 ${need} 或更新的版本才能用。${need} 还没有发布（目前最新是 ${release.version}），可以先把包做好，等 ${need} 发布后再安装。`,
    en: `Packages made here need the game's version ${need} or later. ${need} is not out yet (the latest is ${release.version}); you can make your package now and install it once ${need} is released.`,
    ja: `このツールで作ったパッケージには、ゲームのバージョン ${need} 以降が必要です。${need} はまだ公開されていません（現在の最新は ${release.version}）。パッケージは今作っておき、${need} の公開後にインストールしてください。`,
  });
