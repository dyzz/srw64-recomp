// Scene page controls: which translation shows, the Japanese on or off, and the hero
// filter. Picking a hero hides the other routes' passages and fills in that hero's
// portrait and the {HeroName}-style names. Choices are remembered per browser.

type L3 = { ja: string; zh: string; en: string };
const KEY = 'srw64-story';

// Kept per site language: the English pages' translation choice is not the Chinese pages'.
function load(lang: string): Record<string, string> {
  try { return JSON.parse(localStorage.getItem(`${KEY}-${lang}`) || '{}'); } catch { return {}; }
}
function save(lang: string, v: Record<string, string>) {
  try { localStorage.setItem(`${KEY}-${lang}`, JSON.stringify(v)); } catch { /* private mode */ }
}

export function initStory() {
  const root = document.querySelector<HTMLElement>('[data-story]');
  if (!root) return;
  const lang = root.dataset.lang as keyof L3;
  const names: Record<string, Record<string, L3>> = JSON.parse(root.dataset.names || '{}');
  const saved = load(lang);
  const generic = new Map<HTMLElement, { src: string | null; name: string; ja: string }>();

  const langOf = (el: Element): keyof L3 => {
    const l = el.closest('[lang]')?.getAttribute('lang') || lang;
    return (l.startsWith('zh') ? 'zh' : l.startsWith('ja') ? 'ja' : 'en') as keyof L3;
  };

  function applyRoute(route: string) {
    root!.querySelectorAll<HTMLElement>('[data-routes]').forEach((el) => {
      const routes = el.dataset.routes;
      el.classList.toggle('hidden-route', !!route && !!routes && !routes.split(' ').includes(route));
    });
    root!.querySelectorAll<HTMLElement>('.line[data-cands]').forEach((line) => {
      const cands = JSON.parse(line.dataset.cands!);
      const img = line.querySelector<HTMLImageElement>('img.face');
      const name = line.querySelector<HTMLElement>('.name')!;
      const wja = line.querySelector<HTMLElement>('.wja');
      if (!generic.has(line)) generic.set(line, { src: img?.getAttribute('src') ?? null, name: name.textContent || '', ja: wja?.textContent || '' });
      const pick = route ? cands[route] : null;
      if (pick) {
        name.textContent = pick.who[lang];
        if (wja) wja.textContent = pick.who.ja;
        let face = img;
        if (!face) {
          face = document.createElement('img');
          face.className = 'face'; face.alt = ''; face.width = 160; face.height = 160;
          line.querySelector('.face')!.replaceWith(face);
        }
        if (pick.face != null) face.src = `/gen/portraits/${pick.face}.webp`;
      } else {
        const g = generic.get(line)!;
        name.textContent = g.name;
        if (wja) wja.textContent = g.ja;
        if (img && g.src) img.src = g.src;
      }
    });
    root!.querySelectorAll<HTMLElement>('.ph').forEach((ph) => {
      const n = route ? names[route]?.[ph.dataset.ph!] : null;
      ph.textContent = n ? n[langOf(ph)] : ph.dataset.label!;
    });
  }

  function set(control: string, value: string) {
    root!.querySelectorAll<HTMLButtonElement>(`[data-control="${control}"] button`).forEach((b) => {
      b.setAttribute('aria-pressed', String(b.dataset.value === value));
    });
    if (control === 'tr') root!.dataset.tr = value;
    if (control === 'ja') root!.dataset.ja = value;
    if (control === 'route') applyRoute(value);
    saved[control] = value;
    save(lang, saved);
  }

  root.querySelectorAll<HTMLElement>('[data-control]').forEach((group) => {
    const control = group.dataset.control!;
    const buttons = [...group.querySelectorAll<HTMLButtonElement>('button')];
    buttons.forEach((b) => b.addEventListener('click', () => set(control, b.dataset.value!)));
    const want = saved[control];
    const initial = buttons.find((b) => b.dataset.value === want) ?? buttons.find((b) => b.getAttribute('aria-pressed') === 'true') ?? buttons[0];
    set(control, initial.dataset.value!);
  });

  // A #l<id> link lands on its line even when a route filter would hide it.
  const target = location.hash && document.getElementById(location.hash.slice(1));
  if (target?.classList.contains('hidden-route')) set('route', '');
  if (target) target.scrollIntoView({ block: 'center' });
}
