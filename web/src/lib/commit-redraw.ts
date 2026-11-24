// Redrawing an editor after a field's edit commits, without losing the user's next move.
//
// A text field commits on `change`, which fires as it loses focus to the field clicked or tabbed
// to next. Rebuilding the editor right there replaces that field under the click or the Tab: the
// focus falls to the page and what is typed next goes nowhere. The redraw this returns waits for
// the focus to land (and a pressed pointer to be released, so a button's click still reaches it),
// then gives the focus back to the control in the same place of the rebuilt editor.

const CONTROLS = 'input, select, textarea, button, summary, [tabindex]';

let pressed = false;
let listening = false;
function trackPointer() {
  if (listening) return;
  listening = true;
  document.addEventListener('pointerdown', () => { pressed = true; }, true);
  document.addEventListener('pointerup', () => { pressed = false; }, true);
  document.addEventListener('pointercancel', () => { pressed = false; }, true);
}

export function commitRedraw(container: HTMLElement, redraw: () => void): () => void {
  trackPointer();
  let queued = false;
  const controls = () => [...container.querySelectorAll<HTMLElement>(CONTROLS)];
  const run = () => {
    if (pressed) { document.addEventListener('pointerup', () => setTimeout(run), { once: true, capture: true }); return; }
    queued = false;
    const active = document.activeElement as HTMLElement | null;
    const at = active && container.contains(active) ? controls().indexOf(active) : -1;
    const caret = active instanceof HTMLInputElement || active instanceof HTMLTextAreaElement ? [active.selectionStart, active.selectionEnd] : null;
    // The click may have opened or closed a fold: kept, when the rebuilt editor has the same folds.
    const folds = [...container.querySelectorAll('details')].map((d) => d.open);
    redraw();
    const rebuilt = [...container.querySelectorAll('details')];
    if (rebuilt.length === folds.length) rebuilt.forEach((d, n) => { d.open = folds[n]; });
    const next = at >= 0 ? controls()[at] : undefined;
    if (!next || next === document.activeElement) return;
    next.focus({ preventScroll: true });
    if (caret && (next instanceof HTMLInputElement || next instanceof HTMLTextAreaElement))
      try { next.setSelectionRange(caret[0], caret[1]); } catch { /* a field without a caret (number) */ }
  };
  return () => {
    if (queued) return;
    queued = true;
    setTimeout(run);
  };
}
