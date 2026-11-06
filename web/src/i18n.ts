// The site languages. `code` is the URL prefix, `tag` the BCP 47 tag (and the name of the
// repository's content/<tag>/ folders).
export const LANGS = [
  { code: 'zh', tag: 'zh-Hans', label: '简体中文', short: '简' },
  { code: 'tw', tag: 'zh-Hant', label: '繁體中文', short: '繁' },
  { code: 'en', tag: 'en', label: 'English', short: 'EN' },
  { code: 'ja', tag: 'ja', label: '日本語', short: '日' },
  { code: 'ko', tag: 'ko', label: '한국어', short: '한' },
] as const;

export type Lang = (typeof LANGS)[number]['code'];
export const DEFAULT_LANG: Lang = 'zh';

/** The languages the site is written in. Traditional Chinese (tw, Taiwan) is not written: as
 * in the game, it is made from the Simplified Chinese. Its pages are built from the zh text
 * and converted at the end of the build (scripts/zh_hant_pages.py, the game's converter). */
export type TextLang = 'zh' | 'en' | 'ja' | 'ko';
export const tl = (lang: Lang): TextLang => (lang === 'tw' ? 'zh' : lang);
/** The page languages a text language is shown in: zh text also makes the tw pages. */
export const shownIn = (text: string): Lang[] => (text === 'zh' ? ['zh', 'tw'] : [text as Lang]);

export const langPaths = () => LANGS.map((l) => ({ params: { lang: l.code } }));
export const langInfo = (lang: Lang) => LANGS.find((l) => l.code === lang)!;
/** The lang attribute of TEXT shown on a PAGE: zh text on a tw page is (made) zh-Hant. */
export const tagOf = (page: Lang, text: TextLang) => langInfo(text === 'zh' && page === 'tw' ? 'tw' : text).tag;

/** Pick the string for `lang` out of a { zh, en, ja, ko } record; one without Korean yet
 * shows the English. */
export const pick = <T>(lang: Lang, v: Record<TextLang, T>): T => v[tl(lang)] ?? v.en;
/** Between the items of an inline list, and before what follows a label. */
export const listSep = (lang: Lang) => (lang === 'en' || lang === 'ko' ? ', ' : '、');
export const colon = (lang: Lang) => (lang === 'en' || lang === 'ko' ? ': ' : '：');

/** Same page in another language: swaps the first path segment. */
export const switchLang = (path: string, to: Lang) =>
  path.replace(/^\/(zh|tw|en|ja|ko)(?=\/|$)/, `/${to}`) || `/${to}/`;

export const UI = {
  nav: {
    install: { zh: '安装', en: 'Install', ja: 'インストール', ko: '설치' },
    faq: { zh: '常见问题', en: 'FAQ', ja: 'よくある質問', ko: '자주 묻는 질문' },
    blog: { zh: '博客', en: 'Blog', ja: 'ブログ', ko: '블로그' },
    // Not in the header (full); linked beside the home page download.
    releases: { zh: '更新日志', en: 'Release notes', ja: '更新履歴', ko: '변경 내역' },
    // Not in the header either; linked from the install page and the FAQ.
    android: { zh: '安卓兼容列表', en: 'Android compatibility', ja: 'Android 対応状況', ko: 'Android 호환성' },
    docs: { zh: '技术文档', en: 'Docs', ja: '技術資料', ko: '기술 문서' },
    guide: { zh: '攻略', en: 'Guide', ja: '攻略', ko: '공략' },
    story: { zh: '剧情', en: 'Story', ja: 'シナリオ', ko: '시나리오' },
    library: { zh: '图鉴', en: 'Library', ja: '図鑑', ko: '도감' },
    reviews: { zh: '意见', en: 'Suggestions', ja: '意見', ko: '의견' },
    mods: { zh: 'MOD', en: 'Mods', ja: 'MOD', ko: '모드' },
    // The header's MOD menu; the group names say "MOD", maker and guide, so the items do not.
    modDownloads: { zh: 'MOD 下载', en: 'Mod downloads', ja: 'MOD ダウンロード', ko: '모드 다운로드' },
    // The MOD menu's groups, and the one item under downloads.
    modGroupDownloads: { zh: 'MOD 下载', en: 'Download', ja: 'ダウンロード', ko: '다운로드' },
    modGroupDocs: { zh: 'MOD 文档', en: 'Guides', ja: 'ドキュメント', ko: '가이드' },
    modGroupTools: { zh: 'MOD 制作工具', en: 'Mod makers', ja: 'MOD 制作ツール', ko: '모드 제작 도구' },
    modAll: { zh: '全部 MOD', en: 'All mods', ja: 'すべての MOD', ko: '모든 모드' },
    modSubmit: { zh: '投稿 MOD', en: 'Submit a mod', ja: 'MOD を投稿', ko: 'MOD 제출' },
    modPackages: { zh: '安装与制作', en: 'Installing and making', ja: 'インストールと制作', ko: '설치와 제작' },
    modMusic: { zh: '音乐与语音', en: 'Music and voices', ja: '音楽とボイス', ko: '음악과 음성' },
    modData: { zh: '数值', en: 'Numbers', ja: '数値', ko: '수치' },
    modNames: { zh: '名字、头像与图标', en: 'Names, portraits and icons', ja: '名前・顔グラ・アイコン', ko: '이름, 얼굴 그래픽, 아이콘' },
    modIds: { zh: 'ID 表', en: 'ID tables', ja: 'ID 表', ko: 'ID 표' },
    modBgm: { zh: 'BGM', en: 'BGM', ja: 'BGM', ko: 'BGM' },
    modVoicesMaker: { zh: '战斗语音', en: 'Battle voices', ja: '戦闘ボイス', ko: '전투 음성' },
    modDataMaker: { zh: '数值', en: 'Numbers', ja: '数値', ko: '수치' },
    modNamesMaker: { zh: '名字与头像', en: 'Names and portraits', ja: '名前・顔グラ', ko: '이름과 얼굴 그래픽' },
    modIconsMaker: { zh: '地图图标', en: 'Map icons', ja: 'マップアイコン', ko: '맵 아이콘' },
    // The sister project, the Chinese translation of Super Robot Wars Z (PS2).
    srwz: {
      label: { zh: '机战Z', en: 'SRW Z', ja: 'スパロボZ', ko: '슈로대 Z' },
      title: {
        zh: '姊妹项目：PS2《超级机器人大战Z》中文化补丁与剧情审阅站',
        en: 'Sister project: the Chinese translation of Super Robot Wars Z (PS2), with its story review site',
        ja: '姉妹プロジェクト：PS2『スーパーロボット大戦Z』中国語化パッチとシナリオ校閲サイト',
        ko: '자매 프로젝트: PS2 『슈퍼로봇대전 Z』 중국어 번역과 시나리오 검토 사이트',
      },
    },
  },
  // The project is MARCHWIND 64; the game it recompiles is named in descriptions.
  brand: 'MARCHWIND 64',
  title: {
    zh: '《超级机器人大战64》非官方原生重编译版',
    en: 'An unofficial native recompilation of Super Robot Wars 64',
    ja: '『スーパーロボット大戦64』非公式ネイティブ再コンパイル版',
    ko: '『슈퍼로봇대전64』 비공식 네이티브 리컴파일 버전',
  },
  description: {
    zh: 'Marchwind 64 是《超级机器人大战64》的非官方原生重编译版，可在 Windows、macOS、Linux、Steam Deck、Android 和 Nintendo Switch 上直接运行，提供中英文全文翻译。另有可选的 HD 美术包。使用时需自备日版 ROM。',
    en: 'Marchwind 64 is an unofficial native recompilation of Super Robot Wars 64 for Windows, macOS, Linux, Steam Deck, Android and Nintendo Switch, with full English and Chinese translations. An optional HD art pack is available on every platform. You’ll need your own Japanese ROM.',
    ja: 'マーチウィンド64 は『スーパーロボット大戦64』の非公式ネイティブ再コンパイル版です。Windows・macOS・Linux・Steam Deck・Android・Nintendo Switch で動作し、英語・中国語の全文翻訳を用意しています。別配布の HD アートパックも、すべてのプラットフォームで利用できます。日本版 ROM は各自でご用意ください。',
    ko: 'Marchwind 64는 『슈퍼로봇대전64』의 비공식 네이티브 리컴파일 버전으로, Windows, macOS, Linux, Steam Deck, Android, Nintendo Switch에서 실행되며 영어, 중국어, 일본어, 한국어로 즐길 수 있습니다. 별도의 HD 팩도 모든 플랫폼에서 쓸 수 있습니다. 일본판 ROM은 직접 준비해야 합니다.',
  },
  footer: {
    unofficial: {
      zh: '非官方粉丝项目，与万代南梦宫、BANPRESTO 及各作品权利人无隶属关系。原作角色、美术与商标归各自权利人所有。本站不提供 ROM。',
      en: 'An unofficial fan project, unaffiliated with Bandai Namco, BANPRESTO or the rights holders of the featured series. Original characters, art and trademarks belong to their respective owners. This site does not provide ROMs.',
      ja: '非公式のファンプロジェクトです。バンダイナムコ、バンプレスト、各作品の権利者とは提携していません。原作のキャラクター・アート・商標は各権利者に帰属します。当サイトでは ROM を配布していません。',
      ko: '비공식 팬 프로젝트로, 반다이남코, BANPRESTO 및 수록 작품의 권리자와 아무런 관계가 없습니다. 원작의 캐릭터, 그림, 상표는 각 권리자의 소유입니다. 이 사이트는 ROM을 제공하지 않습니다.',
    },
    source: { zh: '源代码', en: 'Source code', ja: 'ソースコード', ko: '소스 코드' },
  },
  lang: { zh: '语言', en: 'Language', ja: '言語', ko: '언어' },
  readMore: { zh: '阅读全文', en: 'Read more', ja: '続きを読む', ko: '더 읽기' },
  back: { zh: '返回', en: 'Back', ja: '戻る', ko: '돌아가기' },
} as const;

/** Readers suggest changes to the Chinese and English translations; the Japanese pages
 * show the original text and the Korean ones the translator's (테오's), so neither has
 * suggestion features. The Traditional Chinese pages share the Simplified ones'
 * suggestions: their text is made from it. */
export const canReview = (lang: Lang) => lang !== 'ja' && lang !== 'ko';

/** The title logo drawn for each language (public/brand/title-<lang>.webp, 1400 wide; a
 * -small copy is 480 wide) and the name it shows. One M64 mark serves every language;
 * Korean shows the English wordmark (title-ko is a copy of title-en). */
export const TITLE_LOGO: Record<Lang, { alt: string; h: number }> = {
  zh: { alt: '三月风64', h: 432 },
  tw: { alt: '三月風64', h: 424 },
  en: { alt: 'MARCHWIND 64', h: 403 },
  ja: { alt: 'マーチウィンド64', h: 363 },
  ko: { alt: 'MARCHWIND 64', h: 403 },
};
