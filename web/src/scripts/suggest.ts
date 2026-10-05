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
  status: 'pending' | 'questioned' | 'processed' | 'dismissed'; adopted?: boolean;
  admin_reason?: string; question_response?: string;
};

const T = {
  zh: {
    kinds: { mistranslation: '错译', awkward: '不通顺', typo: '错字', naming: '译名不统一', other: '其他' },
    status: { pending: '待处理', questioned: '有疑问', processed: '已处理', dismissed: '不处理' },
    adopted: '已采纳', proposal: '建议', reason: '理由', reasonPh: '哪里不对、为什么（可选）',
    proposedLabel: '建议译文', submit: '提交意见', sent: '已提交，谢谢！', you: '你的身份', anon: '匿名',
    none: '这一句还没有意见。', offline: '意见功能暂时不可用。', failed: '提交失败：', me: '我的意见',
    hint: '不用注册；提交后可以在「我的意见」里修改或撤回。', question: '维护者的疑问', close: '收起',
    empty: '请写建议译文或理由，至少一项。', same: '建议译文和现在的一样。',
  },
  en: {
    kinds: { mistranslation: 'Mistranslation', awkward: 'Awkward', typo: 'Typo', naming: 'Inconsistent name', other: 'Other' },
    status: { pending: 'Pending', questioned: 'Question', processed: 'Done', dismissed: 'Declined' },
    adopted: 'Adopted', proposal: 'Proposal', reason: 'Reason', reasonPh: 'What is wrong and why (optional)',
    proposedLabel: 'Suggested translation', submit: 'Send suggestion', sent: 'Sent, thank you!', you: 'You are', anon: 'anonymous',
    none: 'No suggestions on this line yet.', offline: 'Suggestions are unavailable right now.', failed: 'Could not send: ', me: 'My suggestions',
    hint: 'No account needed; edit or withdraw it later under My suggestions.', question: 'Maintainer’s question', close: 'Close',
    empty: 'Write a suggested translation or a reason.', same: 'The suggestion is the same as the current text.',
  },
  ja: {
    kinds: { mistranslation: '誤訳', awkward: '不自然', typo: '誤字', naming: '表記揺れ', other: 'その他' },
    status: { pending: '未対応', questioned: '質問中', processed: '対応済み', dismissed: '見送り' },
    adopted: '採用', proposal: '提案', reason: '理由', reasonPh: 'どこが・なぜ（任意）',
    proposedLabel: '提案する訳', submit: '意見を送る', sent: '送信しました。ありがとうございます！', you: 'あなたの名前', anon: '匿名',
    none: 'この行への意見はまだありません。', offline: '意見機能は現在利用できません。', failed: '送信できませんでした：', me: '自分の意見',
    hint: '登録不要。送信後は「自分の意見」で修正・取り消しができます。', question: '管理者からの質問', close: '閉じる',
    empty: '提案する訳か理由のどちらかを書いてください。', same: '提案が現在の訳と同じです。',
  },
};

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
  if (!root) return;
  const lang = (root.dataset.lang || 'zh') as keyof typeof T;
  const t = T[lang];
  const scene = root.dataset.scene;
  let items: Item[] = [];
  let me: { name: string } | null = null;
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
  }

  async function refresh() {
    try {
      const q = scene ? `scene=${scene}` : `target_type=${encodeURIComponent(root!.dataset.targetType || '')}&target_prefix=${encodeURIComponent(root!.dataset.targetPrefix || '')}`;
      const [list, who] = await Promise.all([api(`suggestions?${q}&lang=${lang}`), api(`me?lang=${lang}`)]);
      items = list.items; me = who.participant; online = true;
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
    const existing = host.querySelector('.sg-panel');
    if (existing) { existing.remove(); b.setAttribute('aria-expanded', 'false'); return; }
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
    const msg = el('span', 'sg-msg', me ? `${t.you}：${me.name} · ` : '');
    const link = el('a', '', t.me);
    link.href = `/${lang}/reviews/#mine`;
    msg.append(link);
    row.append(submit, msg);
    const hint = el('p', 'sg-note', t.hint);
    form.append(kinds, proposedLabel, reasonLabel, row, hint);
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
        panel.remove();
        panelFor(b);
        host.querySelector('.sg-panel .sg-note')?.replaceWith(el('p', 'sg-ok', t.sent));
      } catch (e) {
        hint.textContent = t.failed + (e as Error).message;
        submit.disabled = false;
      }
    });
    panel.append(form);
    (host.querySelector('.body, [data-term-body]') ?? host).append(panel);
  }

  root.addEventListener('click', (ev) => {
    const b = (ev.target as Element).closest<HTMLElement>('[data-suggest]');
    if (b) panelFor(b);
  });
  // The badges follow the translation shown on story pages.
  new MutationObserver(badges).observe(root, { attributes: true, attributeFilter: ['data-tr'] });
  refresh();
}
