// What the picture MOD makers share (scripts/names-maker.ts, icon-maker.ts): a picture the
// player gave, the square of it they framed, and the framing window.
import { el, type Text } from './mod-package';

// A picture: the one given, the square of it the player framed (centre and zoom, 1 being the
// largest square that fits), and the PNG made from that square. While `framing`, the window
// is open and nothing is made until the player confirms.
export type Picture = { source: ImageBitmap; cx: number; cy: number; zoom: number; png: Uint8Array; size: number; url: string; framing: boolean };

// Previews as data: URLs (the site's policy allows no blob: images).
export const dataUrl = (png: Uint8Array) => { let s = ''; for (let i = 0; i < png.length; i += 0x8000) s += String.fromCharCode(...png.subarray(i, i + 0x8000)); return `data:image/png;base64,${btoa(s)}`; };
const MAX_ZOOM = 5, VIEW = 192;   // CSS pixels of the framing window
// The square of the source the frame shows: its side and top-left corner, kept inside the picture.
export function frame(pic: Picture) {
  const side = Math.min(pic.source.width, pic.source.height) / pic.zoom;
  pic.cx = Math.min(Math.max(pic.cx, side / 2), pic.source.width - side / 2);
  pic.cy = Math.min(Math.max(pic.cy, side / 2), pic.source.height - side / 2);
  return { side, x: pic.cx - side / 2, y: pic.cy - side / 2 };
}

// The framing window: drag to move, the slider or the wheel to zoom, OK to make the PNG.
export function framer(pic: Picture, done: () => void, text: Text, make: (pic: Picture) => Promise<void>, smooth = true, hint = text.frameHint) {
  const canvas = el('canvas', { class: 'framer', width: VIEW * devicePixelRatio, height: VIEW * devicePixelRatio }) as HTMLCanvasElement;
  const zoom = el('input', { type: 'range', min: 1, max: MAX_ZOOM, step: 0.01, value: pic.zoom }) as HTMLInputElement;
  const g = canvas.getContext('2d')!;
  const draw = () => {
    const { side, x, y } = frame(pic);
    g.clearRect(0, 0, canvas.width, canvas.height);
    g.imageSmoothingEnabled = smooth;
    g.imageSmoothingQuality = 'high';
    g.drawImage(pic.source, x, y, side, side, 0, 0, canvas.width, canvas.height);
  };
  const changed = draw;
  let drag: { x: number; y: number; cx: number; cy: number } | null = null;
  canvas.addEventListener('pointerdown', (e) => { drag = { x: e.clientX, y: e.clientY, cx: pic.cx, cy: pic.cy }; canvas.setPointerCapture(e.pointerId); });
  canvas.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const perPixel = frame(pic).side / canvas.clientWidth;
    pic.cx = drag.cx - (e.clientX - drag.x) * perPixel;
    pic.cy = drag.cy - (e.clientY - drag.y) * perPixel;
    changed();
  });
  canvas.addEventListener('pointerup', () => { drag = null; });
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    pic.zoom = Math.min(MAX_ZOOM, Math.max(1, pic.zoom * Math.exp(-e.deltaY / 500)));
    zoom.value = String(pic.zoom); changed();
  }, { passive: false });
  zoom.addEventListener('input', () => { pic.zoom = Number(zoom.value); changed(); });
  draw();
  return el('div', { class: 'framing' }, canvas,
    el('label', { class: 'zoom' }, text.zoom, zoom),
    el('span', { class: 'hint' }, hint),
    el('div', { class: 'frame-actions' },
      el('button', { type: 'button', class: 'btn small', onclick: async () => { await make(pic); pic.framing = false; done(); } }, text.confirmFrame),
      el('button', { type: 'button', class: 'mini', onclick: () => {
        pic.zoom = 1; pic.cx = pic.source.width / 2; pic.cy = pic.source.height / 2; zoom.value = '1'; changed();
      } }, text.resetFrame)));
}
