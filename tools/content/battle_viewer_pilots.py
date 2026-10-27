"""Who can pilot each unit, for the battle viewer's pilot list (src/host/battle_viewer_pilots.inc).

The viewer offers a unit only the pilots the original lets fly it, so every pairing it
plays is one the game's battle quotes were written for:
  - every pilot a stage deploys in the unit (the stage deployment records: pilot +6,
    unit +0xA, as assets/original-data lists them; not the records marked 3, whose
    pilot is a stand-in);
  - the stage scripts' 3D5A (pilot, parameter, unit, mode / old unit) registrations,
    read from assets/original-data/records/stage_events.jsonl: a named pilot with the
    unit it boards (スイームルグS, 真・ゲッター1, ラー・カイラム, 銀鈴ロボ), and a swap that
    names only the old machine (mode below 2000, not 500 / 999) carries its pilots over;
  - のりかえ: a pilot whose category (pilot record +0xB) is not 0 and a unit of the same
    category (unit record +0x12), category 1 also flying 7 and 3 also 4 (801C5618,
    docs/native/native-swap-screens.md);
  - and each of those with every form of the unit: the units a weapon list entry names
    together (transforming, combining) and the super modes (form table D_80218A34);
  - a 変形 unit (ability bit +0x1C 0x02) nobody flies yet takes the pilots of the 変形
    units sharing its weapon list (the list pointer at 0x7E210): ゲッター2/3 and the other
    Getter forms, ザンボエース, 銀鈴ロボTYPE2. Only when it has none, as ダンクーガ's twelve
    beast forms share one list too and each beast has its own pilot.

    .venv/bin/python tools/content/battle_viewer_pilots.py
"""
from __future__ import annotations

from collections import Counter, defaultdict
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'src/host/battle_viewer_pilots.inc'
UNITS, UNIT_SIZE, UNIT_COUNT = 0x71B80, 0x24, 363
PILOT_MAP, PILOT_STATS, PILOT_SIZE, ACTORS = 0x800CA9C4 - 0x80075610, 0x7A1A0, 0x10, 361
WEAPON_LISTS = 0x7E210
SUPER_MODES, SUPER_ROWS = 0x101594, 9
SWAP_ALSO = {1: {7}, 3: {4}}
TRANSFORMS = 0x02   # the ability word's 変形 bit (its low byte, +0x1F)
EVENTS = ROOT / 'assets/original-data/records/stage_events.jsonl'
NOBODY, KEEP_MODES = 999, (500, 999)   # 3D5A: no pilot / no machine; modes that are not a swap


def registrations() -> list[tuple[int, int, int, int]]:
    """Every 3D5A in the stage events, as (pilot, parameter, unit, mode). The events are
    16-bit words; the opcode never occurs as an argument (every hit's pilot and unit are
    in range), so a plain scan is enough."""
    out = []
    for line in EVENTS.read_text().splitlines():
        words = json.loads(line)['raw_hex']
        for at in range(0, len(words) - 16, 4):
            if words[at:at + 4] == '3d5a':
                out.append(tuple(int(words[at + 4 + 4 * k:at + 8 + 4 * k], 16) for k in range(4)))
    return out


def main() -> None:
    rom = (ROOT / 'rom.z64').read_bytes()
    u16 = lambda a: struct.unpack_from('>H', rom, a)[0]
    s16 = lambda a: struct.unpack_from('>h', rom, a)[0]
    pairs: dict[int, set[int]] = defaultdict(set)
    deployed: Counter[tuple[int, int]] = Counter()
    # Stage deployments.
    index = json.loads((ROOT / 'assets/original-data/indexes/stage_deployments.json').read_text())
    for row in index:
        # Records the stage marks 3 hold a stand-in pilot (guerrillas in ゴッドガンダム).
        if '记录值 3' in row.get('summary', ''):
            continue
        at = row['rom_offset']
        pilot, unit = s16(at + 6), s16(at + 0xA)
        if 0 <= pilot < ACTORS and 0 <= unit < UNIT_COUNT:
            pairs[unit].add(pilot)
            deployed[unit, pilot] += 1
    # Script registrations: a pilot boarding a unit counts like a deployment; a swap from
    # an old machine hands that machine's pilots to the new one (after the pairs so far).
    swaps = []
    registered: Counter[tuple[int, int]] = Counter()   # the default only where no stage deploys the unit
    for pilot, _, unit, mode in registrations():
        if unit == NOBODY or unit >= UNIT_COUNT:
            continue
        if pilot != NOBODY and pilot < ACTORS:
            pairs[unit].add(pilot)
            registered[unit, pilot] += 1
        elif mode not in KEEP_MODES and mode < 2000:
            swaps.append((unit, mode))
    for _ in range(3):
        for unit, old in swaps:
            pairs[unit] |= pairs[old]
    # のりかえ categories.
    def pilot_category(actor: int) -> int:
        record = s16(PILOT_MAP + 2 * actor)
        return rom[PILOT_STATS + record * PILOT_SIZE + 0xB] if record >= 0 else 0
    unit_category = {u: rom[UNITS + u * UNIT_SIZE + 0x12] for u in range(UNIT_COUNT)}
    for actor in range(ACTORS):
        category = pilot_category(actor)
        if not category:
            continue
        flies = {category} | SWAP_ALSO.get(category, set())
        for unit, c in unit_category.items():
            if c in flies:
                pairs[unit].add(actor)
    # Forms: a weapon list entry's units, and the super modes.
    forms: dict[int, set[int]] = defaultdict(set)
    for unit in range(UNIT_COUNT):
        at = WEAPON_LISTS + struct.unpack_from('>I', rom, WEAPON_LISTS + unit * 4)[0]
        while u16(at) != 0xFFFF:
            group = {u16(at + 2 * k) for k in range(1, 6)} - {0xFFFF}
            if unit in group:
                forms[unit] |= group
            at += 12
    for row in range(SUPER_ROWS):
        normal, strong = s16(SUPER_MODES + 4 * row), s16(SUPER_MODES + 4 * row + 2)
        forms[normal].add(strong); forms[strong].add(normal)
    for _ in range(3):
        for unit in list(pairs):
            for other in forms.get(unit, ()):
                if 0 <= other < UNIT_COUNT:
                    pairs[other] |= pairs[unit]
    # 変形 forms sharing a weapon list: an unflown one borrows the flown ones' pilots.
    by_list: dict[int, set[int]] = defaultdict(set)
    for unit in range(UNIT_COUNT):
        if rom[UNITS + unit * UNIT_SIZE + 0x1F] & TRANSFORMS:
            by_list[struct.unpack_from('>I', rom, WEAPON_LISTS + unit * 4)[0]].add(unit)
    for family in by_list.values():
        flown = set().union(*(pairs[u] for u in family if u in pairs))
        for unit in family:
            if flown and not pairs.get(unit):
                pairs[unit] |= flown
                forms[unit] |= family - {unit}
    # Deployment counts per unit, a form without any borrowing its other forms'.
    counts: dict[int, Counter[int]] = defaultdict(Counter)
    for (unit, pilot), n in deployed.items():
        counts[unit][pilot] += n
    for (unit, pilot), n in registered.items():
        if not any(counts[unit].values()) and pilot in pairs[unit]:
            counts[unit][pilot] += n
    for unit in pairs:
        if not counts[unit]:
            for other in sorted(forms.get(unit, ())):
                counts[unit] += Counter({p: n for p, n in counts.get(other, Counter()).items() if p in pairs[unit]})
    def best(counter: Counter[int], allowed: list[int]) -> int:
        return max(allowed, key=lambda k: (counter.get(k, 0), -k))
    lines = ['// Generated by tools/content/battle_viewer_pilots.py: {unit, default pilot, {pilots}} the',
             '// battle viewer offers. Unit-major; see the tool for the rules.']
    total = 0
    for unit in sorted(pairs):
        actors = sorted(pairs[unit])
        total += len(actors)
        lines.append('{%d,%d,{%s}},' % (unit, best(counts[unit], actors), ','.join(map(str, actors))))
    OUT.write_text('\n'.join(lines) + '\n')
    print(len(pairs), 'units,', total, 'pairs ->', OUT.relative_to(ROOT))


if __name__ == '__main__':
    main()
