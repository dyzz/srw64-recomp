// Suggestions on story lines and Library names (docs/design/website.md §5.6).
// Every element with data-suggest="<id>" gets a panel: the suggestions already made on
// that target, and a form to add one. Talks to the site API (web/api/server.mjs).
//
// Story pages: the target is a dialogue line, `base:t00_<id>`, in the translation the
// page is showing. Library pages: data-target-type / data-target-id on the button.

type Item = {
  id: number; target_type: string; target_id: string; locale: string; kind: string;
  current: string; proposed: string; reason: string; created_at: string;
  author: { name: string; avatar: string | null }; mine: boolean;
  status: 'pending' | 'questioned' | 'processed' | 'dismissed'; adopted?: boolean; hidden?: boolean;
  admin_reason?: string; question_response?: string;
};

const T = {
  zh: {
    kinds: { mistranslation: '错译', awkward: '不通顺', typo: '错字', naming: '译名不统一', other: '其他' },
    status: { pending: '待处理', questioned: '待确认', processed: '已处理', dismissed: '未采纳' },
    adopted: '已采纳', proposal: '建议', reason: '理由', reasonPh: '修改内容与理由（可选）',
    proposedLabel: '建议译文', submit: '提交意见', sent: '已提交，谢谢！', anon: '匿名',
    none: '这一句还没有公开的意见。', offline: '意见功能暂时不可用，请稍后再试。', failed: '提交失败：', me: '我的意见', hiddenNote: '内容已被管理员隐藏，只有你能看到这条意见。',
    errors: { banned: '这个身份已被停止提交意见。', 'name reserved': '这个昵称不能使用，请换一个。', 'too many requests': '提交太频繁，请稍后再试。' } as Record<string, string>,
    hint: '无需注册。提交后只有你和维护者能看到，处理后才公开；可在「我的意见」中修改、撤回或查看处理结果。', as: '署名：{}（右上角可更改）', asNew: '首次提交时会随机分配一个机战人物作为署名，可在右上角更改。', question: '维护者的问题', close: '收起',
    empty: '请填写建议译文或修改理由，至少一项。', same: '建议译文与当前译文相同。',
  },
  en: {
    kinds: { mistranslation: 'Mistranslation', awkward: 'Awkward wording', typo: 'Typo', naming: 'Inconsistent naming', other: 'Other' },
    status: { pending: 'Pending', questioned: 'Needs clarification', processed: 'Done', dismissed: 'Declined' },
    adopted: 'Accepted', proposal: 'Suggestion', reason: 'Reason', reasonPh: 'What to change and why (optional)',
    proposedLabel: 'Suggested translation', submit: 'Send suggestion', sent: 'Suggestion sent. Thank you!', anon: 'anonymous',
    none: 'No public suggestions on this line yet.', offline: 'Suggestions are temporarily unavailable. Please try again later.', failed: 'Could not send: ', me: 'My suggestions', hiddenNote: 'The maintainers have hidden its words; only you see this suggestion.',
    errors: { banned: 'This identity can no longer send suggestions.', 'name reserved': 'That nickname cannot be used. Please choose another.', 'too many requests': 'Too many requests. Please try again in a minute.' } as Record<string, string>,
    hint: 'No account needed. Only you and the maintainers see a suggestion until it is handled; then it is public. Use My suggestions to edit or withdraw it and check its result.', as: 'Display name: {} (change it at the top right)', asNew: 'Your first suggestion uses a randomly assigned SRW character as your display name. You can change it at the top right.', question: 'Maintainer’s question', close: 'Close',
    empty: 'Enter a suggested translation, a reason, or both.', same: 'The suggested translation matches the current text.',
  },
  ja: {
    kinds: { mistranslation: '誤訳', awkward: '不自然な表現', typo: '誤字', naming: '表記揺れ', other: 'その他' },
    status: { pending: '未対応', questioned: '確認待ち', processed: '対応済み', dismissed: '見送り' },
    adopted: '採用', proposal: '提案', reason: '理由', reasonPh: '修正したい点と理由（任意）',
    proposedLabel: '修正案', submit: '意見を送る', sent: '送信しました。ありがとうございます！', anon: '匿名',
    none: 'この行への公開された意見はまだありません。', offline: '現在、意見機能を利用できません。時間をおいてお試しください。', failed: '送信できませんでした：', me: '自分の意見', hiddenNote: '内容は管理者により非表示になりました。この意見はあなたにだけ表示されています。',
    errors: { banned: 'この ID からは意見を送信できなくなりました。', 'name reserved': 'このニックネームは使えません。別のものにしてください。', 'too many requests': '送信が多すぎます。しばらくしてからお試しください。' } as Record<string, string>,
    hint: '登録は不要です。送信した意見は対応されるまであなたと管理者だけが見られ、対応後に公開されます。「自分の意見」で修正・取り消しや結果の確認ができます。', as: '表示名：{}（右上で変更できます）', asNew: '初めて意見を送るときに、スパロボのキャラクター名がランダムで割り当てられます。右上で変更できます。', question: '管理者からの質問', close: '閉じる',
    empty: '修正案か理由のどちらかを入力してください。', same: '修正案が現在の訳と同じです。',
  },
};

import { loadMe, loadMyResults, onMeChange, reloadMe, type Me } from './identity';

const LOCALE: Record<string, string> = { zh: 'zh-Hans', en: 'en', ja: 'ja' };

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

export function initSuggestions() {
  const root = document.querySelector<HTMLElement>('[data-story], [data-library]');
  // Japanese pages have no suggestion buttons (see canReview).
  if (!root || !root.querySelector('[data-suggest]')) return;
  const lang = (root.dataset.lang || 'zh') as keyof typeof T;
  const t = T[lang];
  const scene = root.dataset.scene;
  let items: Item[] = [];
  let online = true;

  // The translation being reviewed: on story pages the one showing, else the page's.
  const reviewedLocale = () => LOCALE[root.dataset.tr && root.dataset.tr !== 'none' ? root.dataset.tr : lang === 'ja' ? 'en' : lang];
  const targetOf = (b: HTMLElement) => ({
    type: b.dataset.targetType || 'dialogue',
    id: b.dataset.targetId || `base:t00_${String(b.dataset.suggest).padStart(5, '0')}`,
  });

  function badges() {
    root!.querySelectorAll<HTMLElement>('[data-suggest]').forEach((b) => {
      const { type, id } = targetOf(b);
      const n = items.filter((x) => x.target_type === type && x.target_id === id && x.locale === reviewedLocale()).length;
      if (n) b.dataset.count = String(n); else delete b.dataset.count;
    });
    // Story pages count them in the header and filter by them.
    root!.dispatchEvent(new CustomEvent('suggestions', { detail: { items, online } }));
  }

  async function refresh() {
    try {
      const q = scene ? `scene=${scene}` : `target_type=${encodeURIComponent(root!.dataset.targetType || '')}&target_prefix=${encodeURIComponent(root!.dataset.targetPrefix || '')}`;
      const [list] = await Promise.all([api(`suggestions?${q}&lang=${lang}`), loadMe(lang)]);
      items = list.items; online = true;
    } catch { online = false; items = []; }
    badges();
  }

  function currentText(b: HTMLElement): string {
    const line = b.closest('.line, [data-term]');
    if (!line) return '';
    const l = reviewedLocale() === 'zh-Hans' ? 'zh' : reviewedLocale();
    const box = line.querySelector(`.tr-${l}`) || line.querySelector('[data-current]');
    if (!box) return '';
    // Name placeholders go back to their {HeroName} form, as the translation stores them.
    const text = (node: Element) => {
      const copy = node.cloneNode(true) as Element;
      copy.querySelectorAll<HTMLElement>('.ph').forEach((ph) => ph.replaceWith(`{${ph.dataset.ph}}`));
      return copy.textContent || '';
    };
    const parts = [...box.querySelectorAll('p, li')].map(text);
    return parts.length ? parts.join('\n') : text(box).trim();
  }

  function panelFor(b: HTMLElement) {
    const host = b.closest('.line, [data-term]')!;
    const external = (host as HTMLElement).dataset.panel ? document.getElementById((host as HTMLElement).dataset.panel!) : null;
    const existing = (external ?? host).querySelector('.sg-panel');
    if (existing) { existing.remove(); b.setAttribute('aria-expanded', 'false'); return; }
    // One pop-over at a time.
    if (host.classList.contains('term')) root!.querySelectorAll('.term .sg-panel, .term-row .sg-panel').forEach((p) => p.remove()); root!.querySelectorAll('.term [aria-expanded]').forEach((x) => x.setAttribute('aria-expanded', 'false'));
    b.setAttribute('aria-expanded', 'true');
    const { type, id } = targetOf(b);
    const locale = reviewedLocale();
    const panel = el('div', 'sg-panel');
    if (!online) { panel.append(el('p', 'sg-note', t.offline)); host.querySelector('.body, [data-term-body]')?.append(panel) ?? host.append(panel); return; }

    const mine = items.filter((x) => x.target_type === type && x.target_id === id && x.locale === locale);
    const list = el('div', 'sg-list');
    if (!mine.length) list.append(el('p', 'sg-note', t.none));
    for (const x of mine) {
      const card = el('div', `sg-item st-${x.status}`);
      const head = el('div', 'sg-head');
      if (x.author.avatar) { const a = el('img', 'sg-avatar'); a.src = x.author.avatar; a.alt = ''; head.append(a); }
      head.append(el('span', 'sg-author', x.author.name || t.anon));
      head.append(el('span', `sg-status st-${x.status}`, x.adopted ? t.adopted : t.status[x.status]));
      head.append(el('span', 'sg-kind', (t.kinds as Record<string, string>)[x.kind] || x.kind));
      card.append(head);
      if (x.hidden) card.append(el('p', 'sg-note', t.hiddenNote));
      if (x.proposed) { const p = el('p', 'sg-proposed'); p.append(el('span', 'sg-k', `${t.proposal}：`), document.createTextNode(x.proposed)); card.append(p); }
      if (x.reason) { const p = el('p', 'sg-reason'); p.append(el('span', 'sg-k', `${t.reason}：`), document.createTextNode(x.reason)); card.append(p); }
      if (x.admin_reason) { const p = el('p', 'sg-admin'); p.append(el('span', 'sg-k', `${x.status === 'questioned' ? t.question : t.status[x.status]}：`), document.createTextNode(x.admin_reason)); card.append(p); }
      list.append(card);
    }
    panel.append(list);

    const form = el('form', 'sg-form');
    const kinds = el('div', 'sg-kinds');
    let kind = type === 'dialogue' ? 'awkward' : 'naming';
    for (const [k, label] of Object.entries(t.kinds)) {
      const chip = el('button', 'sg-chip', label);
      chip.type = 'button';
      chip.setAttribute('aria-pressed', String(k === kind));
      chip.addEventListener('click', () => { kind = k; kinds.querySelectorAll('button').forEach((c) => c.setAttribute('aria-pressed', String(c === chip))); });
      kinds.append(chip);
    }
    const current = currentText(b);
    const proposedLabel = el('label', 'sg-field', t.proposedLabel);
    const proposed = el('textarea');
    proposed.rows = Math.min(6, Math.max(2, current.split('\n').length + 1));
    proposed.value = current;
    proposedLabel.append(proposed);
    const reasonLabel = el('label', 'sg-field', t.reason);
    const reason = el('input');
    reason.type = 'text'; reason.placeholder = t.reasonPh; reason.maxLength = 500;
    reasonLabel.append(reason);
    const row = el('div', 'sg-row');
    const submit = el('button', 'sg-submit', t.submit);
    submit.type = 'submit';
    const msg = el('span', 'sg-msg');
    const link = el('a', '', t.me);
    link.href = `/${lang}/reviews/#mine`;
    msg.append(link);
    row.append(submit, msg);
    const hint = el('p', 'sg-note', t.hint);
    // Who the suggestion will be signed as; changed from the header.
    const who = el('p', 'sg-who');
    const showWho = (m: Me | null) => {
      who.replaceChildren();
      if (m?.avatar) { const img = el('img', 'sg-avatar'); img.src = m.avatar; img.alt = ''; who.append(img); }
      who.append(m ? t.as.replace('{}', m.name) : t.asNew);
    };
    loadMe(lang).then(showWho, () => {});
    onMeChange(showWho);
    form.append(kinds, proposedLabel, reasonLabel, who, row, hint);
    form.addEventListener('submit', async (ev) => {
      ev.preventDefault();
      const p = proposed.value.trim();
      const r = reason.value.trim();
      if (!r && (!p || p === current.trim())) { hint.textContent = p === current.trim() && p ? t.same : t.empty; return; }
      submit.disabled = true;
      try {
        await api(`suggestions?lang=${lang}`, {
          method: 'POST',
          body: JSON.stringify({
            target_type: type, target_id: id, locale, kind,
            proposed: p === current.trim() ? '' : p, reason: r,
            request_id: crypto.randomUUID(),
            context: { scene: scene ? Number(scene) : undefined, href: `${location.pathname}#${host.id || ''}`, speaker: host.querySelector('.name')?.textContent || undefined },
          }),
        });
        await refresh();
        await reloadMe(lang);
        loadMyResults(lang);
        panel.remove();
        panelFor(b);
        host.querySelector('.sg-panel .sg-note')?.replaceWith(el('p', 'sg-ok', t.sent));
      } catch (e) {
        hint.textContent = t.failed + (t.errors[(e as Error).message] ?? (e as Error).message);
        submit.disabled = false;
      }
    });
    panel.append(form);
    const slot = ((host as HTMLElement).dataset.panel ? document.getElementById((host as HTMLElement).dataset.panel!) : host.querySelector('.body, [data-term-body]')) as HTMLElement ?? host;
    slot.append(panel);
    // A pop-over under a small name stays inside the window.
    if (slot.classList.contains('term-body')) {
      slot.style.left = '0';
      const r = slot.getBoundingClientRect();
      const over = r.right - (document.documentElement.clientWidth - 16);
      if (over > 0) slot.style.left = `${-over}px`;
    }
  }

  root.addEventListener('click', (ev) => {
    const b = (ev.target as Element).closest<HTMLElement>('[data-suggest]');
    if (b) panelFor(b);
  });
  // The badges follow the translation shown on story pages.
  new MutationObserver(badges).observe(root, { attributes: true, attributeFilter: ['data-tr'] });
  refresh();
}
