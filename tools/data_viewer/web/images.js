import {element, anchor} from './ui.js';

export function thumbnail(value, label) {
  const img = element('img', undefined, 'row-thumbnail ' + value.kind);
  img.src = value.thumbnail;
  img.alt = label + ' · ' + value.caption;
  img.loading = 'lazy';
  img.decoding = 'async';
  img.width = 48;
  img.height = 48;
  return img;
}

export function renderImages(record) {
  const fragment = document.createDocumentFragment();
  for (const image of record.images || []) {
    const figure = element('figure', undefined, 'entity-media ' + image.kind);
    const original = element('a', undefined, 'image-original');
    original.href = image.path;
    original.target = '_blank';
    original.rel = 'noopener';
    original.title = '打开原始尺寸图片';
    const img = element('img');
    img.src = image.path;
    img.alt = record.label + ' · ' + image.caption;
    img.width = image.width;
    img.height = image.height;
    img.decoding = 'async';
    original.append(img);
    const caption = element('figcaption');
    caption.append(element('strong', image.caption),
      element('span', `${image.width} × ${image.height} · 点击图片查看原图`));
    if (image.kind === 'unit-icon') caption.append(element('span', '战术地图上的机体外观'));
    if (image.layout) caption.append(element('span',
      `${image.layout.grid_columns} × ${image.layout.grid_rows} 格 · 按原始图块布局拼合`));
    const refs = element('div', undefined, 'image-sources');
    for (const source of image.sources) refs.append(anchor(source.key, source.role));
    caption.append(refs);
    figure.append(original, caption);
    fragment.append(figure);
  }
  return fragment;
}
