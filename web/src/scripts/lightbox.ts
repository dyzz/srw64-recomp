// Click a screenshot to see it full size. Covers the home page's shots, gallery and
// comparison pairs and the images in blog posts; ←/→ step through the page's images,
// Esc or a click closes.
const ZOOM = '.shot img, .gallery img, .pair img, .prose img';

export function initLightbox() {
  const images = () => [...document.querySelectorAll<HTMLImageElement>(ZOOM)];
  if (!images().length) return;
  const box = document.createElement('dialog');
  box.className = 'lb';
  const img = document.createElement('img');
  const cap = document.createElement('p');
  const prev = document.createElement('button'), next = document.createElement('button'), close = document.createElement('button');
  prev.className = 'lb-nav prev'; prev.textContent = '‹'; prev.setAttribute('aria-label', 'Previous');
  next.className = 'lb-nav next'; next.textContent = '›'; next.setAttribute('aria-label', 'Next');
  close.className = 'lb-close'; close.textContent = '×'; close.setAttribute('aria-label', 'Close');
  box.append(img, cap, prev, next, close);
  document.body.append(box);
  let at = 0;

  function show(i: number) {
    const list = images();
    at = (i + list.length) % list.length;
    const src = list[at];
    img.src = src.currentSrc || src.src;
    img.alt = src.alt;
    cap.textContent = src.alt;
    cap.hidden = !src.alt;
    prev.hidden = next.hidden = list.length < 2;
  }
  document.addEventListener('click', (ev) => {
    const target = (ev.target as Element).closest<HTMLImageElement>(ZOOM);
    if (!target || target.closest('a')) return;
    show(images().indexOf(target));
    box.showModal();
  });
  box.addEventListener('click', (ev) => {
    if (ev.target === prev) show(at - 1);
    else if (ev.target === next) show(at + 1);
    else box.close();
  });
  box.addEventListener('keydown', (ev) => {
    if (ev.key === 'ArrowLeft') show(at - 1);
    if (ev.key === 'ArrowRight') show(at + 1);
  });
}
