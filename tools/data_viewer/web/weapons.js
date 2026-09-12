import {element} from './ui.js';

export function weaponBadges(traits, expanded = false) {
  const group = element('div', undefined, 'weapon-markers');
  for (const marker of traits?.markers || []) {
    const badge = element('span', expanded ? `${marker.token} · ${marker.label}` : marker.token,
      'weapon-marker marker-' + ({'格':'melee','射':'ranged'}[marker.token] || marker.token));
    badge.title = marker.label;
    badge.setAttribute('aria-label', `${marker.token}：${marker.label}`);
    group.append(badge);
  }
  if (traits && !traits.parsed) group.append(element('small', '标记待解析'));
  if (!group.childNodes.length) group.append(element('span', '—'));
  return group;
}
