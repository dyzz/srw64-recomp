export const ROUTES = {
  '3DD1': ['3DD0', '3DD1', '3DD5', '3DD7'], '3DD2': ['3DD0', '3DD2', '3DD5', '3DD8'],
  '3DD3': ['3DD0', '3DD3', '3DD6', '3DD7'], '3DD4': ['3DD0', '3DD4', '3DD6', '3DD8'],
};
export function sectionVisible(section, route) {
  return !route || !section || ROUTES[route]?.includes(section) || ['3DD9','3DDA','3DDB'].includes(section);
}
export function lineId(eventKey, offset) { return `${eventKey.split(':').pop()}-${offset.toString(16)}`; }
export function storyHash(scene, line = '') {
  const params = new URLSearchParams({scene: String(scene)});
  if (line) params.set('line', line);
  return '#' + params;
}
export function parseStoryHash(hash) {
  const raw = hash.replace(/^#/, '');
  const params = new URLSearchParams(raw);
  const value = /^\d+$/.test(raw) ? raw : params.get('scene');
  const scene = value !== null && /^\d+$/.test(value) ? Number(value) : 1;
  const line = params.get('line') || '';
  return {scene: Number.isSafeInteger(scene) ? scene : 1, line: /^[0-9a-f]+-[0-9a-f]+$/.test(line) ? line : ''};
}
export function selectSpeaker(speaker, route) {
  if (speaker.status !== 'route-relative') return speaker;
  const offset = Object.keys(ROUTES).indexOf(route);
  if (offset < 0) return {...speaker, portrait: null};
  const candidate = speaker.candidates?.find(c => {
    const id = Number(c.key.split(':').pop());
    return id === 25 + offset || id === 29 + offset;
  });
  return candidate ? {...speaker, ...candidate, label: candidate.label + '（所选路线）', status: 'route-preview'}
    : {...speaker, portrait: null};
}
export function searchDialogue(rows, query, scene = null, limit = 200) {
  const q = query.trim().toLocaleLowerCase();
  if (q.length < 2) return {total: 0, rows: []};
  const matches = rows.filter(r => (scene === null || r[0] === scene) &&
    `${r[4]} ${r[5]} ${r[3]}`.toLocaleLowerCase().includes(q));
  return {total: matches.length, rows: matches.slice(0, limit)};
}
