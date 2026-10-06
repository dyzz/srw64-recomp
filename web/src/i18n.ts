// The three site languages. `code` is the URL prefix, `tag` the BCP 47 tag
// (and the name of the repository's content/<tag>/ folders).
export const LANGS = [
  { code: 'zh', tag: 'zh-Hans', label: '简体中文', short: '中' },
  { code: 'en', tag: 'en', label: 'English', short: 'EN' },
  { code: 'ja', tag: 'ja', label: '日本語', short: '日' },
] as const;

export type Lang = (typeof LANGS)[number]['code'];
export const DEFAULT_LANG: Lang = 'zh';

export const langPaths = () => LANGS.map((l) => ({ params: { lang: l.code } }));
export const langInfo = (lang: Lang) => LANGS.find((l) => l.code === lang)!;

/** Pick the string for `lang` out of a { zh, en, ja } record. */
export const pick = <T>(lang: Lang, v: Record<Lang, T>): T => v[lang];

/** Same page in another language: swaps the first path segment. */
export const switchLang = (path: string, to: Lang) =>
  path.replace(/^\/(zh|en|ja)(?=\/|$)/, `/${to}`) || `/${to}/`;

export const UI = {
  nav: {
    home: { zh: '首页', en: 'Home', ja: 'ホーム' },
    install: { zh: '安装', en: 'Install', ja: 'インストール' },
    faq: { zh: '常见问题', en: 'FAQ', ja: 'よくある質問' },
    blog: { zh: '博客', en: 'Blog', ja: 'ブログ' },
    docs: { zh: '技术文档', en: 'Docs', ja: '技術資料' },
    guide: { zh: '攻略', en: 'Guide', ja: '攻略' },
    story: { zh: '剧情', en: 'Story', ja: 'シナリオ' },
    library: { zh: '图鉴', en: 'Library', ja: '図鑑' },
    reviews: { zh: '意见', en: 'Suggestions', ja: '意見' },
    mods: { zh: 'MOD', en: 'Mods', ja: 'MOD' },
  },
  // The project is MARCHWIND 64; the game it recompiles is named in descriptions.
  brand: 'MARCHWIND 64',
  title: {
    zh: '《超级机器人大战64》非官方原生重编译版',
    en: 'An unofficial native recompilation of Super Robot Wars 64',
    ja: '『スーパーロボット大戦64』非公式ネイティブ再コンパイル版',
  },
  description: {
    zh: 'Marchwind 64 是《超级机器人大战64》的非官方原生重编译版，可在 Windows、macOS、Linux、Steam Deck 和 Android 上直接运行，提供中英文全文翻译。另有可选的 HD 美术包。使用时需自备日版 ROM。',
    en: 'Marchwind 64 is an unofficial native recompilation of Super Robot Wars 64 for Windows, macOS, Linux, Steam Deck and Android, with full English and Chinese translations. An optional HD art pack is available on every platform. You’ll need your own Japanese ROM.',
    ja: 'マーチウィンド64 は『スーパーロボット大戦64』の非公式ネイティブ再コンパイル版です。Windows・macOS・Linux・Steam Deck・Android で動作し、英語・中国語の全文翻訳を用意しています。別配布の HD アートパックも、すべてのプラットフォームで利用できます。日本版 ROM は各自でご用意ください。',
  },
  footer: {
    unofficial: {
      zh: '非官方粉丝项目，与万代南梦宫、BANPRESTO 及各作品权利人无隶属关系。原作角色、美术与商标归各自权利人所有。本站不提供 ROM。',
      en: 'An unofficial fan project, unaffiliated with Bandai Namco, BANPRESTO or the rights holders of the featured series. Original characters, art and trademarks belong to their respective owners. This site does not provide ROMs.',
      ja: '非公式のファンプロジェクトです。バンダイナムコ、バンプレスト、各作品の権利者とは提携していません。原作のキャラクター・アート・商標は各権利者に帰属します。当サイトでは ROM を配布していません。',
    },
    source: { zh: '源代码', en: 'Source code', ja: 'ソースコード' },
  },
  lang: { zh: '语言', en: 'Language', ja: '言語' },
  readMore: { zh: '阅读全文', en: 'Read more', ja: '続きを読む' },
  back: { zh: '返回', en: 'Back', ja: '戻る' },
} as const;

/** Readers suggest changes to the Chinese and English translations; the Japanese pages
 * show the original text, so they have no suggestion features. */
export const canReview = (lang: Lang) => lang !== 'ja';

/** The title logo drawn for each language (public/brand/title-<lang>.webp, 1400 wide; a
 * -small copy is 480 wide) and the name it shows. One M64 mark serves every language. */
export const TITLE_LOGO: Record<Lang, { alt: string; h: number }> = {
  zh: { alt: '三月风64', h: 432 },
  en: { alt: 'MARCHWIND 64', h: 403 },
  ja: { alt: 'マーチウィンド64', h: 355 },
};
