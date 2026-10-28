// Build-time data for the battle voice maker (pages/[lang]/mods/voices.astro): every battle
// line with its speaker, its text and when the game shows it, worded in the page's language.
// The text is the published table (docs/guide/audio-mods/battle-voices.csv); when a line shows
// comes from data/voices.json (tools/content/export_audio_tables.py), as fields named here.
import { tl, type Lang, type TextLang } from '../i18n';
import table from '../../../docs/guide/audio-mods/battle-voices.csv?raw';
import voices from '../data/voices.json';
import bgm from '../data/bgm.json';
import data from '../data/data-mods.json';

type Names = { ja: string; zh: string; en: string };
type Scene = {
  k: string; s?: number; ai?: number; g?: number[]; lo?: number; w?: number; wr?: number; u?: number; ou?: number;
  ov?: number; cp?: number; c?: number; cap?: number; f?: number[]; fe?: number; q?: number[]; x?: number;
};

// RFC 4180: quoted fields may hold commas, quotes ("") and line breaks.
function csv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [], field = '', quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"' && text[i + 1] === '"') { field += '"'; i++; }
      else if (c === '"') quoted = false;
      else field += c;
    } else if (c === '"') quoted = true;
    else if (c === ',') { row.push(field); field = ''; }
    else if (c === '\n') { row.push(field); rows.push(row); row = []; field = ''; }
    else if (c !== '\r') field += c;
  }
  if (field || row.length) { row.push(field); rows.push(row); }
  return rows;
}

// The words, by the fields of a trigger (src/srw64_native/battle_quotes.py describes them).
const W: Record<TextLang, {
  situations: string[]; afterimage: string; general: string; weapon: string; low: string; unit: string; vsUnit: string;
  vsPilot: string; copilot: string; flag: string; flagIs: string; condition: string; female: string; combo: string;
  flagship: string; exchange: string; never: string; open: string; close: string; sep: string; or: string; name: (n: string) => string;
}> = {
  zh: {
    situations: ['攻击', '被击坠', '重伤·HP 不足 30%', '中伤·HP 30–90%', '轻伤·HP 90% 以上', '回避', '攻击无效·护罩或装甲', '无法反击·弹药或 EN 不足', '无法反击·射程外'],
    afterimage: '分身回避', general: '通用 {i}/{n}', weapon: '使用{w}', low: '可能被通用台词顶掉', unit: '乘坐{u}', vsUnit: '对手机体{u}',
    vsPilot: '对手{p}', copilot: '副驾驶{p}', flag: '剧情标志 {f}', flagIs: '剧情标志 {f} = {v}', condition: '条件 {c}', female: '对手是女性',
    combo: '合体技{w}', flagship: '旗舰被击坠（舰长，二选一）', exchange: '对话第 {i}/{n} 句', never: '原版不会出现',
    open: '（', close: '）', sep: '，', or: '／', name: (n) => `〈${n}〉`,
  },
  en: {
    situations: ['Attack', 'Shot down', 'Heavy damage, HP < 30%', 'Medium damage, HP 30–90%', 'Light damage, HP ≥ 90%', 'Evade', 'No damage, barrier or armor', 'Can’t counter, no ammo or EN', 'Can’t counter, out of range'],
    afterimage: 'Evade by afterimage', general: 'general, {i}/{n}', weapon: 'using {w}', low: 'a general line may win', unit: 'in {u}', vsUnit: 'vs {u}',
    vsPilot: 'vs {p}', copilot: 'co-pilot {p}', flag: 'story flag {f}', flagIs: 'story flag {f} = {v}', condition: 'condition {c}', female: 'vs a woman',
    combo: 'Combination attack {w}', flagship: 'Flagship shot down (captain, one of two)', exchange: 'exchange line {i}/{n}', never: 'never shown in the original',
    open: ' (', close: ')', sep: ', ', or: ' / ', name: (n) => n,
  },
  ja: {
    situations: ['攻撃', '撃墜', '大ダメージ・HP 30% 未満', '中ダメージ・HP 30〜90%', '小ダメージ・HP 90% 以上', '回避', 'ダメージなし・バリアか装甲', '反撃不可・弾切れか EN 不足', '反撃不可・射程外'],
    afterimage: '分身で回避', general: '汎用、{i}/{n}', weapon: '{w}使用', low: '汎用台詞が優先されることあり', unit: '{u}搭乗', vsUnit: '相手ユニット{u}',
    vsPilot: '相手{p}', copilot: 'サブパイロット{p}', flag: 'シナリオフラグ {f}', flagIs: 'シナリオフラグ {f} = {v}', condition: '条件 {c}', female: '相手が女性',
    combo: '合体技{w}', flagship: '旗艦撃墜（艦長、二つから一つ）', exchange: '掛け合い {i}/{n}', never: '原作では出ない',
    open: '（', close: '）', sep: '、', or: '／', name: (n) => `「${n}」`,
  },
};
const put = (s: string, v: Record<string, string | number>) => s.replace(/\{(\w+)\}/g, (_, k) => String(v[k] ?? ''));

export function voiceLines(lang: Lang) {
  const t = tl(lang), w = W[t];
  const local = (n: Names | undefined, fallback: string) => (n ? n[t] || n.ja : fallback);
  const actors = new Map<number, Names>(bgm.pilots.map((p) => [p.id, p.name]));
  const units = new Map<number, Names>(data.units.map((u) => [u.id, u.name]));
  const weapons = new Map<number, Names>(data.weapons.map((x) => [x.id, x.name]));
  const actorsOfVoice = voices.actorsOfVoice as Record<string, number[]>;
  const actor = (id: number) => w.name(local(actors.get(id), String(id)));
  const voiceOf = (v: number) => {
    const names = [...new Set((actorsOfVoice[String(v)] ?? []).map((a) => local(actors.get(a), String(a))))];
    return w.name(names.length ? names.join(w.or) : String(v));
  };

  function label(scenes: Scene[]): { text: string; never: boolean } {
    if (!scenes.length) return { text: w.never, never: true };
    const one = (x: Scene) => {
      const head = x.k === 'flagship' ? w.flagship : x.k === 'combo' ? put(w.combo, { w: w.name(local(weapons.get(x.w!), String(x.w))) })
        : x.ai ? w.afterimage : w.situations[x.s ?? 0] ?? w.situations[0];
      const more: string[] = [];
      if (x.g) more.push(put(w.general, { i: x.g[0], n: x.g[1] }));
      if (x.k !== 'combo' && x.w !== undefined) more.push(put(w.weapon, { w: w.name(local(weapons.get(x.w), String(x.w))) }));
      if (x.u !== undefined) more.push(put(w.unit, { u: w.name(local(units.get(x.u), String(x.u))) }));
      if (x.ou !== undefined) more.push(put(w.vsUnit, { u: w.name(local(units.get(x.ou), String(x.ou))) }));
      if (x.ov !== undefined) more.push(put(w.vsPilot, { p: voiceOf(x.ov) }));
      if (x.cp !== undefined) more.push(put(w.copilot, { p: actor(x.cp) }));
      if (x.f) more.push(x.f.length > 1 ? put(w.flagIs, { f: x.f[0], v: x.f[1] }) : put(w.flag, { f: x.f[0] }));
      if (x.c !== undefined) more.push(put(w.condition, { c: x.c }));
      if (x.fe) more.push(w.female);
      if (x.lo) more.push(w.low);
      if (x.q) more.push(put(w.exchange, { i: x.q[0], n: x.q[1] }));
      if (x.x) more.push(w.never);
      return more.length ? `${head}${w.open}${more.join(w.sep)}${w.close}` : head;
    };
    return { text: [...new Set(scenes.map(one))].join(t === 'en' ? '; ' : '；'), never: scenes.every((x) => x.x) };
  }

  const scenes = new Map<number, Scene[]>((voices.scenes as [number, Scene[]][]).map(([r, s]) => [r, s]));
  const rows = csv(table);
  const head = rows[0], col = (name: string) => head.indexOf(name);
  const [cRecord, cSpeaker, cJa, cZh, cEn] = ['record', 'speaker', 'text_ja', 'text_zh', 'text_en'].map(col);
  const [nJa, nZh, nEn] = ['speaker_ja', 'speaker_zh', 'speaker_en'].map(col);
  const labels: string[] = [], labelIndex = new Map<string, number>();
  const lines: [number, number, number, string, number][] = [];   // record, speaker, label, text, never
  const ja: string[] = [];
  const speakers = new Map<number, { id: number; name: Names; lines: number }>();
  for (const r of rows.slice(1)) {
    if (r.length < head.length) continue;
    const record = Number(r[cRecord]), speaker = Number(r[cSpeaker]);
    const l = label(scenes.get(record) ?? []);
    if (!labelIndex.has(l.text)) { labelIndex.set(l.text, labels.length); labels.push(l.text); }
    const text = t === 'ja' ? r[cJa] : (t === 'zh' ? r[cZh] : r[cEn]) || r[cJa];
    lines.push([record, speaker, labelIndex.get(l.text)!, text, l.never ? 1 : 0]);
    ja.push(t === 'ja' ? '' : r[cJa]);
    const s = speakers.get(speaker) ?? { id: speaker, name: actors.get(speaker) ?? { ja: r[nJa], zh: r[nZh] || r[nJa], en: r[nEn] || r[nJa] }, lines: 0 };
    s.lines++;
    speakers.set(speaker, s);
  }
  return { labels, lines, ja, speakers: [...speakers.values()] };
}
