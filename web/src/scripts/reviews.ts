// The suggestions page: lists by state, the reader's own suggestions with edit,
// withdraw and answer, and the reader's display identity.

const T = {
  zh: {
    status: { pending: '待处理', questioned: '有疑问', processed: '已处理', dismissed: '不处理' }, adopted: '已采纳原句',
    kinds: { mistranslation: '错译', awkward: '不通顺', typo: '错字', naming: '译名不统一', other: '其他' },
    locale: { 'zh-Hans': '中文', en: '英文' }, now: '现在：', proposed: '建议：', reason: '理由：', question: '维护者的疑问：',
    note: '说明：', answer: '我的回答：', edit: '修改', del: '撤回', save: '保存', reply: '回答', cancel: '取消',
    confirm: '确定撤回这条意见？', empty: '这里还没有意见。', more: '加载更多', offline: '意见功能暂时不可用。', line: '台词',
  },
  en: {
    status: { pending: 'Pending', questioned: 'Question', processed: 'Done', dismissed: 'Declined' }, adopted: 'adopted as proposed',
    kinds: { mistranslation: 'Mistranslation', awkward: 'Awkward', typo: 'Typo', naming: 'Inconsistent name', other: 'Other' },
    locale: { 'zh-Hans': 'Chinese', en: 'English' }, now: 'Now: ', proposed: 'Proposal: ', reason: 'Reason: ', question: 'Question: ',
    note: 'Note: ', answer: 'My answer: ', edit: 'Edit', del: 'Withdraw', save: 'Save', reply: 'Answer', cancel: 'Cancel',
    confirm: 'Withdraw this suggestion?', empty: 'Nothing here yet.', more: 'Load more', offline: 'Suggestions are unavailable right now.', line: 'line',
  },
  ja: {
    status: { pending: '未対応', questioned: '質問中', processed: '対応済み', dismissed: '見送り' }, adopted: '提案どおり採用',
    kinds: { mistranslation: '誤訳', awkward: '不自然', typo: '誤字', naming: '表記揺れ', other: 'その他' },
    locale: { 'zh-Hans': '中国語', en: '英語' }, now: '現在：', proposed: '提案：', reason: '理由：', question: '管理者の質問：',
    note: '補足：', answer: '自分の回答：', edit: '修正', del: '取り消す', save: '保存', reply: '回答する', cancel: 'キャンセル',
    confirm: 'この意見を取り消しますか？', empty: 'まだ意見はありません。', more: 'さらに読み込む', offline: '意見機能は現在利用できません。', line: '行',
  },
};
const PAGE = 30;

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

export function initReviews() {
  const root = document.querySelector<HTMLElement>('[data-reviews]');
  if (!root) return;
  const lang = root.dataset.lang as keyof typeof T;
  const t = T[lang];
  const list = root.querySelector<HTMLElement>('[data-items]')!;
  const more = root.querySelector<HTMLButtonElement>('[data-more]')!;
  let tab = location.hash === '#mine' ? 'mine' : 'pending';
  let offset = 0;

  async function identity() {
    try {
      const { participant } = await api(`me?lang=${lang}`);
      if (!participant) return;
      const who = root!.querySelector<HTMLElement>('[data-who]')!;
      who.textContent = '';
      if (participant.avatar) { const img = el('img'); img.src = participant.avatar; img.alt = ''; who.append(img); }
      who.append(participant.name);
      root!.querySelector<HTMLElement>('[data-reroll]')!.hidden = false;
      root!.querySelector<HTMLElement>('[data-nick]')!.hidden = false;
    } catch { /* offline: the default text stays */ }
  }
  root.querySelector('[data-reroll]')!.addEventListener('click', async () => {
    await api(`me?lang=${lang}`, { method: 'POST', body: JSON.stringify({ mode: 'pilot' }) });
    identity(); load(true);
  });
  root.querySelector<HTMLFormElement>('[data-nick]')!.addEventListener('submit', async (ev) => {
    ev.preventDefault();
    const input = (ev.target as HTMLFormElement).querySelector('input')!;
    if (!input.value.trim()) return;
    await api(`me?lang=${lang}`, { method: 'POST', body: JSON.stringify({ mode: 'custom', name: input.value }) });
    input.value = '';
    identity(); load(true);
  });

  function link(x: any): HTMLElement {
    if (x.target_type === 'dialogue') {
      const id = Number(x.target_id.slice(9));
      const a = el('a', '', `${x.context.scene_title ?? ''} · ${t.line} @${id}`);
      a.href = `/${lang}/story/${x.context.scene}/#l${id}`;
      return a;
    }
    const a = el('a', '', x.source);
    a.href = x.context.href || `/${lang}/library/`;
    return a;
  }

  function card(x: any): HTMLElement {
    const li = el('li');
    const top = el('div', 'top');
    top.append(link(x), el('span', '', t.locale[x.locale as 'en'] ?? x.locale), el('span', '', (t.kinds as any)[x.kind] ?? x.kind));
    const st = el('span', `sg-status st-${x.status}`, t.status[x.status as 'pending'] + (x.adopted ? ` · ${t.adopted}` : ''));
    top.append(st);
    const by = el('span');
    if (x.author.avatar) { const img = el('img', 'sg-avatar'); img.src = x.author.avatar; img.alt = ''; by.append(img, ' '); }
    by.append(x.author.name);
    top.append(by, el('span', '', new Date(x.created_at).toLocaleDateString(lang === 'zh' ? 'zh-CN' : lang)));
    li.append(top);
    if (x.target_type === 'dialogue') li.append(el('div', 'src', x.source));
    const line = (k: string, cls: string, text: string) => { const p = el('div', cls); p.append(el('span', 'k', k), text); return p; };
    if (x.proposed) { li.append(line(t.now, 'cur', x.current), line(t.proposed, 'pro', x.proposed)); }
    else li.append(line(t.now, '', x.current));
    if (x.status === 'processed' && x.live && x.live !== x.current && !x.adopted) li.append(line('→ ', 'live', x.live));
    if (x.reason) li.append(line(t.reason, '', x.reason));
    if (x.admin_reason) li.append(line(x.status === 'questioned' ? t.question : t.note, 'adm', x.admin_reason));
    if (x.question_response) li.append(line(t.answer, '', x.question_response));
    if (x.mine) {
      const acts = el('div', 'acts');
      if (x.status === 'pending') {
        const edit = el('button', 'mini', t.edit); edit.type = 'button';
        edit.addEventListener('click', () => {
          const box = el('textarea'); box.rows = 3; box.value = x.proposed || x.current;
          const save = el('button', 'mini', t.save); save.type = 'button';
          save.addEventListener('click', async () => { await api(`suggestions?lang=${lang}`, { method: 'PATCH', body: JSON.stringify({ id: x.id, proposed: box.value }) }); load(true); });
          acts.replaceChildren(box, save);
        });
        acts.append(edit);
      }
      if (x.status === 'questioned') {
        const reply = el('button', 'mini', t.reply); reply.type = 'button';
        reply.addEventListener('click', () => {
          const box = el('textarea'); box.rows = 3; box.value = x.question_response || '';
          const save = el('button', 'mini', t.save); save.type = 'button';
          save.addEventListener('click', async () => { await api(`suggestions?lang=${lang}`, { method: 'PATCH', body: JSON.stringify({ id: x.id, answer: box.value }) }); load(true); });
          acts.replaceChildren(box, save);
        });
        acts.append(reply);
      }
      if (x.status !== 'processed') {
        const del = el('button', 'mini', t.del); del.type = 'button';
        del.addEventListener('click', async () => { if (confirm(t.confirm)) { await api(`suggestions?id=${x.id}`, { method: 'DELETE' }); load(true); } });
        acts.append(del);
      }
      li.append(acts);
    }
    return li;
  }

  async function load(reset = false) {
    if (reset) { offset = 0; list.replaceChildren(); }
    const q = tab === 'mine' ? 'mine=1' : `status=${tab}`;
    try {
      const data = await api(`suggestions?${q}&lang=${lang}&offset=${offset}&limit=${PAGE}`);
      for (const [k, n] of Object.entries(data.counts)) {
        const c = root!.querySelector<HTMLElement>(`[data-count="${k}"]`);
        if (c && tab !== 'mine') c.textContent = String(n);
      }
      if (tab === 'mine') root!.querySelector<HTMLElement>('[data-count="mine"]')!.textContent = String(data.total);
      if (!data.items.length && offset === 0) list.append(el('li', 'sg-note', t.empty));
      data.items.forEach((x: any) => list.append(card(x)));
      offset += data.items.length;
      more.hidden = offset >= data.total;
      more.textContent = t.more;
    } catch {
      list.replaceChildren(el('li', 'sg-note', t.offline));
    }
  }

  root.querySelectorAll<HTMLButtonElement>('[data-tab]').forEach((b) => {
    b.setAttribute('aria-selected', String(b.dataset.tab === tab));
    b.addEventListener('click', () => {
      tab = b.dataset.tab!;
      root!.querySelectorAll('[data-tab]').forEach((x) => x.setAttribute('aria-selected', String(x === b)));
      load(true);
    });
  });
  more.addEventListener('click', () => load());
  identity();
  load(true);
}
