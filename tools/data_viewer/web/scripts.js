import {element, anchor, block, disclosure, table} from './ui.js';

const hex = n => '0x' + n.toString(16).toUpperCase().padStart(4, '0');
const status = s => ({'terminator-reached':'已读至结束符','unknown-opcode':'未知指令处停止',
  'truncated-operands':'参数越过边界','physical-boundary':'已到物理边界'}[s] || s);
const clean = s => (s || '').replaceAll('<BR>', '\n').replaceAll('<END>', '').replaceAll('<STOP>', ' ▸ ');
const confidence = c => ({'code-confirmed':'代码已确认','structure-confirmed':'结构已确认','unknown':'语义待解析'}[c] || c);
const pad4 = n => String(n).padStart(4, '0');

function operandCell(field) {
  const span = element('span', undefined, 'operand');
  const label = field.label || field.meaning;
  if (field.key) span.append(anchor(field.key, `${field.name} = ${label ?? field.value}`));
  else span.append(element('span', `${field.name} = ${label ?? field.value}`));
  if (field.role === 'position' && field.x !== undefined) span.append(element('small', ` (${field.x}, ${field.y})`));
  if (field.role === 'position' && field.relative_to) span.append(element('small', ` ${field.relative_to} → ${field.direction} ${field.distance}`));
  if (field.role === 'region_x' || field.role === 'region_y') span.append(element('small', ` [${field.start}, ${field.start + field.span})`));
  if (field.role === 'region_target' && field.count !== undefined) span.append(element('small', ` ×${field.count}`));
  if (field.scene_targets) for (const t of field.scene_targets) span.append(document.createTextNode(' '), anchor(t.key, `→ 场景 ${t.scene} 目标`));
  if (field.scene_misses) span.append(element('small', ` 场景 ${field.scene_misses.join('/')} 无对应记录`));
  if (field.label === undefined && field.meaning === undefined && field.key === undefined && field.x === undefined && field.relative_to === undefined && field.start === undefined) span.append(element('small', ` (${hex(field.value)})`));
  return span;
}

function instructionRows(instructions) {
  return instructions.map(i => {
    const cell = element('div');
    if (i.kind === 'dialogue') {
      const who = element('div', undefined, 'script-speaker');
      if (i.speaker_key) who.append(anchor(i.speaker_key, i.speaker_label));
      else if (i.speaker_status === 'route-relative') {
        who.append(element('span', `${i.speaker_label}（按路线）：`));
        for (const c of i.speaker_candidates || []) who.append(anchor(c.key, c.label), document.createTextNode(' '));
      } else who.append(element('span', i.speaker_label || '说话人未解析'));
      cell.append(who);
      if (i.text_key) cell.append(anchor(i.text_key, `文本 ${i.operands[0]} · 显示模式 ${i.dialogue_mode}`), element('div', clean(i.text), 'script-dialogue'));
      else cell.append(element('span', `文本 ${i.operands[0]} 不在表 0 中`));
    } else if (i.fields?.length) {
      for (const f of i.fields) cell.append(operandCell(f), document.createTextNode(' '));
    } else cell.append(element('span', i.operands.length ? i.operands.map(hex).join(' · ') : '—'));
    const name = element('div', undefined, `script-op script-${i.kind}`);
    name.style.paddingLeft = `${i.depth * 1.2}rem`;
    name.append(i.opcode_key ? anchor(i.opcode_key, `${hex(i.opcode)} ${i.name}`) : element('span', `${hex(i.opcode)} ${i.name}`));
    if (i.unbalanced) name.append(element('small', ' 多余的块结束'));
    if (i.no_advance) name.append(element('small', ' 不返回'));
    return [hex(i.rom_offset), name, cell];
  });
}

function triggerLine(trigger) {
  const line = element('div', undefined, 'script-trigger');
  line.append(element('strong', trigger.name), element('span', ` · ${trigger.polled}${trigger.status_code ? ' · 状态码 ' + trigger.status_code : ''} · ${confidence(trigger.confidence)}`));
  const fields = element('div', undefined, 'links');
  for (const f of trigger.fields) {
    if (f.name === '保留' && f.value === 0) continue;
    const row = operandCell(f);
    if (f.note) row.append(element('small', ` ${f.note}`));
    fields.append(row);
  }
  line.append(fields);
  return line;
}

export function renderScript(data, coverage) {
  const root = element('div', undefined, 'script-view');
  if (data.scenario) {
    const s = data.scenario;
    root.append(element('p', `事件库共 ${coverage.scene_index_slots} 个索引槽，合并共用入口表后为 ${coverage.unique_event_lists} 组、${coverage.unique_events} 个事件入口；全部 ${coverage.unique_events} 个事件已读至结束符。索引不等于可玩关卡数，章节名称尚未建立对应。`, 'basis'));
    const related = element('div', undefined, 'links');
    related.append(anchor(s.map_key, '查看同索引地图与资源'), anchor(s.auxiliary_key, '查看场景配套数据（出击记录）'));
    root.append(block('关联数据', related));
    if (s.shared_scene_indices.length) {
      const shared = element('div', undefined, 'links');
      for (const i of s.shared_scene_indices) shared.append(anchor(`base:scenarios:${pad4(i)}`, `场景索引 ${i}`));
      root.append(block('共用这份事件入口表', shared));
    }
    root.append(block('事件入口', table(['入口', '触发类型', '触发参数', '内容'], s.events.map(e => {
      const params = element('div');
      for (const f of e.trigger_fields) if (!(f.name === '保留' && f.value === 0)) params.append(operandCell(f), document.createTextNode(' '));
      if (!params.childNodes.length) params.append(element('span', '—'));
      return [anchor(e.key, `入口 ${e.slot}`), `${e.type} · ${e.type_name}`, params,
        `${e.instruction_count} 条指令 · ${e.dialogue_count} 条对白 · ${status(e.status)}`];
    }), '本场景的事件入口')));
    root.append(element('p', '类型 0–11 进入分类事件组，12–14 保存为独立入口。触发条件按 8009E180 的轮询阶段与各类型触发函数解释；列表顺序不表示发生顺序。', 'basis'));
    const flow = element('div', undefined, 'links');
    for (const k of s.next_scene_keys) flow.append(anchor(k, `3D4B → ${k}`));
    if (!s.next_scene_keys.length) flow.append(element('span', '本场景脚本没有 3D4B 场景切换'));
    root.append(block('后续场景（路线流向）', flow));
    const speakers = element('div', undefined, 'entity-links');
    for (const [k, v] of Object.entries(s.speaker_keys)) { const a = anchor(k, v); a.className = 'entity-chip'; speakers.append(a); }
    if (Object.keys(s.speaker_keys).length) root.append(block('登场说话人（不含仅按路线解析的主角）', speakers));
    const groups = element('div', undefined, 'links');
    for (const [g, keys] of Object.entries(s.deployment_group_keys)) {
      const row = element('div');
      row.append(element('strong', `组 ${g}：${keys.length} 条记录 `));
      for (const k of keys.slice(0, 12)) row.append(anchor(k, k.slice(-8)), document.createTextNode(' '));
      if (keys.length > 12) row.append(element('small', ` … 共 ${keys.length} 条`));
      if (!keys.length) row.append(element('small', ' 本场景配套数据中没有该组'));
      groups.append(row);
    }
    if (s.deployment_groups.length) root.append(block('脚本引用的配套记录组（3D45/3D3D/3D46）', groups));
    if (s.flags_referenced.length) root.append(block('引用的 2 位变量', element('div', s.flags_referenced.join(', '), 'mono')));
  } else if (data.script) {
    const s = data.script;
    const scenes = element('div', undefined, 'links');
    for (const i of s.scenes) scenes.append(anchor(`base:scenarios:${pad4(i)}`, `场景索引 ${i}`));
    root.append(block('所属场景', scenes));
    root.append(block('触发条件', triggerLine(s.trigger)));
    root.append(element('p', `登记类型 ${s.header_words[0]} · 头部参数 ${s.header_words.slice(1).map(hex).join(' / ')}`));
    root.append(element('p', `${status(s.status)} · ${s.instructions.length} 条指令、${s.decoded_bytes} 字节 · ${s.blocks.length} 个条件块（最深 ${s.max_depth} 层${s.unbalanced_block_ends ? '，' + s.unbalanced_block_ends + ' 个多余块结束' : ''}）。${s.scope}`, 'basis'));
    if (s.hazards.length) root.append(element('p', `扫描差异：${s.hazards.map(h => `${h.kind} @ ${h.offset ?? h.opener_offset}`).join('；')}`, 'basis'));
    root.append(block('指令序列（缩进 = 条件块嵌套；标记 = 主角上下文段）', table(['ROM 地址', '指令', '参数 / 对白'], instructionRows(s.instructions), '静态顺序指令视图')));
    if (s.stop_word !== null) root.append(element('p', `下一字 ${hex(s.stop_word)}：不在已确认的指令、条件或标记集合内，后续不继续识别。`, 'basis'));
    if (s.remainder_hex) root.append(disclosure(s.status === 'terminator-reached' ? '结束符后的保留字节' : '未解析的原始字节',
      element('pre', s.remainder_hex.match(/.{1,64}/g).join('\n'))));
  } else if (data.opcode) {
    const o = data.opcode;
    const rows = [
      ['族', {command: '普通命令（3D31–3D79）', condition: '条件指令（3E00–3E1D）', marker: '上下文标记（3DD0–3DDB）'}[o.family]],
      ['处理函数', o.handler_vram || (o.family === 'marker' ? '8009F0E8 标记循环' : o.family === 'condition' ? '内联恒真分支' : '空处理函数（默认分支）')],
      ['参数长度', o.operand_words === null || o.operand_words === undefined ? '无（不可达）' : `${o.operand_words} 个 16 位参数`],
      ['语义确认程度', confidence(o.semantic_confidence)],
      ['依据', o.basis || '—']];
    if (o.block) rows.splice(2, 0, ['块结构', {opener: '条件块开始（为假时跳到同层 3E1D）', statement: '语句（恒真，不开块）', 'block-end': '块结束'}[o.block]]);
    if (o.matches) rows.splice(2, 0, ['匹配规则', o.matches]);
    if (o.dialogue_mode !== undefined && o.dialogue_mode !== null) rows.push(['对白显示模式', String(o.dialogue_mode)]);
    root.append(table(['项目', '值'], rows, '指令定义'));
    if (o.operands?.length) root.append(block('参数', table(['序号', '名称', '角色'], o.operands.map((p, i) => [String(i), p.name, p.role]), '参数定义')));
    root.append(element('p', '参数长度与分发表、条件跳转表、扫描表和触发表逐项对照 ROM 机器码；参数长度已知不代表所有参数的游戏含义已确认。', 'basis'));
  } else if (data.event_type) {
    const t = data.event_type;
    root.append(table(['项目', '值'], [['分组', t.group === null ? `独立槽 ${t.slot}` : `分类组 ${t.group}`], ['轮询', t.polled], ['状态码', t.status_code || '—'], ['触发函数', t.handler_vram || '—'], ['确认程度', confidence(t.confidence)]], '事件类型'));
  } else if (data.auxiliary) {
    const a = data.auxiliary;
    root.append(element('p', `${a.record_count} 条 28 字节记录，${a.groups.length} 个组${a.terminated ? '，已读到 999 结束符' : '，本块上界前没有对齐的 999'}；尾部 ${a.trailing_bytes} 字节保留。`, 'basis'));
    const groups = element('div', undefined, 'links');
    for (const g of a.groups) groups.append(element('span', `组 ${g}`));
    root.append(block('包含的组号', groups));
  } else if (data.deployment) {
    const d = data.deployment;
    const side = (data.fields || []).find(f => f.name === '阵营');
    root.append(element('p', `组 ${d.group} · 格 (${d.x}, ${d.y}) · 阵营 ${side ? side.value : d.side}（记录值 ${d.side}） · 等级偏移 ${d.level_offset} · 强化索引 ${d.upgrade_index} · 行为标志 ${hex(d.behaviour)} · 附加值 ${d.extra}`, 'basis'));
    root.append(element('p', '字段来自 8020ABB4 生成运行时机体的读取顺序；坐标经 3D3D→801C78A0 的屏幕换算确认，出击位置的写入链保留为结构确认。', 'basis'));
  }
  return root;
}
