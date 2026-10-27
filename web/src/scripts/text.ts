// The strings a script draws itself, for the page's language. The build converts the
// Traditional Chinese pages (scripts/zh_hant_pages.py) but not the scripts, so a tw page
// carries its scripts' zh strings in a JSON payload the build converts
// (components/ZhHantTexts.astro); without it, a tw page uses the zh strings.
type TextLang = 'zh' | 'en' | 'ja';

export function texts<T>(table: Record<TextLang, T>, key: string, lang: string): T {
  if (lang !== 'tw') return table[lang as TextLang];
  try {
    const own = JSON.parse(document.getElementById('zh-hant-texts')?.textContent || '{}')[key];
    if (own) return own as T;
  } catch { /* the zh strings below */ }
  return table.zh;
}
