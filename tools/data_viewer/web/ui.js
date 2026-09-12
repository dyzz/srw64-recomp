export function element(tag, text, cls) {
  const e = document.createElement(tag);
  if (text !== undefined) e.textContent = text;
  if (cls) e.className = cls;
  return e;
}

export function anchor(key, text) {
  const a = element('a', text);
  a.href = '#' + encodeURIComponent(key);
  return a;
}

export function block(title, child) {
  const fragment = document.createDocumentFragment();
  fragment.append(element('h3', title), child);
  return fragment;
}

export function disclosure(title, child, cls) {
  const d = element('details', undefined, cls);
  d.append(element('summary', title), child);
  return d;
}

export function rawBlock(title, raw) {
  return disclosure(title, element('pre', raw));
}

export function table(headers, rows, caption) {
  const wrap = element('div', undefined, 'table-scroll');
  wrap.tabIndex = 0;
  wrap.setAttribute('aria-label', caption);
  const t = element('table', undefined, 'data-table');
  t.append(element('caption', caption, 'sr-only'));
  const head = element('thead'), tr = element('tr');
  for (const name of headers) {
    const th = element('th', name);
    th.scope = 'col';
    tr.append(th);
  }
  head.append(tr);
  const body = element('tbody');
  for (const cells of rows) {
    const row = element('tr');
    for (const content of cells) {
      const td = element('td');
      td.append(content instanceof Node ? content : document.createTextNode(String(content)));
      row.append(td);
    }
    body.append(row);
  }
  t.append(head, body);
  wrap.append(t);
  return wrap;
}
