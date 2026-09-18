"""Optional rules: corrections for original defects and difficulty options; see docs/rule-fixes.md.

This is a gameplay choice, kept apart from presentation preferences. A first
playtest launch turns the corrections on and leaves the difficulty options off;
the in-game 选项 menu and settings window switch them live, and each session
report records what a run started with.
"""
from __future__ import annotations

import json
from pathlib import Path

RULES_VERSION = 1
SCHEMA = "srw64.rule-settings.v1"
# Same ids and order as the host catalog in tools/recomp/native-host/rule_fixes.hpp.
RULE_FIXES = {
    "esp-level": "超能力的命中/回避补正按技能等级（原版固定 64）",
    "seisenshi-level": "圣战士的回避补正按技能等级（原版固定 32）",
    "limit-cap": "命中、回避与运动性之和不超过机体限界（原版不限制）",
    "potential-bands": "底力按 HP 档位对齐：90% 以上不加成，低于 10% 才到最高档（原版整体提前一档）",
    "potential-half": "底力的命中/回避补正减半，暴击率不变（资料称原版为本应的 2 倍）",
    "weapon-inherit-map": "换机时补上三件漏登记的同名武器改造继承：レイズナー 的火炎放射器与グレネードランチャー、アルトロン 的ドラゴンファイヤー",
    "aura-slash-power": "圣战士的ハイパーオーラ斬り等 L3 解锁武器按技能等级加威力 +200…+1500（原版缺失）",
    "boss-dummy-half": "头目的假身次数减半，至少保留 1 次（原版为部署记录里的 2/3/5/7）",
    "boss-dummy-none": "头目不再拥有假身；与减半同时勾选时以本项为准",
    "upgrade-cap-break": "改造上限突破：所有机体可改到 15 段（原作上限 6～15），换装、追加武器仍按原作上限",
}
# Bug fixes; a first launch turns these on.
CORRECTIONS = ("esp-level", "seisenshi-level", "limit-cap", "potential-bands", "potential-half",
               "weapon-inherit-map", "aura-slash-power")
# Difficulty choices: these change the original balance on purpose, so they start off.
DIFFICULTY = tuple(fix for fix in RULE_FIXES if fix not in CORRECTIONS)
# "fixed" is the corrections only; the difficulty choices need "all" or an
# explicit list, so that --rules fixed never changes the original balance.
PRESETS = {"original": (), "fixed": CORRECTIONS, "all": tuple(RULE_FIXES)}
DEFAULT = CORRECTIONS


def parse(text: str) -> tuple[str, ...]:
    """Comma-separated ids, returned in catalog order; empty selects the original rules."""
    if not text:
        return ()
    ids = text.split(",")
    unknown = [item for item in ids if item not in RULE_FIXES]
    if unknown:
        raise ValueError(f"未知的规则修正 {', '.join(map(repr, unknown))}；可用：{', '.join(RULE_FIXES)}")
    return tuple(item for item in RULE_FIXES if item in ids)


def load(path: Path) -> tuple[str, ...]:
    """The saved choice, or the default set when nothing has been chosen yet.

    An empty saved list is a deliberate "original rules", not an absent file.
    """
    if not path.exists():
        return DEFAULT
    data = json.loads(path.read_text())
    if (not isinstance(data, dict) or data.get("schema") != SCHEMA or not isinstance(data.get("fixes"), list)
            or not all(isinstance(item, str) for item in data["fixes"])):
        raise ValueError(f"规则设置文件无效：{path}；请用 --rules 重新选择")
    return parse(",".join(data["fixes"]))


def save(path: Path, fixes: tuple[str, ...]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps({"schema": SCHEMA, "rules_version": RULES_VERSION, "fixes": list(fixes)},
                                    ensure_ascii=False, indent=2) + "\n")
    temporary.replace(path)


def select(path: Path, preset: str | None = None, fixes: str | None = None) -> tuple[str, ...]:
    """Save an explicit choice for later launches; otherwise reuse the saved one."""
    if preset is not None and fixes is not None:
        raise ValueError("预设与自定义规则只能选一个")
    if preset is None and fixes is None:
        return load(path)
    if preset is not None and preset not in PRESETS:
        raise ValueError(f"未知的规则预设 {preset!r}；可用：{', '.join(PRESETS)}")
    chosen = PRESETS[preset] if preset is not None else parse(fixes)
    save(path, chosen)
    return chosen


def recorded(report: Path) -> tuple[str, ...] | None:
    """Fixes a finished run was played with, or None if its report is unreadable.

    Reports written before this setting existed have no entry; those runs used
    the original rules.
    """
    try:
        data = json.loads(report.read_text())
        entry = data.get("rule_fixes")
        return () if entry is None else parse(",".join(entry["enabled"]))
    except (OSError, ValueError, AttributeError, KeyError, TypeError):
        return None


def describe(fixes: tuple[str, ...]) -> str:
    return "；".join(RULE_FIXES[item] for item in fixes) if fixes else "原版规则（未启用修正）"
