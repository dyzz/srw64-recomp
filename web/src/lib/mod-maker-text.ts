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
  template: L(lang, { zh: '从模板开始', en: 'Start from the template', ja: 'テンプレートから始める', ko: '템플릿으로 시작' }),
  templateHint: L(lang, { zh: '载入一份做好的示例包（新人物、新机体、新武器、入队），在它上面改', en: 'Load a finished example package (a new character, unit and weapon, and their joining) and change it', ja: '完成した見本のパッケージ（新キャラクター・新ユニット・新武器・加入）を読み込んで、そこから変えていきます', ko: '완성된 예시 패키지(새 인물·새 기체·새 무기·합류)를 불러와서 고쳐 나갑니다' }),
  templateLoaded: L(lang, { zh: '已载入模板。改成你自己的内容（包 ID、名字、数值……）再导出。', en: 'The template is loaded. Make it yours (package ID, names, numbers…) before exporting.', ja: 'テンプレートを読み込みました。自分の内容（パッケージ ID・名前・数値など）に変えてから書き出してください。', ko: '템플릿을 불러왔습니다. 자신의 내용(패키지 ID, 이름, 수치 등)으로 바꾼 뒤 내보내세요.' }),
  templateConfirm: L(lang, { zh: '用模板替换现在的内容？没导出的改动会丢掉。', en: 'Replace what is here with the template? Changes not exported are lost.', ja: '今の内容をテンプレートで置き換えますか？書き出していない変更は失われます。', ko: '지금 내용을 템플릿으로 바꿀까요? 내보내지 않은 변경은 사라집니다.' }),
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

// The unit maker and the level editor cover the common cases; a big package (many units, a
// campaign of many stages, whole sets of lines) is easier written as files by an AI agent that
// follows the docs, which say how in their "Let an AI make it" sections.
export const aiHint = (lang: Lang, kind: 'units' | 'campaign') => ({
  text: L(lang, kind === 'units' ? {
    zh: '网页工具适合简单的包和起步，很难满足复杂的需求。机体多、形态和合体技复杂、台词成套时，建议用能读写文件的 AI 工具（Claude Code、Codex 等）照文档直接写包的文件，再打包成 .srw64mod。',
    en: 'This web tool suits simple packages and getting started; it can hardly meet complex needs. For many units, involved forms and combination attacks, or whole sets of lines, an AI tool that reads and writes files (Claude Code, Codex and the like) writing the package\'s files from the docs, then packing them as a .srw64mod, is the better way.',
    ja: 'このウェブツールは簡単なパッケージや最初の一歩に向いていて、複雑な要求にはなかなか応えられません。ユニットが多い、形態や合体攻撃が複雑、台詞を一式そろえるといった場合は、ファイルを読み書きできる AI ツール（Claude Code、Codex など）にドキュメントどおりパッケージのファイルを書かせ、.srw64mod にまとめる方法をおすすめします。',
    ko: '이 웹 도구는 간단한 패키지와 시작용으로 알맞고, 복잡한 요구는 채우기 어렵습니다. 기체가 많거나 형태와 합체기가 복잡하거나 대사를 한 벌로 갖출 때는, 파일을 읽고 쓸 수 있는 AI 도구(Claude Code, Codex 등)가 문서대로 패키지 파일을 쓰고 .srw64mod로 묶는 방법을 권합니다.',
  } : {
    zh: '网页编辑器适合简单的关卡和起步，很难满足复杂的需求。关卡多、事件和分支复杂、剧情台词多时，建议用能读写文件的 AI 工具（Claude Code、Codex 等）照文档直接写战役的文件，再打包成 .srw64mod。',
    en: 'This web editor suits simple stages and getting started; it can hardly meet complex needs. For many stages, involved events and branches, or a lot of story, an AI tool that reads and writes files (Claude Code, Codex and the like) writing the campaign\'s files from the docs, then packing them as a .srw64mod, is the better way.',
    ja: 'このウェブエディターは簡単なステージや最初の一歩に向いていて、複雑な要求にはなかなか応えられません。ステージが多い、イベントや分岐が複雑、ストーリーの台詞が多いといった場合は、ファイルを読み書きできる AI ツール（Claude Code、Codex など）にドキュメントどおりキャンペーンのファイルを書かせ、.srw64mod にまとめる方法をおすすめします。',
    ko: '이 웹 편집기는 간단한 스테이지와 시작용으로 알맞고, 복잡한 요구는 채우기 어렵습니다. 스테이지가 많거나 이벤트와 분기가 복잡하거나 스토리 대사가 많을 때는, 파일을 읽고 쓸 수 있는 AI 도구(Claude Code, Codex 등)가 문서대로 캠페인 파일을 쓰고 .srw64mod로 묶는 방법을 권합니다.',
  }),
  link: L(lang, { zh: '交给 AI 做的方法', en: 'How to let an AI make it', ja: 'AI に任せる方法', ko: 'AI에게 맡기는 방법' }),
  href: `/${lang}/docs/${kind === 'units' ? 'mod-data' : 'mod-campaign'}/`,
});
