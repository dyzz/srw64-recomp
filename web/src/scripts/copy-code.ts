// A copy button on every code block of a technical page (src/pages/[lang]/docs/[slug].astro).
// The labels come from the article's data-copy / data-copied attributes, in the page's language.
export function copyButtons(root: HTMLElement) {
  const label = root.dataset.copy || 'Copy';
  const done = root.dataset.copied || 'Copied';
  root.querySelectorAll<HTMLPreElement>('pre').forEach((pre) => {
    if (pre.parentElement?.classList.contains('code-wrap')) return;
    const wrap = document.createElement('div');
    wrap.className = 'code-wrap';
    pre.replaceWith(wrap);
    wrap.append(pre);
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'copy-code';
    button.textContent = label;
    button.addEventListener('click', async () => {
      const text = (pre.querySelector('code') ?? pre).textContent ?? '';
      try {
        await navigator.clipboard.writeText(text.replace(/\n$/, ''));
        button.textContent = done;
        button.classList.add('done');
        setTimeout(() => { button.textContent = label; button.classList.remove('done'); }, 1600);
      } catch {
        // No clipboard (an insecure origin): select the text so it can be copied by hand.
        const range = document.createRange();
        range.selectNodeContents(pre);
        const selection = window.getSelection();
        selection?.removeAllRanges();
        selection?.addRange(range);
      }
    });
    wrap.append(button);
  });
}
