"""Entity views over confirmed original records; never infer a default pilot.

Profiles are a read-only projection. Original keys, bytes and mapping sentinels
remain authoritative, including distinct identities sharing a source record.
"""
from __future__ import annotations

from copy import deepcopy

from .original_data import link


def skill_ranks(thresholds: list[int]) -> list[int | None]:
    # The game counts all nonzero thresholds <= level, not the array position.
    active = sorted(v for v in thresholds if v > 0)
    return active + [None] * (len(thresholds) - len(active))


def attach_profiles(categories: dict, sources: dict[str, str]) -> None:
    records = {r["key"]: r for rows in categories.values() for r in rows}

    def ref(row: dict) -> dict:
        return {"key": row["key"], "label": row["label"]}

    def attach(owner: dict, target: dict, relation: str, reverse: bool = False) -> dict:
        reference = link(target["key"], relation)
        if reference not in owner["links"]:
            owner["links"].append(reference)
        if reverse:
            target.setdefault("related_entities", [])
            if ref(owner) not in target["related_entities"]:
                target["related_entities"].append(ref(owner))
            backward = link(owner["key"], "整合档案")
            if backward not in target["links"]:
                target["links"].append(backward)
        return ref(target)

    def resolve(row: dict, category: str) -> dict | None:
        keys = {l["key"] for l in row["links"] if l["key"].startswith(f"base:{category}:")}
        if len(keys) > 1:
            raise ValueError(f"Ambiguous {category} mapping: {row['key']}")
        return records[next(iter(keys))] if keys else None

    def stats(row: dict | None) -> list[dict]:
        return [deepcopy(f) | {"source_key": row["key"]} for f in row["fields"] if "id" in f] if row else []

    for unit in categories["units"]:
        uid = int(unit["key"].rsplit(":", 1)[1])
        profile = {"kind": "unit", "stats": stats(unit), "weapons": [], "related_forms": [],
                   "observed_pilots": [], "notices": []}
        forms = {}
        for entry in unit["weapon_list"]["rows"]:
            weapon = records[f"base:weapons:{entry['weapon_id']:04d}"]
            attach(unit, weapon, "武器列表引用", reverse=True)
            eligible = []
            for form_id in entry["eligible_unit_ids"]:
                form = records[f"base:units:{form_id:04d}"]
                eligible.append(attach(unit, form, "武器匹配形态"))
                if form_id != uid:
                    forms[form["key"]] = ref(form)
            profile["weapons"].append(ref(weapon) | {"menu_label": weapon["menu_label"],
                "weapon_traits": deepcopy(weapon["weapon_traits"]),
                "stats": stats(weapon), "matches_form": uid in entry["eligible_unit_ids"],
                "eligible_forms": eligible, "list_rom_offset": entry["rom_offset"]})
        profile["related_forms"] = list(forms.values())
        unit["profile"] = profile
        unit["summary"] = f"{sum(w['matches_form'] for w in profile['weapons'])} 项武器匹配当前形态"
        unit["search_terms"] = " ".join([w["label"] for w in profile["weapons"]] + [f["label"] for f in forms.values()])

    for actor in categories["actors"]:
        base, spirits, thresholds = (resolve(actor, c) for c in ("pilot_stats", "spirits", "pilot_thresholds"))
        full_key = next(l["key"] for l in actor["links"] if l["relation"] == "全名")
        profile = {"kind": "pilot", "full_name": sources[full_key].replace("<END>", "").replace("<BR>", " "),
                   "stats": stats(base), "spirits": [], "skills": [], "observed_units": [],
                   "source_records": [], "notices": [f["basis"] for f in actor["fields"] if f.get("basis")]}
        for table in ("actor_stats_map", "actor_spirits_map"):
            mapping = resolve(actor, table)
            if mapping:
                profile["source_records"].append(attach(actor, mapping, "原始映射", reverse=True))
                value = mapping["fields"][0]["value"]
                if value < 0:
                    part = "能力" if table == "actor_stats_map" else "精神"
                    profile["notices"].append(f"{part}映射为 {value}，未关联固定记录；保留原值，不能据此断定角色没有该能力。")
        for source in (base, spirits, thresholds):
            if source:
                profile["source_records"].append(attach(actor, source, "原始档案来源", reverse=True))
        if spirits:
            profile["spirits"] = [deepcopy(c) | {"source_key": spirits["key"], "slot": i + 1}
                                  for i, c in enumerate(spirits["fields"][0]["value"])]
            for command in profile["spirits"]:
                reference = link(command["text_key"], "精神名称")
                if reference not in actor["links"]:
                    actor["links"].append(reference)
        if base:
            flags = next(f["value"] for f in base["fields"] if f["name"] == "技能标志")
            if flags["unknown_bits"]:
                profile["notices"].append(f"技能标志还有未解释位 {flags['unknown_bits']:#04x}。")
            for skill in flags["enabled"]:
                group = skill["threshold_group"]
                raw_levels = thresholds["fields"][group * 2]["value"] if thresholds else None
                profile["skills"].append(deepcopy(skill) | {
                    "ranks": skill_ranks(raw_levels) if raw_levels is not None else None,
                    "source_key": thresholds["key"] if thresholds else base["key"],
                    "selected_for_display": group != 0 or skill["name"] == flags["shared_group_name"]})
        actor["profile"] = profile
        actor["has_stats"] = bool(base)
        spirit_summary = f"精神 {len(profile['spirits'])} 项" if spirits else "精神未关联"
        actor["summary"] = f"能力已关联 · {spirit_summary}" if base else "能力未关联 · 保留名称身份"
        actor["search_terms"] = " ".join([profile["full_name"]] + [c["command_name"] for c in profile["spirits"]]
                                         + [s["name"] for s in profile["skills"]])

    # These edges describe one observed allocation, not a default assignment or
    # the set of allowed pilot/unit pairings throughout the game.
    for observation in categories.get("observations", []):
        for instance in observation["instances"]:
            unit = records[instance["unit_key"]]
            for pilot in instance["pilots"]:
                actor = records[pilot["actor_key"]]
                for owner, target, collection in [(unit, actor, "observed_pilots"), (actor, unit, "observed_units")]:
                    relation = ref(target) | {"observation_key": observation["key"],
                                             "confidence": "snapshot-observed"}
                    if relation not in owner["profile"][collection]:
                        owner["profile"][collection].append(relation)
                    for key, label in [(target["key"], "历史快照搭乘关系"), (observation["key"], "搭乘关系来源")]:
                        reference = link(key, label, "snapshot-observed")
                        if reference not in owner["links"]:
                            owner["links"].append(reference)
