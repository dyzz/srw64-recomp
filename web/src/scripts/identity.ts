// The reader's display identity, as on the SRW Z site: a character's name and face drawn
// at random, one picked from the list, or a nickname with a nameless soldier's face.
// Used on the suggestions page and inside every suggestion panel.

export type Me = { name: string; avatar: string | null; mode: 'pilot' | 'custom'; pilot_id: number | null };
type Pilot = { id: number; series: number | null; name: string; ja: string; avatar: string };

const T = {
  zh: {
    none: '尚未设置署名。首次提交意见时会随机分配一个机战人物，也可以现在选择。',
    reroll: '随机换一个', pick: '选择人物', nick: '用昵称', save: '保存', cancel: '收起', change: '更改',
    nickPh: '昵称（最多 20 字）', search: '搜索人物名', loading: '载入中…', failed: '没能更改：', errors: { banned: '这个身份已被停止使用。', 'name reserved': '这个昵称不能使用，请换一个。', 'too many requests': '操作太频繁，请稍后再试。' } as Record<string, string>, none2: '没有找到符合的人物。',
    other: '其他', you: '你的署名',
  },
  en: {
    none: 'No display name yet. Your first suggestion will use a random SRW character, or you can choose one now.',
    reroll: 'Randomise', pick: 'Choose a character', nick: 'Use a nickname', save: 'Save', cancel: 'Close', change: 'Change',
    nickPh: 'Nickname (up to 20 characters)', search: 'Search by name', loading: 'Loading…', failed: 'Could not change: ', errors: { banned: 'This identity has been disabled.', 'name reserved': 'That nickname cannot be used. Please choose another.', 'too many requests': 'Too many changes. Please try again in a minute.' } as Record<string, string>, none2: 'No matching characters.',
    other: 'Other', you: 'Your display name',
  },
  ja: {
    none: '表示名はまだ設定されていません。初めて意見を送るときにスパロボのキャラクターがランダムで割り当てられます。今選ぶこともできます。',
    reroll: 'ランダムに変える', pick: 'キャラクターを選ぶ', nick: 'ニックネームにする', save: '保存', cancel: '閉じる', change: '変更',
    nickPh: 'ニックネーム（20 文字まで）', search: '名前で検索', loading: '読み込み中…', failed: '変更できませんでした：', errors: { banned: 'この ID は利用できなくなりました。', 'name reserved': 'このニックネームは使えません。別のものにしてください。', 'too many requests': '操作が多すぎます。しばらくしてからお試しください。' } as Record<string, string>, none2: '該当するキャラクターが見つかりません。',
    other: 'その他', you: 'あなたの表示名',
  },
};
type Lang = keyof typeof T;

async function api(path: string, init?: RequestInit) {
  const r = await fetch(`/api/${path}`, { credentials: 'same-origin', headers: { 'Content-Type': 'application/json' }, ...init });
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(data.error || String(r.status));
  return data;
}
function el<K extends keyof HTMLElementTagNameMap>(tag: K, cls?: string, text?: string) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  return e;
}

// One fetch per page, shared by every widget on it.
let mePromise: Promise<Me | null> | null = null;
let pilotsPromise: Promise<{ series: { id: number | null; name: string }[]; pilots: Pilot[] }> | null = null;
const listeners = new Set<(me: Me | null) => void>();

export function loadMe(lang: string): Promise<Me | null> {
  mePromise ??= api(`me?lang=${lang}`).then((x) => x.participant as Me | null);
  return mePromise;
}
function announce(me: Me) {
  mePromise = Promise.resolve(me);
  listeners.forEach((f) => f(me));
}
/** Fetches the identity again, e.g. after a first suggestion created it. */
export async function reloadMe(lang: string) {
  mePromise = null;
  const me = await loadMe(lang);
  listeners.forEach((f) => f(me));
  return me;
}
export const onMeChange = (f: (me: Me | null) => void) => { listeners.add(f); return () => listeners.delete(f); };

/** Fills `box` with the identity and its controls. `compact` hides the controls behind a Change button. */
export function mountIdentity(box: HTMLElement, lang: Lang, opts: { compact?: boolean } = {}) {
  const t = T[lang];
  box.classList.add('id-box');
  box.replaceChildren();
  const head = el('div', 'id-head');
  const who = el('span', 'id-who');
  const actions = el('div', 'id-actions');
  const reroll = el('button', 'id-btn', t.reroll);
  const pick = el('button', 'id-btn', t.pick);
  const nick = el('button', 'id-btn', t.nick);
  [reroll, pick, nick].forEach((b) => { b.type = 'button'; });
  actions.append(reroll, pick, nick);
  const toggle = el('button', 'id-btn id-toggle', t.change);
  toggle.type = 'button';
  head.append(el('span', 'id-lbl', t.you), who);
  if (opts.compact) head.append(toggle);
  box.append(head, actions);
  const drawer = el('div', 'id-drawer');
  const msg = el('p', 'id-msg');
  box.append(drawer, msg);
  actions.hidden = !!opts.compact;
  toggle.addEventListener('click', () => {
    actions.hidden = !actions.hidden;
    toggle.textContent = actions.hidden ? t.change : t.cancel;
    if (actions.hidden) drawer.replaceChildren();
  });

  function show(me: Me | null) {
    who.replaceChildren();
    if (!me) { who.append(el('span', 'id-none', t.none)); return; }
    if (me.avatar) { const img = el('img'); img.src = me.avatar; img.alt = ''; who.append(img); }
    who.append(el('strong', '', me.name));
  }
  async function change(body: object) {
    msg.textContent = '';
    try {
      const { participant } = await api(`me?lang=${lang}`, { method: 'POST', body: JSON.stringify(body) });
      announce(participant);
      drawer.replaceChildren();
    } catch (e) { msg.textContent = t.failed + (t.errors[(e as Error).message] ?? (e as Error).message); }
  }

  reroll.addEventListener('click', () => change({ mode: 'pilot' }));
  nick.addEventListener('click', () => {
    if (drawer.querySelector('.id-nick')) { drawer.replaceChildren(); return; }
    // Not a <form>: the widget sits inside the suggestion form.
    const form = el('div', 'id-nick');
    const input = el('input');
    input.type = 'text'; input.maxLength = 20; input.placeholder = t.nickPh; input.setAttribute('aria-label', t.nick);
    const save = el('button', 'id-btn', t.save);
    save.type = 'button';
    form.append(input, save);
    const submit = () => { if (input.value.trim()) change({ mode: 'custom', name: input.value }); };
    save.addEventListener('click', submit);
    input.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); submit(); } });
    drawer.replaceChildren(form);
    input.focus();
  });
  pick.addEventListener('click', async () => {
    if (drawer.querySelector('.id-pick')) { drawer.replaceChildren(); return; }
    const wrap = el('div', 'id-pick');
    const search = el('input');
    search.type = 'search'; search.placeholder = t.search; search.setAttribute('aria-label', t.search);
    search.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') ev.preventDefault(); });
    const grid = el('div', 'id-grid', t.loading);
    wrap.append(search, grid);
    drawer.replaceChildren(wrap);
    try {
      pilotsPromise ??= api(`me/pilots?lang=${lang}`);
      const { series, pilots } = await pilotsPromise;
      const seriesName = new Map(series.map((s) => [s.id, s.name]));
      const current = (await loadMe(lang))?.pilot_id;
      const render = () => {
        const q = search.value.trim().toLowerCase();
        const hits = pilots.filter((p) => !q || p.name.toLowerCase().includes(q) || p.ja.includes(q));
        grid.replaceChildren();
        if (!hits.length) { grid.append(el('p', 'id-msg', t.none2)); return; }
        let last: number | null | undefined;
        for (const p of hits) {
          if (p.series !== last) { grid.append(el('h4', '', seriesName.get(p.series) ?? t.other)); last = p.series; }
          const b = el('button', 'id-face');
          b.type = 'button';
          if (p.id === current) b.setAttribute('aria-current', 'true');
          const img = el('img'); img.src = p.avatar; img.alt = ''; img.loading = 'lazy'; img.width = 56; img.height = 56;
          b.append(img, el('span', '', p.name));
          b.addEventListener('click', () => change({ mode: 'pilot', pilot_id: p.id }));
          grid.append(b);
        }
      };
      search.addEventListener('input', render);
      render();
      search.focus();
    } catch (e) { grid.textContent = t.failed + (e as Error).message; }
  });

  onMeChange(show);
  loadMe(lang).then(show, () => { box.hidden = true; });
}

// --- Results of the reader's own suggestions ----------------------------------
// A suggestion has a result once it is questioned, processed or declined. The header
// marks results the reader has not looked at yet; opening "Mine" marks them seen.
type Result = { id: number; result: string };
const SEEN = 'srw64-seen-results';
const seen = (): Record<string, string> => { try { return JSON.parse(localStorage.getItem(SEEN) || '{}'); } catch { return {}; } };
const unseenListeners = new Set<(n: number) => void>();
let mine: Result[] = [];

function unseenCount() {
  const s = seen();
  return mine.filter((x) => x.result !== 'pending' && s[x.id] !== x.result).length;
}
export async function loadMyResults(lang: string) {
  try { mine = (await api(`suggestions?mine=1&lang=${lang}&limit=500`)).items; } catch { mine = []; }
  unseenListeners.forEach((f) => f(unseenCount()));
}
/** Remembers every current result as seen. */
export function markResultsSeen(items: Result[]) {
  const s = seen();
  for (const x of items) s[x.id] = x.result;
  try { localStorage.setItem(SEEN, JSON.stringify(s)); } catch { /* private mode */ }
  mine = items;
  unseenListeners.forEach((f) => f(unseenCount()));
}
export const unseenResults = (items: Result[]) => { const s = seen(); return new Set(items.filter((x) => x.result !== 'pending' && s[x.id] !== x.result).map((x) => x.id)); };

const CHIP = {
  zh: { setup: '署名设置', news: (n: number) => `${n} 条意见有了处理结果`, mine: '我的意见', title: '提交意见时显示的名字和头像' },
  en: { setup: 'Display name', news: (n: number) => `Suggestion results: ${n}`, mine: 'My suggestions', title: 'Name and avatar shown with your suggestions' },
  ja: { setup: '表示名', news: (n: number) => `${n} 件の意見に対応結果があります`, mine: '自分の意見', title: '意見に表示される名前とアイコン' },
};

/** The header's identity button: face and name, a dot for unseen results, and a popover with the controls. */
export function mountIdentityChip(host: HTMLElement, lang: Lang) {
  const c = CHIP[lang];
  const btn = el('button', 'idc-btn');
  btn.type = 'button';
  btn.title = c.title;
  btn.setAttribute('aria-expanded', 'false');
  const dot = el('span', 'idc-dot');
  dot.hidden = true;
  const pop = el('div', 'idc-pop');
  pop.hidden = true;
  const news = el('a', 'idc-news');
  news.href = `/${lang}/reviews/#mine`;
  news.hidden = true;
  const body = el('div');
  const foot = el('a', 'idc-mine', c.mine);
  foot.href = `/${lang}/reviews/#mine`;
  pop.append(news, body, foot);
  host.append(btn, pop);
  mountIdentity(body, lang);

  const show = (me: Me | null) => {
    btn.replaceChildren();
    if (me?.avatar) { const img = el('img'); img.src = me.avatar; img.alt = ''; btn.append(img); }
    btn.append(el('span', 'idc-name', me ? me.name : c.setup), dot);
  };
  onMeChange(show);
  loadMe(lang).then(show, () => { host.hidden = true; });
  unseenListeners.add((n) => {
    dot.hidden = n === 0;
    news.hidden = n === 0;
    news.textContent = c.news(n);
  });
  loadMyResults(lang);

  const open = (v: boolean) => { pop.hidden = !v; btn.setAttribute('aria-expanded', String(v)); };
  btn.addEventListener('click', () => open(pop.hidden));
  document.addEventListener('click', (ev) => { if (!host.contains(ev.target as Node)) open(false); });
  document.addEventListener('keydown', (ev) => { if (ev.key === 'Escape') open(false); });
}
