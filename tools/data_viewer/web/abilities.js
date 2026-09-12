import {element, anchor, block, table} from './ui.js';

export function renderAbility(record) {
  const d = record.definition, out = document.createDocumentFragment(), pilot = d.kind === 'pilot';
  out.append(element('p', `${d.family} · ${d.alias}`, 'full-name'));
  if (record.label_confidence === 'candidate') out.append(element('p', '触发条件与持有位已确认；名称对应仍待确认。', 'profile-note'));
  out.append(element('p', d.note, 'ability-description'));
  const stats = element('div', undefined, 'stat-grid');
  const counts = [['原始持有' + (pilot ? '人物身份' : '机体／形态'), d.holder_count]];
  if (pilot) counts.push(['有有效习得阈值', d.learnable_count], ['无有效阈值', d.zero_threshold_count], ['阈值未关联', d.missing_threshold_count]);
  for (const [name, count] of counts) {
    const card = element('div', undefined, 'stat-card');
    card.append(element('span', name), element('strong', count));stats.append(card);
  }
  out.append(stats, element('p', pilot
    ? `按人物 ID 分别保留，共引用 ${d.unique_stats_count} 条不同能力记录。标志持有不表示等级 1 就能使用；没有有效阈值的项目不计算为已习得。`
    : '按原始机体 ID 统计，变形前后及同名记录分别保留；强化或事件动态追加的能力不计入原始持有者。', 'section-note'));
  const rows = d.members.map(m => {
    const cells = [anchor(m.key, m.label), Number(m.key.split(':').at(-1))];
    if (pilot) cells.push(m.max_rank === null ? '阈值未关联' : m.max_rank === 0 ? '无有效阈值' : `Lv. ${m.first_level}`,
      m.max_rank ? `L${m.max_rank}` : '—', m.selected_for_display ? '按等级显示' : '同组其他名称优先');
    return cells;
  });
  out.append(block('全部持有者 · ' + d.holder_count, rows.length
    ? table(pilot ? ['人物', 'ID', '首次习得', '最高技能等级', '原界面名称'] : ['机体／形态', 'ID'], rows, record.label + '全部持有者')
    : element('p', '原始记录中没有持有者。', 'section-note')));
  return out;
}

export function abilityCoverage(category, coverage) {
  const pilot = category === 'pilot_skills';
  const text = pilot
    ? `已检查全部 ${coverage.actor_total} 个人物身份：${coverage.actor_total - coverage.actors_without_stats.length} 个关联固定能力，${coverage.actors_without_stats.length} 个未关联；${coverage.actors_without_thresholds.length} 个能力已关联但技能阈值缺失。`
    : `已检查全部 ${coverage.unit_total} 个机体／形态的能力位与装备位，整理 ${coverage.unit_definition_count} 类能力、功能及装备条件。${coverage.unit_without_abilities.length} 个记录未设置这些标志。`;
  const unknown = pilot ? coverage.pilot_unknown_bits : coverage.unit_unknown_bits;
  return element('p', text + (unknown.length ? `仍有 ${unknown.length} 项未解释位，见提取清单。` : '上述字段在原始记录中没有遗留的未解释置位。'), 'coverage-note');
}
