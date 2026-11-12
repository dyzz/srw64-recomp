// The <meta name="description"> of the pages made from game data: one sentence per page,
// from the page's own numbers and names, so that no two of them share a description.
import { listSep, tl, type Lang } from '../i18n';
import { loc, type Person, type Unit } from './data';

/** `name (series)`, in the page language's own brackets; the bare name without a series. */
const named = (lang: Lang, name: string, series: string) =>
  !series ? name : tl(lang) === 'en' ? `${name} (${series})` : tl(lang) === 'ko' ? `${name}(${series})` : `${name}（${series}）`;

export const unitDescription = (lang: Lang, u: Unit, series: string) => {
  const who = named(lang, loc(u.name, lang), series);
  const has = u.weapons.length > 0;
  return {
    zh: `${who}在《超级机器人大战64》中的机体数据：HP ${u.hp}、EN ${u.en}、移动力 ${u.move}、运动性 ${u.mobility}、装甲 ${u.armor}${has ? '，以及各武器的攻击力、射程和改造费用' : ''}。`,
    en: `${who} in Super Robot Wars 64: HP ${u.hp}, EN ${u.en}, move ${u.move}, mobility ${u.mobility}, armor ${u.armor}${has ? ', with the power, range and upgrade costs of its weapons' : ''}.`,
    ja: `『スーパーロボット大戦64』の${who}のユニットデータ。HP ${u.hp}、EN ${u.en}、移動力 ${u.move}、運動性 ${u.mobility}、装甲 ${u.armor}。${has ? '武器の攻撃力・射程・改造費用も掲載。' : ''}`,
    ko: `『슈퍼로봇대전64』 ${who}의 유닛 데이터: HP ${u.hp}, EN ${u.en}, 이동력 ${u.move}, 운동성 ${u.mobility}, 장갑 ${u.armor}${has ? ', 무기의 공격력·사정거리·개조 비용' : ''}.`,
  }[tl(lang)];
};

export const personDescription = (lang: Lang, p: Person, series: string) => {
  const who = named(lang, loc(p.name, lang), series);
  if (!p.stats) {
    return {
      zh: `${who}在《超级机器人大战64》中的人物资料。`,
      en: `${who}, a character of Super Robot Wars 64.`,
      ja: `『スーパーロボット大戦64』の${who}のキャラクターデータ。`,
      ko: `『슈퍼로봇대전64』 ${who}의 캐릭터 데이터.`,
    }[tl(lang)];
  }
  const spirits = p.spirits.map((s) => loc(s.name, lang)).join(listSep(lang));
  return {
    zh: `${who}在《超级机器人大战64》中的驾驶员数据：Lv1 至 Lv99 的能力成长、地形适应、特殊技能${spirits ? `，精神指令：${spirits}` : ''}。`,
    en: `${who} in Super Robot Wars 64: pilot stats from level 1 to 99, terrain ratings and skills${spirits ? `, and the spirit commands ${spirits}` : ''}.`,
    ja: `『スーパーロボット大戦64』の${who}のパイロットデータ。Lv1〜Lv99 の能力値、地形適応、特殊技能${spirits ? `、精神コマンド（${spirits}）` : ''}を掲載。`,
    ko: `『슈퍼로봇대전64』 ${who}의 파일럿 데이터: Lv1~Lv99 능력치, 지형 적응, 특수 기능${spirits ? `, 정신 커맨드(${spirits})` : ''}.`,
  }[tl(lang)];
};

/** A story scene: `stage` is the page's own "Stage 12" label, empty for a scene outside the stages. */
export const sceneDescription = (lang: Lang, stage: string, title: string, lines: number) =>
  ({
    zh: `《超级机器人大战64》${stage}「${title}」的剧情台词全文：日文原文与译文对照，共 ${lines} 句。`,
    en: `The full script of ${stage ? `${stage}, ` : ''}“${title}” in Super Robot Wars 64: ${lines} lines, the Japanese original beside its translation.`,
    ja: `『スーパーロボット大戦64』${stage}「${title}」のシナリオ台詞全文。日本語の原文と訳文を並べて掲載（全${lines}行）。`,
    ko: `『슈퍼로봇대전64』 ${stage ? `${stage} ` : ''}「${title}」의 시나리오 대사 전문: 일본어 원문과 번역문 대조, 총 ${lines}행.`,
  })[tl(lang)];

/** A lead paragraph written with links in it, as plain text. */
export const plainText = (html: string) => html.replace(/<a\b[^>]*>.*?<\/a>/g, '').replace(/<[^>]+>/g, '').trim();
