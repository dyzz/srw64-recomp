"""Named ability indexes over original flags, with bidirectional holder links.

Counts describe original record identities, not deployments or unique characters.
Runtime additions and learned skills are deliberately separate from flag holders.
"""
from __future__ import annotations

from copy import deepcopy
import struct

from .catalog import text_key
from .original_data import checked_slice, link, raw_record


def flag_value(raw: bytes, offset: int, size: int) -> int:
    return int.from_bytes(checked_slice(raw, offset, size), "big")


def attach_ability_catalog(categories: dict, rom: bytes, layout: dict, sources: dict) -> dict:
    """Enrich existing profiles and return an explicit coverage audit."""
    evidence = {e["id"]: e for e in layout["evidence"]}

    def name(tid):
        return sources[text_key(0, tid)].replace("<END>", "").replace("<BR>", " ")

    def definition(category, spec, pilot=False):
        if "ui_index" in spec:
            config = layout["unit_abilities"]
            offset = config["ui_table_rom_offset"] + spec["ui_index"] * config["ui_table_stride"]
            raw = checked_slice(rom, offset, config["ui_table_stride"])
            mask, tid, width, reserved = struct.unpack(">IHBB", raw)
            if mask != spec["mask"]:
                raise ValueError("Ability mask differs from original UI table")
            label = name(tid)
            if mask in (4, 8):
                label += " · " + ("10%" if mask == 4 else "20%")
        else:
            source = evidence["pilot_skill_names" if pilot else spec["source_evidence"]]
            offset = source["rom_offset"]
            raw = checked_slice(rom, offset, source["byte_size"])
            tid, label = spec.get("text_id"), spec["name"]
        identity = f"{spec['mask']:02x}" if pilot else spec["id"]
        row = raw_record(f"base:{category}:{identity}", label, raw, offset, "code-confirmed")
        if spec.get("label_confidence"):
            row["label_confidence"] = spec["label_confidence"]
        row["definition"] = deepcopy(spec) | {"kind": "pilot" if pilot else "unit", "members": []}
        row["evidence"] = (layout["pilot_skills"]["evidence"] if pilot else spec["evidence"]).copy()
        ids = range(spec["rank_text_base"] + 1, spec["rank_text_base"] + 10) if pilot else ([] if tid is None else [tid])
        row["links"] = [link(text_key(0, i), "原始名称／等级文本") for i in ids]
        row["fields"] = [{"name": "定义依据", "value": "原始显示表条目" if "ui_index" in spec else "原始消费代码；功能名称见定义说明",
                          "confidence": "code-confirmed"},
                         {"name": "持有判定", "value": {k: spec[k] for k in ("offset", "size", "mask")},
                          "confidence": "code-confirmed"}]
        return row

    units = [definition("unit_abilities", d) for d in layout["unit_abilities"]["definitions"]]
    pilots = [definition("pilot_skills", d | {"offset": layout["pilot_skills"]["flags_offset"], "size": 1,
              "family": "特殊技能", "note": "按原始技能标志统计人物身份；习得等级来自该人物映射的阈值表。技能效果还受装备和战斗条件限制。"}, True)
              for d in layout["pilot_skills"]["definitions"]]
    categories["unit_abilities"], categories["pilot_skills"] = units, pilots
    pilot_by_mask = {d["definition"]["mask"]: d for d in pilots}

    def add_link(owner, key, relation):
        reference = link(key, relation)
        if reference not in owner["links"]:
            owner["links"].append(reference)

    def join(owner, ability, extra=None):
        d = ability["definition"]
        d["members"].append({"key": owner["key"], "label": owner["label"]} | (extra or {}))
        add_link(ability, owner["key"], "原始标志持有者")
        add_link(owner, ability["key"], "特殊能力／技能定义")
        return {"key": ability["key"], "label": ability["label"], "alias": d["alias"],
                "family": d["family"], "note": d["note"], "label_confidence": ability.get("label_confidence", "code-confirmed")}

    audit = {"unit_total": len(categories["units"]), "actor_total": len(categories["actors"]),
             "unit_unknown_bits": [], "pilot_unknown_bits": [], "actors_without_stats": [],
             "unit_without_abilities": [], "actors_without_skill_flags": [],
             "actors_without_thresholds": []}
    known = {}
    for spec in layout["unit_abilities"]["definitions"]:
        pair = (spec["offset"], spec["size"])
        known[pair] = known.get(pair, 0) | spec["mask"]
    for row in categories["units"]:
        raw, profile = bytes.fromhex(row["raw_hex"]), row["profile"]
        profile["abilities"] = []
        profile["ability_flags"] = []
        for (offset, size), mask in known.items():
            value = flag_value(raw, offset, size)
            unknown = value & ~mask
            profile["ability_flags"].append({"offset": offset, "size": size, "raw": value, "unknown_bits": unknown})
            if unknown:
                audit["unit_unknown_bits"].append({"key": row["key"], "offset": offset, "bits": unknown})
        for ability in units:
            spec = ability["definition"]
            if flag_value(raw, spec["offset"], spec["size"]) & spec["mask"]:
                profile["abilities"].append(join(row, ability))
        if not profile["abilities"]:
            audit["unit_without_abilities"].append(row["key"])
        row["summary"] = f"{len(profile['abilities'])} 项能力／功能 · {sum(w['matches_form'] for w in profile['weapons'])} 项武器"
        row["search_terms"] = " ".join([w["label"] for w in profile["weapons"]] + [f["label"] for f in profile["related_forms"]]
            + [a["label"] + " " + a["alias"] for a in profile["abilities"]])

    for row in categories["actors"]:
        p = row["profile"]
        if not row["has_stats"]:
            audit["actors_without_stats"].append(row["key"])
            continue
        base_key = p["stats"][0]["source_key"]
        base = categories["pilot_stats"][int(base_key.rsplit(":", 1)[1])]
        flags = next(f["value"] for f in base["fields"] if f["name"] == "技能标志")
        if flags["unknown_bits"]:
            audit["pilot_unknown_bits"].append({"key": row["key"], "bits": flags["unknown_bits"]})
        if not p["skills"]:
            audit["actors_without_skill_flags"].append(row["key"])
        if not any(r["key"].startswith("base:pilot_thresholds:") for r in p["source_records"]):
            audit["actors_without_thresholds"].append(row["key"])
        for skill in p["skills"]:
            d = pilot_by_mask[skill["mask"]]
            ranks = skill["ranks"]
            active = [r for r in ranks if r is not None] if ranks is not None else None
            extra = {"base_source_key": base_key, "threshold_source_key": skill["source_key"] if ranks is not None else None,
                     "ranks": deepcopy(ranks), "first_level": min(active) if active else None,
                     "max_rank": len(active) if active is not None else None,
                     "selected_for_display": skill["selected_for_display"]}
            reference = join(row, d, extra)
            skill["definition_key"], skill["alias"] = reference["key"], reference["alias"]
            skill["equipment_note"] = {1: "还需机体具备切落对应装备，并满足攻击类型条件。",
                                       2: "还需机体具备盾装备。"}.get(skill["mask"])
        row["search_terms"] = " ".join([p["full_name"]] + [s["command_name"] for s in p["spirits"]]
            + [s["name"] + " " + s["alias"] for s in p["skills"]])

    for row in units + pilots:
        d = row["definition"]
        members = d["members"]
        d["holder_count"] = len(members)
        if d["kind"] == "pilot":
            d["learnable_count"] = sum(m["max_rank"] is not None and m["max_rank"] > 0 for m in members)
            d["zero_threshold_count"] = sum(m["max_rank"] == 0 for m in members)
            d["missing_threshold_count"] = sum(m["max_rank"] is None for m in members)
            d["unique_stats_count"] = len({m["base_source_key"] for m in members})
        row["summary"] = f"{d['family']} · {len(members)} 个{'人物身份' if d['kind'] == 'pilot' else '机体／形态'}持有"
        row["search_terms"] = " ".join([d["alias"], d["family"]] + [m["label"] for m in members])
    audit["unit_definition_count"], audit["pilot_definition_count"] = len(units), len(pilots)
    audit["unit_memberships"] = sum(d["definition"]["holder_count"] for d in units)
    audit["pilot_memberships"] = sum(d["definition"]["holder_count"] for d in pilots)
    return audit
