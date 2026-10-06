// The suggestions page: lists by state, the reader's own suggestions with edit,
// withdraw and answer, and the reader's display identity.

const T = {
  zh: {
    status: { pending: '待处理', questioned: '待确认', processed: '已处理', dismissed: '未采纳' }, adopted: '已原样采纳',
    kinds: { mistranslation: '错译', awkward: '不通顺', typo: '错字', naming: '译名不统一', other: '其他' },
    locale: { 'zh-Hans': '中文', en: '英文' }, now: '当前：', proposed: '建议：', reason: '理由：', question: '维护者的问题：',
    note: '说明：', answer: '我的回答：', edit: '修改', del: '撤回', save: '保存', reply: '回答', cancel: '取消',
    confirm: '确定撤回这条意见？', empty: '这里还没有意见。', more: '加载更多', offline: '意见功能暂时不可用，请稍后再试。', line: '台词',
    results: { all: '全部', pending: '待处理', questioned: '待确认', adopted: '已采纳', final: '最终译文', dismissed: '未采纳' },
    verdict: { adopted: '已采纳：你的建议已原样用于译文。', final: '已处理：译文已按维护者的最终方案更新。', dismissed: '未采纳。', questioned: '维护者需要补充信息，请在下方回复。' },
    finalText: '最终译文：', fresh: '新结果', cap: '显示最近 500 条意见。',
  },
  en: {
    status: { pending: 'Pending', questioned: 'Needs clarification', processed: 'Done', dismissed: 'Declined' }, adopted: 'Accepted as proposed',
    kinds: { mistranslation: 'Mistranslation', awkward: 'Awkward wording', typo: 'Typo', naming: 'Inconsistent naming', other: 'Other' },
    locale: { 'zh-Hans': 'Chinese', en: 'English' }, now: 'Current: ', proposed: 'Suggested: ', reason: 'Reason: ', question: 'Maintainer’s question: ',
    note: 'Note: ', answer: 'My answer: ', edit: 'Edit', del: 'Withdraw', save: 'Save', reply: 'Answer', cancel: 'Cancel',
    confirm: 'Withdraw this suggestion?', empty: 'No suggestions here yet.', more: 'Load more', offline: 'Suggestions are temporarily unavailable. Please try again later.', line: 'line',
    results: { all: 'All', pending: 'Pending', questioned: 'Needs clarification', adopted: 'Accepted', final: 'Final wording', dismissed: 'Declined' },
    verdict: { adopted: 'Accepted: your suggested wording has been used as written.', final: 'Done: the translation has been updated to the maintainers’ final wording.', dismissed: 'Declined.', questioned: 'The maintainers need more information. Please reply below.' },
    finalText: 'Final wording: ', fresh: 'New', cap: 'Showing the latest 500 suggestions.',
  },
  ja: {
    status: { pending: '未対応', questioned: '確認待ち', processed: '対応済み', dismissed: '見送り' }, adopted: '提案どおり採用',
    kinds: { mistranslation: '誤訳', awkward: '不自然な表現', typo: '誤字', naming: '表記揺れ', other: 'その他' },
    locale: { 'zh-Hans': '中国語', en: '英語' }, now: '現在：', proposed: '提案：', reason: '理由：', question: '管理者からの質問：',
    note: '補足：', answer: '自分の回答：', edit: '修正', del: '取り消す', save: '保存', reply: '回答する', cancel: 'キャンセル',
    confirm: 'この意見を取り消しますか？', empty: 'まだ意見はありません。', more: 'さらに読み込む', offline: '現在、意見機能を利用できません。時間をおいてお試しください。', line: '行',
    results: { all: 'すべて', pending: '未対応', questioned: '確認待ち', adopted: '採用', final: '最終訳', dismissed: '見送り' },
    verdict: { adopted: '採用：提案どおりの訳が反映されました。', final: '対応済み：管理者の最終案を訳文に反映しました。', dismissed: '見送りになりました。', questioned: '管理者から確認事項があります。下で回答してください。' },
    finalText: '最終訳：', fresh: '新着', cap: '最新 500 件の意見を表示しています。',
  },
};
import { markResultsSeen, onMeChange, unseenResults } from './identity';

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

  let fresh = new Set<number>();
  function card(x: any): HTMLElement {
    const li = el('li');
    if (fresh.has(x.id)) li.classList.add('fresh');
    const top = el('div', 'top');
    top.append(link(x), el('span', '', t.locale[x.locale as 'en'] ?? x.locale), el('span', '', (t.kinds as any)[x.kind] ?? x.kind));
    const st = el('span', `sg-status st-${x.status}`, t.status[x.status as 'pending'] + (x.adopted ? ` · ${t.adopted}` : ''));
    top.append(st);
    const by = el('span');
    if (x.author.avatar) { const img = el('img', 'sg-avatar'); img.src = x.author.avatar; img.alt = ''; by.append(img, ' '); }
    by.append(x.author.name);
    top.append(by, el('span', '', new Date(x.created_at).toLocaleDateString(lang === 'zh' ? 'zh-CN' : lang)));
    li.append(top);
    if (x.mine && x.result !== 'pending') {
      const v = el('p', `verdict r-${x.result}`, (t.verdict as Record<string, string>)[x.result] ?? '');
      if (fresh.has(x.id)) v.prepend(el('span', 'new', t.fresh));
      li.append(v);
    }
    if (x.target_type === 'dialogue') li.append(el('div', 'src', x.source));
    const line = (k: string, cls: string, text: string) => { const p = el('div', cls); p.append(el('span', 'k', k), text); return p; };
    if (x.proposed) { li.append(line(t.now, 'cur', x.current), line(t.proposed, 'pro', x.proposed)); }
    else li.append(line(t.now, '', x.current));
    if (x.status === 'processed' && x.live && x.live !== x.current && !x.adopted) li.append(line(t.finalText, 'live', x.live));
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

  // "Mine" loads all of the reader's (latest 500) at once and filters by result here.
  const filters = root.querySelector<HTMLElement>('[data-results]')!;
  let result = 'all';
  async function loadMine() {
    list.replaceChildren();
    more.hidden = true;
    try {
      const [data, all] = await Promise.all([api(`suggestions?mine=1&lang=${lang}&limit=500`), api(`suggestions?status=pending&lang=${lang}&limit=1`)]);
      for (const [k, n] of Object.entries(all.counts)) {
        const c = root!.querySelector<HTMLElement>(`[data-count="${k}"]`);
        if (c) c.textContent = String(n);
      }
      fresh = unseenResults(data.items);
      markResultsSeen(data.items);
      root!.querySelector<HTMLElement>('[data-count="mine"]')!.textContent = String(data.total);
      filters.replaceChildren();
      for (const [k, label] of Object.entries(t.results)) {
        const n = k === 'all' ? data.items.length : data.items.filter((x: any) => x.result === k).length;
        const b = el('button', 'mini', `${label} ${n}`);
        b.type = 'button';
        b.setAttribute('aria-pressed', String(k === result));
        b.addEventListener('click', () => { result = k; loadMine(); });
        filters.append(b);
      }
      const shown = data.items.filter((x: any) => result === 'all' || x.result === result);
      if (!shown.length) list.append(el('li', 'sg-note', t.empty));
      shown.forEach((x: any) => list.append(card(x)));
      if (data.total >= 500) list.append(el('li', 'sg-note', t.cap));
    } catch {
      list.replaceChildren(el('li', 'sg-note', t.offline));
    }
  }

  async function load(reset = false) {
    filters.hidden = tab !== 'mine';
    if (tab === 'mine') return loadMine();
    if (reset) { offset = 0; list.replaceChildren(); }
    const q = `status=${tab}`;
    try {
      const data = await api(`suggestions?${q}&lang=${lang}&offset=${offset}&limit=${PAGE}`);
      for (const [k, n] of Object.entries(data.counts)) {
        const c = root!.querySelector<HTMLElement>(`[data-count="${k}"]`);
        if (c) c.textContent = String(n);
      }
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
  // Names on the list follow the identity.
  onMeChange(() => load(true));
  load(true);
}
