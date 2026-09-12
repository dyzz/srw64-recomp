import {element, anchor, block, disclosure, table} from './ui.js';
import {weaponBadges} from './weapons.js';

function statGrid(stats) {
  const grid = element('div', undefined, 'stat-grid');
  for (const stat of stats) {
    const card = element('div', undefined, 'stat-card');
    card.append(element('span', stat.name.replace(/^基础/, '').trim()), element('strong', stat.value));
    if (stat.growth_per_level !== undefined) card.append(element('small', `每级 +${stat.growth_per_level}`));
    grid.append(card);
  }
  return grid;
}

function weaponTable(weapons, otherForms = false) {
  const headers = ['武器', '属性', '攻击力', '射程', '命中', '暴击', '弹数', 'EN', '气力'];
  if (otherForms) headers.push('匹配形态');
  const rows = weapons.map(w => {
    const s = Object.fromEntries(w.stats.map(f => [f.id, f.value]));
    const name = element('div', undefined, 'weapon-name');
    name.append(anchor(w.key, w.weapon_traits?.display_name || w.label));
    const signed = n => n > 0 ? '+' + n : n;
    const cells = [name, weaponBadges(w.weapon_traits), s.power, s.range_min === s.range_max ? s.range_min : `${s.range_min}–${s.range_max}`,
      signed(s.hit_modifier), signed(s.critical_modifier), s.ammo === -1 ? '不限' : s.ammo,
      s.en_cost, s.will_required || '—'];
    if (otherForms) cells.push(entityLinks(w.eligible_forms));
    return cells;
  });
  return table(headers, rows, otherForms ? '共用列表中的其他形态武器' : '匹配当前形态的武器');
}

export function entityLinks(items) {
  const links = element('div', undefined, 'entity-links');
  for (const item of items) {
    const a = anchor(item.key, item.label);
    a.className = 'entity-chip';
    links.append(a);
  }
  return links;
}

function observedRelations(profile) {
  const relations = profile.observed_pilots || profile.observed_units;
  const fragment = document.createDocumentFragment();
  if (!relations.length) return fragment;
  const group = element('div');
  group.append(entityLinks(relations));
  const note = element('p', '来自历史快照，仅代表该时点的搭乘关系。 ', 'section-note');
  for (const key of new Set(relations.map(r => r.observation_key))) note.append(anchor(key, '查看来源'));
  group.append(note);
  fragment.append(block(profile.kind === 'unit' ? '关联机师 · 历史观察' : '搭乘机体 · 历史观察', group));
  return fragment;
}

export function renderProfile(record) {
  const p = record.profile, fragment = document.createDocumentFragment();
  if (p.kind === 'pilot' && p.full_name !== record.label) fragment.append(element('p', p.full_name, 'full-name'));
  if (p.stats.length) {
    fragment.append(block('基础能力', statGrid(p.stats)), element('p',
      p.kind === 'unit' ? '原始基础值，未计入改造、装备及状态修正。' : '等级 1 的基础值；卡片下方为等级成长增量。机体和状态修正另计。', 'section-note'));
  } else fragment.append(element('p', '这个名称身份尚未关联固定能力记录。', 'profile-note'));

  if (p.kind === 'unit') {
    const abilities = element('div', undefined, 'ability-grid');
    for (const a of p.abilities || []) {
      const card = element('div', undefined, 'ability-card');
      card.append(element('small', a.family), anchor(a.key, a.label), element('span', a.alias));
      if (a.label_confidence === 'candidate') card.append(element('small', '名称待确认', 'candidate-label'));
      abilities.append(card);
    }
    fragment.append(block(`特殊能力与装备 · ${(p.abilities || []).length} 项`, abilities.childNodes.length ? abilities
      : element('p', '原始能力位与装备位未设置已整理的项目。', 'section-note')));
    const unknown = (p.ability_flags || []).filter(f => f.unknown_bits);
    if (unknown.length) fragment.append(element('p', '仍有未解释标志：' + unknown.map(f => `+0x${f.offset.toString(16)} / 0x${f.unknown_bits.toString(16)}`).join('、'), 'profile-note'));
    fragment.append(element('p', '点击能力查看条件和全部持有机体。这里展示原始配置；强化／事件追加以及驾驶员技能条件另计。', 'section-note'));
    const current = p.weapons.filter(w => w.matches_form), others = p.weapons.filter(w => !w.matches_form);
    fragment.append(block(`武器 · 当前形态 ${current.length} 项`, current.length ? weaponTable(current) : element('p', '列表中没有匹配当前形态的武器。', 'section-note')));
    fragment.append(element('p', '格：格斗武器 · 射：射击武器 · P：可移动后使用 · B：光束武器 · MAP：地图武器', 'section-note weapon-legend'));
    fragment.append(element('p', '按原始形态槽匹配；实际使用还受技能、状态及其他条件限制。', 'section-note'));
    if (p.related_forms.length) fragment.append(block('共用武器列表的关联形态', entityLinks(p.related_forms)));
    if (others.length) fragment.append(disclosure(`其他形态武器 · ${others.length} 项`, weaponTable(others, true), 'profile-fold'));
  } else {
    const spirits = element('div', undefined, 'spirit-grid');
    for (const command of p.spirits) {
      const card = element('div', undefined, 'spirit-card');
      card.append(element('span', `Lv. ${command.level}`, 'learn-level'), anchor(command.text_key, command.command_name));
      spirits.append(card);
    }
    fragment.append(block(p.spirits.length ? `精神习得 · ${p.spirits.length} 项` : '精神习得 · 未关联', p.spirits.length ? spirits : element('p', '尚未关联固定精神习得表，原始映射见下方来源。', 'section-note')));
    const skills = element('div');
    for (const skill of p.skills) {
      const card = element('div', undefined, 'skill-card');
      const title = element('div', undefined, 'skill-title');
      title.append(skill.definition_key ? anchor(skill.definition_key, skill.name + ' · ' + skill.alias) : element('strong', skill.name), anchor(skill.source_key, '习得来源 ↗'));
      card.append(title);
      if (skill.equipment_note) card.append(element('p', skill.equipment_note, 'section-note'));
      if (!skill.selected_for_display) card.append(element('p', '该标志已启用；原界面优先显示同组的另一种技能名称。', 'section-note'));
      if (skill.ranks) card.append(table(skill.ranks.map((_, i) => 'L' + (i + 1)),
        [skill.ranks.map(level => level === null ? '—' : `Lv. ${level}`)], skill.name + '各级习得等级'));
      else card.append(element('p', '阈值记录未能安全关联，暂不计算习得等级。', 'section-note'));
      skills.append(card);
    }
    fragment.append(block('特殊技能与习得', p.skills.length ? skills : element('p', p.stats.length ? '已解析技能位中没有启用项。' : '能力记录未关联，技能信息待确认。', 'section-note')));
    if (p.skills.length) fragment.append(element('p', 'L1–L9 表示技能等级，Lv. 表示机师习得等级；— 表示没有该级的有效阈值。', 'section-note'));
  }
  fragment.append(observedRelations(p));
  if (p.notices.length) {
    const list = element('ul', undefined, 'profile-notices');
    for (const notice of p.notices) list.append(element('li', notice));
    fragment.append(disclosure('数据关联说明 · ' + p.notices.length + ' 项', list, 'profile-fold'));
  }
  if (p.source_records?.length) fragment.append(block('档案来源', entityLinks(p.source_records.map(r => {
    const [,category,id] = r.key.split(':');
    const name = {pilot_stats:'能力表',spirits:'精神表',pilot_thresholds:'技能表',actor_stats_map:'能力映射',actor_spirits_map:'精神映射'}[category];
    return {...r, label: `${name} ${Number(id)}`};
  }))));
  return fragment;
}
