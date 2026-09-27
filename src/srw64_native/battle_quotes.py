"""Which battle quote the game says when: the selection tables of the battle overlay, decoded.

The battle overlay (ROM 0x121560, code at 0x801C2600) fills two quote slots per combatant
(record +0x2C for the attack, +0x848 for the reaction) in ``func_80222050``:

1. ``func_8022245C`` walks the pilot's *special list* (index ``0x121150`` → data ``0x20F250``):
   pairs of (condition code, text offset), text id = 14227 + offset; a pair whose code is
   40000/40001 continues the previous condition (multi-line exchanges, one line per speaker).
   Matching pairs land in three priority groups; one is drawn at random from the best group.
2. ``func_80222B14`` draws from the pilot's *generic block* (table ``0x1161C0``, 36 bytes per
   voice: nine (first offset, count) pairs, text id = 5813 + offset), indexed by situation.
   It runs when no special line matched, or when the special line came from the lowest group
   (weapon lines coded 900–2599): then the generic line replaces it two times out of three.

Pilots are addressed by *voice* (``D_800CA9C4[actor] = voice``, -1 = mute); a few actors share
a voice (the story copies of Domon and the Shuffle Alliance). Situations, as the caller passes
them (generic slot / special-code base):

    0 / 20000 attack        1 / 10000 shot down        2 / 12500 HP left < 30 %
    3 / 15000 HP left 30–90 %                          4 / 17500 HP left ≥ 90 %
    5 / 25000 evaded (27500: evaded by afterimage, result codes 6–12)
    6 / 22500 no damage (barrier, armor)               7 / 30000 cannot counter: no ammo/EN
    8 / 32500 cannot counter: out of range

Special condition codes (``func_8022245C``):

    < 91              row of the condition table D_80222F20 (c, w): c as below, w = weapon
                      (-1 none, 900+id, 2600+id); rows with c in 700–879 use the next row as a
                      story-flag check (flag, value)
    900 + weapon      own weapon, low priority (generic line may replace it)
    2600 + weapon     own weapon, normal priority
    5000              the opponent's pilot is female (pilot flags bit 1)
    8000 + n          combination attack (weapon canonical id 1222–1229 → base 8000–8700),
                      n = partner table [main pilot][partner]; needs battle mode 3
    10000 + 2500·s + k  situation s with condition k: k < 400 own unit id; 400–999 as c below

c values (``func_80221F5C``): < 400 opponent unit id; 400–699 opponent voice + 400;
700–879 story flag (c - 700) value; 880–889 co-pilot (``func_801C4A08``: 880 チャム, 881
シルキー, 882 ローレンス, 883 アイシャ, 884 甲児, 885 ひかる, 886 マリア, 887 鉄也, 888 ジュン,
889 ナイーダ). Weapons compare through field 0 of the weapon record (``0x119970``, 14 bytes
each), so same-named weapons on different units match together.
"""
from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
import struct

GENERIC_FIRST_ID = 5813
SPECIAL_FIRST_ID = 14227
# 5799–5812: two lines per flagship captain, picked by the main program (800A2890) when a
# flagship goes down; the captain order is the list at D_800C9A08.
FLAGSHIP_FIRST_ID = 5799
FLAGSHIP_CAPTAINS = (46, 180, 179, 253, 37, 49, 250)
VOICES = 257  # generic table rows; D_800CA9C4 never yields more

# vaddr → ROM offset for the main segment and the battle overlay
MAIN_DELTA = 0x80076610 - 0x1000
OVERLAY_DELTA = 0x801C2600 - 0x121560

VOICE_OF_ACTOR = 0x800CA9C4 - MAIN_DELTA
GENERIC_TABLE = 0x1161C0
SPECIAL_INDEX = 0x121150
SPECIAL_DATA = 0x20F250
SPECIAL_MAX_BYTES = 0x1E0
COND_TABLE = 0x80222F20 - OVERLAY_DELTA
COND_ROWS = 91
WEAPON_TABLE = 0x119970
WEAPON_RECORD = 14
PARTNER_TABLES = {8000: 0x8022308C, 8100: 0x80223154, 8200: 0x8022321C, 8300: 0x8022308C,
                  8400: 0x8022308C, 8500: 0x8022308C, 8600: 0x8022308C, 8700: 0x802232E4}
COMBO_BASE_OF_CANON = {1222: 8000, 1223: 8100, 1224: 8200, 1225: 8300, 1226: 8400, 1227: 8500,
                       1228: 8600, 1229: 8700}
# func_80221A3C: actor → partner-table index
PARTNER_INDEX = {196: 0, 108: 1, 113: 2, 195: 3, 197: 4, 107: 5, 114: 6, 112: 7, 116: 8, 124: 9}
AURA_PARTNER_INDEX = {181: 0, 184: 1, 185: 2, 186: 3}  # +4 when the partner unit carries チャム (183)
# func_801C4A08: co-pilot actor → condition 880 + slot
COPILOT_SLOT = {183: 0, 182: 1, 24: 2, 32: 3, 196: 4, 114: 5, 113: 6, 108: 7, 107: 8, 116: 9}

SITUATIONS = {
    0: ("攻击", "attack"),
    1: ("被击坠", "shot down"),
    2: ("重伤（剩余 HP 不足 30%）", "heavy damage (HP left < 30%)"),
    3: ("中伤（剩余 HP 30–90%）", "medium damage (HP left 30–90%)"),
    4: ("轻伤（剩余 HP 90% 以上）", "light damage (HP left ≥ 90%)"),
    5: ("回避", "evaded"),
    6: ("攻击无效（护罩／装甲）", "no damage (barrier, armor)"),
    7: ("弹药／EN 不足，无法反击", "cannot counter: no ammo or EN"),
    8: ("射程外，无法反击", "cannot counter: out of range"),
}
SPECIAL_SITUATIONS = {10000: 1, 12500: 2, 15000: 3, 17500: 4, 20000: 0, 22500: 6, 25000: 5,
                      27500: 5, 30000: 7, 32500: 8}


@dataclass
class Trigger:
    kind: str                 # generic | weapon | combo | situation | female | table | continuation
    desc: str                 # Chinese one-liner for dumps, comments and docs
    situation: int | None = None
    code: int | None = None
    priority: int | None = None   # 0 best … 2 lowest (special lists), None for generic
    fields: dict = field(default_factory=dict)
    sequence: list[int] = field(default_factory=list)  # text ids of the exchange this line is part of
    position: int = 0

    def as_dict(self) -> dict:
        out = {"kind": self.kind, "desc": self.desc}
        if self.situation is not None:
            out["situation"] = self.situation
        if self.code is not None:
            out["code"] = self.code
        if self.priority is not None:
            out["priority"] = self.priority
        out.update(self.fields)
        if len(self.sequence) > 1:
            out["sequence"] = self.sequence
            out["position"] = self.position
        return out


class BattleQuoteTables:
    """Decodes the ROM tables; ``names`` map unit/weapon/actor ids to their Japanese names."""

    def __init__(self, rom: bytes, unit_names: dict[int, str], weapon_names: dict[int, str],
                 actor_names: dict[int, str]):
        self.rom = rom
        self.unit_names, self.weapon_names, self.actor_names = unit_names, weapon_names, actor_names
        self.voice_of_actor = {a: self.s16(VOICE_OF_ACTOR + 2 * a) for a in sorted(actor_names)}
        self.actors_of_voice: dict[int, list[int]] = defaultdict(list)
        for actor, voice in self.voice_of_actor.items():
            if voice >= 0:
                self.actors_of_voice[voice].append(actor)
        self.generic = {v: [(self.u16(GENERIC_TABLE + 36 * v + 4 * i), self.s16(GENERIC_TABLE + 36 * v + 4 * i + 2))
                            for i in range(9)] for v in range(VOICES)}
        self.special = {v: self.read_special(v) for v in range(VOICES)}
        self.cond = [(self.s16(COND_TABLE + 4 * i), self.s16(COND_TABLE + 4 * i + 2)) for i in range(COND_ROWS)]
        self.canon = {w: self.s16(WEAPON_TABLE + WEAPON_RECORD * w) for w in sorted(weapon_names)}
        self.weapons_of_canon: dict[int, list[int]] = defaultdict(list)
        for w, c in self.canon.items():
            self.weapons_of_canon[c].append(w)
        self.partner_pairs = {base: self.read_partner_table(base) for base in PARTNER_TABLES}

    # ------------------------------------------------------------------ raw reads
    def u16(self, offset: int) -> int:
        return struct.unpack_from(">H", self.rom, offset)[0]

    def s16(self, offset: int) -> int:
        return struct.unpack_from(">h", self.rom, offset)[0]

    def u32(self, offset: int) -> int:
        return struct.unpack_from(">I", self.rom, offset)[0]

    def read_special(self, voice: int) -> list[tuple[int, int]]:
        """Every (code, offset) pair up to the 0xFFFF terminator. The game copies only
        SPECIAL_MAX_BYTES (120 pairs) into its buffer, so pairs past that index never show
        (デューク, ボス and 甲児 have longer lists); ``triggers`` flags them ``unreachable``."""
        offset = self.u32(SPECIAL_INDEX + 4 * voice)
        if offset == 0 or offset >= 0x20000:
            return []
        pairs, at = [], SPECIAL_DATA + offset
        while len(pairs) < 1024:
            code = self.u16(at)
            if code == 0xFFFF:
                break
            pairs.append((code, self.u16(at + 2)))
            at += 4
        return pairs

    def read_partner_table(self, base: int) -> dict[int, list[tuple[int, int]]]:
        """variant n → [(main index, partner index)] with T[main·stride + partner] == n."""
        stride = 8 if base == 8700 else 10
        table = PARTNER_TABLES[base] - OVERLAY_DELTA
        pairs: dict[int, list[tuple[int, int]]] = defaultdict(list)
        for main in range(stride):
            for partner in range(stride):
                n = self.s16(table + 2 * (main * stride + partner))
                if n != 16:
                    pairs[n].append((main, partner))
        return dict(pairs)

    # ------------------------------------------------------------------ names
    def unit(self, unit_id: int) -> str:
        return f"〈{self.unit_names.get(unit_id, f'机体 {unit_id}')}〉"

    def actor(self, actor_id: int) -> str:
        return f"〈{self.actor_names.get(actor_id, f'人物 {actor_id}')}〉"

    def voice_label(self, voice: int) -> str:
        actors = self.actors_of_voice.get(voice, [])
        names = []
        for a in actors:
            name = self.actor_names.get(a, str(a))
            if name not in names:
                names.append(name)
        return "〈" + "／".join(names) + "〉" if names else f"〈声部 {voice}〉"

    def weapon(self, weapon_id: int) -> str:
        names = []
        for w in self.weapons_of_canon.get(self.canon.get(weapon_id, -2), [weapon_id]):
            name = self.weapon_names.get(w, f"武器 {w}")
            if name not in names:
                names.append(name)
        return "〈" + "／".join(names) + "〉"

    def partner_label(self, base: int, variant: int) -> str:
        index = AURA_PARTNER_INDEX if base == 8700 else PARTNER_INDEX
        by_index = {i: self.actor_names.get(a, str(a)) for a, i in index.items()}
        if base == 8700:
            by_index.update({i + 4: f"{n}（带チャム）" for i, n in list(by_index.items())})
        pairs = self.partner_pairs.get(base, {}).get(variant, [])
        return "、".join(f"{by_index.get(m, m)}＋{by_index.get(p, p)}" for m, p in pairs) or f"变体 {variant}"

    # ------------------------------------------------------------------ conditions
    def describe_c(self, c: int) -> tuple[str, dict]:
        if c < 400:
            return f"对手机体{self.unit(c)}", {"opponent_unit": c}
        if c < 700:
            return f"对手驾驶员{self.voice_label(c - 400)}", {"opponent_voice": c - 400}
        if c < 880:
            return f"剧情标志 {c - 700}", {"flag": c - 700}
        slot = c - 880
        actor = next((a for a, s in COPILOT_SLOT.items() if s == slot), None)
        if actor is None:
            return f"副驾驶槽 {slot}", {"copilot_slot": slot}
        return f"副驾驶{self.actor(actor)}", {"copilot": actor}

    def describe_weapon_code(self, w: int) -> tuple[str, dict]:
        if w >= 2600:
            return "使用" + self.weapon(w - 2600), {"weapon": w - 2600}
        if w >= 900:
            return "使用" + self.weapon(w - 900), {"weapon": w - 900}
        return f"武器码 {w}（按表原样）", {"weapon_raw": w}

    def describe_code(self, code: int) -> tuple[str, dict, int | None, int | None]:
        """→ (desc, fields, situation, priority)."""
        if code < COND_ROWS:
            c, w = self.cond[code]
            desc, fields = self.describe_c(c)
            fields = {"row": code, **fields}
            if 700 <= c < 880 and code + 1 < COND_ROWS:
                flag, value = self.cond[code + 1]
                desc = f"剧情标志 {flag} = {value}"
                fields.update({"flag": flag, "flag_value": value})
            if w != -1:
                wdesc, wfields = self.describe_weapon_code(w)
                desc, fields = f"{desc}，{wdesc}", {**fields, **wfields}
            priority = 2 if (c < 400 or 400 <= c < 700) else 1
            return desc, fields, 0, priority
        if code < 900:
            return f"未知条件码 {code}", {}, None, None
        if code < 2600:
            desc, fields = self.describe_weapon_code(code)
            return desc + "（低优先，可能被通用台词顶掉）", fields, 0, 2
        if code < 5000:
            desc, fields = self.describe_weapon_code(code)
            return desc, fields, 0, 1
        if code == 5000:
            return "对手驾驶员为女性", {"female_opponent": True}, 0, 2
        if 8000 <= code < 8800:
            base, variant = code // 100 * 100, code % 100
            canon = next((k for k, b in COMBO_BASE_OF_CANON.items() if b == base), None)
            name = self.weapon(canon) if canon is not None else f"合体技 {base}"
            return (f"合体技{name}：{self.partner_label(base, variant)}",
                    {"combo": base, "variant": variant, "weapon": canon}, 0, 0)
        if code >= 10000:
            fp = 10000 + (code - 10000) // 2500 * 2500
            k = (code - 10000) % 2500
            situation = SPECIAL_SITUATIONS.get(fp)
            head = SITUATIONS[situation][0] if situation is not None else f"情境 {fp}"
            if fp == 27500:
                head = "分身回避"
            if k < 400:
                return f"{head}，乘坐{self.unit(k)}", {"own_unit": k}, situation, 2
            if k < 1000:
                desc, fields = self.describe_c(k)
                return f"{head}，{desc}", fields, situation, 1
            return f"{head}，条件 {k}", {"k": k}, situation, 1
        return f"未知条件码 {code}", {}, None, None

    # ------------------------------------------------------------------ per text id
    def triggers(self) -> dict[int, list[Trigger]]:
        out: dict[int, list[Trigger]] = defaultdict(list)
        for voice, slots in self.generic.items():
            for situation, (first, count) in enumerate(slots):
                for i in range(count):
                    text_id = GENERIC_FIRST_ID + first + i
                    out[text_id].append(Trigger("generic", f"{SITUATIONS[situation][0]}（通用，{i + 1}/{count}）",
                                                situation=situation, fields={"voice": voice}))
        for voice, pairs in self.special.items():
            sequences: list[tuple[int, list[int], bool]] = []
            for index, (code, offset) in enumerate(pairs):
                text_id = SPECIAL_FIRST_ID + offset
                unreachable = index >= SPECIAL_MAX_BYTES // 4
                if code in (40000, 40001) and sequences:
                    sequences[-1][1].append(text_id)
                else:
                    sequences.append((code, [text_id], unreachable))
            for code, ids, unreachable in sequences:
                desc, fields, situation, priority = self.describe_code(code)
                kind = ("combo" if 8000 <= code < 8800 else "situation" if code >= 10000
                        else "female" if code == 5000 else "table" if code < COND_ROWS else "weapon")
                if unreachable:
                    desc += "（超出原版读取上限，不会出现）"
                    fields = {**fields, "unreachable": True}
                for position, text_id in enumerate(ids):
                    line = desc if len(ids) == 1 else f"{desc}（对话第 {position + 1}/{len(ids)} 句）"
                    out[text_id].append(Trigger(kind, line, situation=situation, code=code, priority=priority,
                                                fields={"voice": voice, **fields}, sequence=ids, position=position))
        for index, captain in enumerate(FLAGSHIP_CAPTAINS):
            for i in range(2):
                out[FLAGSHIP_FIRST_ID + 2 * index + i].append(Trigger(
                    "flagship", f"旗舰被击坠，舰长{self.actor(captain)}（主程序 800A2890，随机二选一）",
                    fields={"captain": captain}))
        return dict(out)

    def voice_summary(self) -> list[dict]:
        rows = []
        for voice in range(VOICES):
            generic = {SITUATIONS[s][1]: [GENERIC_FIRST_ID + first + i for i in range(count)]
                       for s, (first, count) in enumerate(self.generic[voice]) if count > 0}
            special = []
            current = None
            for index, (code, offset) in enumerate(self.special[voice]):
                text_id = SPECIAL_FIRST_ID + offset
                if code in (40000, 40001) and current:
                    current["ids"].append(text_id)
                    continue
                desc, fields, situation, priority = self.describe_code(code)
                current = {"code": code, "desc": desc, "situation": situation, "priority": priority,
                           **fields, "ids": [text_id]}
                if index >= SPECIAL_MAX_BYTES // 4:
                    current["unreachable"] = True
                special.append(current)
            if not generic and not special:
                continue
            rows.append({"voice": voice, "actors": self.actors_of_voice.get(voice, []),
                         "label": self.voice_label(voice), "generic": generic, "special": special})
        return rows
