// Texts the MOD makers share (pages/[lang]/mods/data.astro, names.astro and the package
// form, components/ModPackageForm.astro), in one page language.
import { tl, type Lang } from '../i18n';
import { release } from '../data/release';

const L = <T>(lang: Lang, v: { zh: T; en: T; ja: T; ko: T }) => v[tl(lang)];

export const LICENSES = ['CC-BY-4.0', 'CC-BY-SA-4.0', 'CC-BY-NC-4.0', 'CC-BY-NC-SA-4.0', 'CC0-1.0', 'MIT', 'GPL-3.0-or-later'];

export const packageText = (lang: Lang) => ({
  package: L(lang, { zh: '包信息', en: 'Package', ja: 'パッケージ情報', ko: '패키지 정보' }),
  id: L(lang, { zh: 'ID（作者.名字，发布后不要再改）', en: 'ID (author.name; keep it once released)', ja: 'ID（作者.名前。公開後は変更しない）', ko: 'ID (작성자.이름, 공개한 뒤에는 바꾸지 마세요)' }),
  version: L(lang, { zh: '版本', en: 'Version', ja: 'バージョン', ko: '버전' }),
  name: L(lang, { zh: '名称', en: 'Name', ja: '名前', ko: '이름' }),
  authors: L(lang, { zh: '作者（多人用逗号分隔）', en: 'Authors (comma-separated)', ja: '作者（複数はカンマ区切り）', ko: '작성자 (여러 명은 쉼표로 구분)' }),
  license: L(lang, { zh: '许可证', en: 'License', ja: 'ライセンス', ko: '라이선스' }),
  description: L(lang, { zh: '说明（可选）', en: 'Description (optional)', ja: '説明（任意）', ko: '설명 (선택)' }),
  import: L(lang, { zh: '打开已有的 .srw64mod 继续编辑', en: 'Open an existing .srw64mod', ja: '既存の .srw64mod を開く', ko: '기존 .srw64mod 열기' }),
  export: L(lang, { zh: '导出', en: 'Export', ja: '書き出し', ko: '내보내기' }),
  exportButton: L(lang, { zh: '导出 .srw64mod', en: 'Export .srw64mod', ja: '.srw64mod を書き出す', ko: '.srw64mod 내보내기' }),
  exported: L(lang, { zh: '已导出 {file}（{size}）。把它拖进游戏窗口即可安装。', en: 'Exported {file} ({size}). Drag it into the game window to install.', ja: '{file}（{size}）を書き出しました。ゲームのウィンドウにドラッグするとインストールされます。', ko: '{file} ({size}) 파일을 내보냈습니다. 게임 창에 끌어다 놓으면 설치됩니다.' }),
  errId: L(lang, { zh: 'ID 只能用字母、数字和 . _ -，最多 64 个字符', en: 'The ID may use letters, digits and . _ - (up to 64)', ja: 'ID に使えるのは英数字と . _ -（64 文字まで）です', ko: 'ID에는 영문자, 숫자와 . _ -만 쓸 수 있습니다 (최대 64자)' }),
  errVersion: L(lang, { zh: '版本写成 1.0.0 这样的格式', en: 'Write the version like 1.0.0', ja: 'バージョンは 1.0.0 の形式で書いてください', ko: '버전은 1.0.0 형식으로 적어 주세요' }),
  errName: L(lang, { zh: '至少填一种语言的名称', en: 'Give a name in at least one language', ja: '名前を少なくとも一つの言語で入力してください', ko: '이름을 적어도 한 가지 언어로 입력해 주세요' }),
  errLicense: L(lang, { zh: '请选择许可证', en: 'Choose a license', ja: 'ライセンスを選んでください', ko: '라이선스를 선택해 주세요' }),
  errImport: L(lang, { zh: '无法打开这个包：{detail}', en: 'This package could not be opened: {detail}', ja: 'このパッケージを開けません：{detail}', ko: '이 패키지를 열 수 없습니다: {detail}' }),
  imported: L(lang, { zh: '已打开 {id}', en: 'Opened {id}', ja: '{id} を開きました', ko: '{id}을(를) 열었습니다' }),
  search: L(lang, { zh: '输入名字或编号查找', en: 'Search by name or number', ja: '名前か番号で検索', ko: '이름이나 번호로 검색' }),
  remove: L(lang, { zh: '移除', en: 'Remove', ja: '削除', ko: '제거' }),
  reset: L(lang, { zh: '恢复原版', en: 'Reset', ja: '元に戻す', ko: '원래대로' }),
  original: L(lang, { zh: '原版', en: 'Original', ja: '原作', ko: '원본' }),
  none: L(lang, { zh: '没有符合的条目', en: 'Nothing matches', ja: '該当なし', ko: '일치하는 항목이 없습니다' }),
  pilot: L(lang, { zh: '人物', en: 'Pilot', ja: 'キャラクター', ko: '파일럿' }),
  unit: L(lang, { zh: '机体', en: 'Unit', ja: 'ユニット', ko: '기체' }),
  weapon: L(lang, { zh: '武器', en: 'Weapon', ja: '武器', ko: '무기' }),
  game: L(lang, { zh: '需要游戏版本', en: 'Game version needed', ja: '必要なゲームバージョン', ko: '필요한 게임 버전' }),
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
    ko: `이 도구로 만든 패키지는 게임 ${need} 이상 버전이 필요합니다. ${need}은(는) 아직 공개되지 않았습니다 (현재 최신 버전은 ${release.version}). 패키지는 지금 만들어 두고 ${need}이(가) 공개된 뒤에 설치하세요.`,
  });
