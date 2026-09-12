"""Read-only, provenance-preserving catalog of the pinned original JP data.

Structure and field semantics have separate confidence. Unknown bytes and negative
mapping sentinels must survive extraction; these records are not a mod write ABI.
"""
from __future__ import annotations

import struct

from .catalog import sha, text_key
from .weapon_traits import weapon_traits


def checked_slice(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"Out-of-bounds span: {offset:#x} + {size:#x}")
    return data[offset:offset + size]


def check_layout(rom: bytes, layout: dict) -> None:
    if layout.get("schema") != "srw64.original-data-layout.v1":
        raise ValueError("Unsupported original data layout")
    if sha(rom) != layout["rom_sha256"]:
        raise ValueError("Original data requires the pinned JP ROM")
    for table in layout["tables"]:
        raw = checked_slice(rom, table["rom_offset"], table["stride"] * table["count"])
        if sha(raw) != table["sha256"]:
            raise ValueError(f"Table identity changed: {table['id']}")
    for item in layout["evidence"]:
        if sha(checked_slice(rom, item["rom_offset"], item["byte_size"])) != item["sha256"]:
            raise ValueError(f"Parsing evidence changed: {item['id']}")


def raw_record(key: str, label: str, raw: bytes, offset: int,
               confidence: str = "structure-confirmed") -> dict:
    return {"schema": "srw64.original-record.v1", "key": key, "label": label,
            "confidence": confidence, "rom_offset": offset, "byte_size": len(raw),
            "source_sha256": sha(raw), "raw_hex": raw.hex(), "links": [], "fields": []}


def link(key: str, relation: str, confidence: str = "code-confirmed") -> dict:
    return {"key": key, "relation": relation, "confidence": confidence}


def field(name: str, value, offset: int, size: int, confidence: str, basis: str) -> dict:
    return {"name": name, "value": value, "offset": offset, "size": size,
            "confidence": confidence, "basis": basis}


def numeric_field(raw: bytes, spec: dict) -> dict:
    encoded = int.from_bytes(checked_slice(raw, spec["offset"], spec["size"]),
                             "big", signed=spec.get("signed", False))
    value = encoded * spec.get("scale", 1)
    result = field(spec["name"], value, spec["offset"], spec["size"],
                   spec["confidence"], spec["basis"])
    result.update({"id": spec["id"], "encoded_value": encoded,
                   "encoding": "s" if spec.get("signed", False) else "u",
                   "scale": spec.get("scale", 1),
                   "label_text_key": text_key(0, spec["label_text_id"]),
                   "evidence": spec["evidence"]})
    if "growth_per_level" in spec:
        result["growth_per_level"] = spec["growth_per_level"]
    if str(encoded) in spec.get("sentinels", {}):
        result["sentinel_meaning"] = spec["sentinels"][str(encoded)]
    return result


def pilot_skill_fields(raw: bytes, config: dict) -> list[dict]:
    offset = config["flags_offset"]
    flags = checked_slice(raw, offset, 1)[0]
    active = [d for d in config["definitions"] if flags & d["mask"]]
    shared = next((d for mask in config["shared_display_priority"] for d in active
                   if d["mask"] == mask), None)
    known_mask = 0
    for definition in config["definitions"]:
        known_mask |= definition["mask"]
    return [field("技能标志", {"raw": flags, "enabled": active,
                  "shared_group_name": shared["name"] if shared else None,
                  "unknown_bits": flags & ~known_mask}, offset, 1, "code-confirmed",
                  "第一组名称按 4→8→16→32→64 选择；技能等级为 0 时不显示名称。未解释位保留。")]


def skill_threshold_fields(raw: bytes, config: dict) -> list[dict]:
    result = []
    stride, used = config["threshold_group_stride"], config["thresholds_used_per_group"]
    for index, group in enumerate(config["threshold_groups"]):
        offset = index * stride
        values = list(checked_slice(raw, offset, used))
        result.append(field(group["name"] + " · 习得等级", values, offset, used,
                            "code-confirmed", f"启用掩码 {group['enabled_mask']:#x}；计数 0 < 阈值 <= 当前等级的项，最多 {used} 级。"))
        result.append(field(group["name"] + " · 未参与计数的字节",
                            list(checked_slice(raw, offset + used, stride - used)),
                            offset + used, stride - used, "unknown",
                            "800A80F0 的技能等级循环不读取此字节；保留原值，不解释为第 10 级。"))
    return result


def weapon_list(rom: bytes, config: dict, index: int, weapon_count: int) -> dict:
    if not 0 <= index < config["pointer_count"]:
        raise ValueError("Unit weapon list index out of range")
    base = config["rom_offset"]
    pointer_offset = base + index * 4
    relative = int.from_bytes(checked_slice(rom, pointer_offset, 4), "big")
    start = base + relative
    if start < base + config["first_payload_relative"] or start >= config["region_end"]:
        raise ValueError("Weapon list pointer outside payload")
    offset, rows = start, []
    while offset + 12 <= config["region_end"]:
        raw = checked_slice(rom, offset, 12)
        values = struct.unpack(">6H", raw)
        if values[0] == 0xFFFF:
            return {"pointer_rom_offset": pointer_offset, "relative_offset": relative,
                    "rom_offset": start, "rows": rows, "terminator_hex": raw.hex(),
                    "raw_hex": rom[start:offset + 12].hex(),
                    "source_sha256": sha(rom[start:offset + 12])}
        if values[0] >= weapon_count:
            raise ValueError(f"Dangling weapon reference: {values[0]}")
        eligible = [v for v in values[1:] if v != 0xFFFF]
        if any(v >= config["pointer_count"] for v in eligible):
            raise ValueError("Dangling eligible unit reference")
        rows.append({"rom_offset": offset, "weapon_id": values[0],
                     "remaining_u16": list(values[1:]), "raw_hex": raw.hex(),
                     "eligible_unit_ids": eligible, "eligibility_confidence": "code-confirmed"})
        offset += 12
    raise ValueError("Unterminated weapon list")


def extract_gameplay(rom: bytes, layout: dict, sources: dict) -> dict[str, list[dict]]:
    check_layout(rom, layout)
    specs = {t["id"]: t for t in layout["tables"]}
    output: dict[str, list[dict]] = {k: [] for k in specs}
    names = layout["names"]

    def name(index: int) -> str:
        return sources[text_key(0, index)].replace("<END>", "").replace("<BR>", " ")

    for category, spec in specs.items():
        for index in range(spec["count"]):
            offset = spec["rom_offset"] + spec["stride"] * index
            raw = checked_slice(rom, offset, spec["stride"])
            output[category].append(raw_record(f"base:{category}:{index:04d}",
                                               f"{category} {index}", raw, offset))
            output[category][-1]["extent_basis"] = spec["extent_basis"]
    for index, row in enumerate(output["units"]):
        name_id = names["unit_base"] + index
        row["label"] = name(name_id)
        row["label_confidence"] = "code-confirmed"
        row["links"].append(link(text_key(0, name_id), "机体名称"))
        row["weapon_list"] = weapon_list(rom, layout["unit_weapon_lists"], index, specs["weapons"]["count"])
        row["links"] += [link(f"base:weapons:{r['weapon_id']:04d}", "可加载武器（含共用形态）")
                          for r in row["weapon_list"]["rows"]]
        row["evidence"] = ["unit_loader", "unit_weapons", "unit_names", "weapon_eligible_units", "unit_repair_cost"]
    for index, row in enumerate(output["weapons"]):
        name_id, menu_id = names["weapon_base"] + index, names["weapon_menu_base"] + index
        row["label"] = name(name_id)
        row["label_confidence"] = "code-confirmed"
        row["menu_label"] = name(menu_id)
        row["weapon_traits"] = weapon_traits(row["label"], row["menu_label"])
        row["links"] += [link(text_key(0, name_id), "武器名称"),
                         link(text_key(0, menu_id), "武器菜单名称（含类型标记）")]
        row["evidence"] = ["pilot_loader", "unit_weapons", "weapon_names", "weapon_menu_names"]
    for category, definitions in layout["numeric_fields"].items():
        for row in output[category]:
            raw = bytes.fromhex(row["raw_hex"])
            row["fields"] = [numeric_field(raw, spec) for spec in definitions]
            row.setdefault("evidence", [])
            for f in row["fields"]:
                row["evidence"].extend(f["evidence"])
                reference = link(f["label_text_key"], "数值界面标签")
                if reference not in row["links"]:
                    row["links"].append(reference)
            if category == "pilot_stats":
                row["fields"] += pilot_skill_fields(raw, layout["pilot_skills"])
                row["evidence"] += layout["pilot_skills"]["evidence"] + ["pilot_loader"]
                for skill in row["fields"][-1]["value"]["enabled"]:
                    row["links"].append(link(text_key(0, skill["rank_text_base"] + 1), skill["name"] + " L1 原文"))
            row["evidence"] = list(dict.fromkeys(row["evidence"]))
    for row in output["pilot_thresholds"]:
        row["fields"] = skill_threshold_fields(bytes.fromhex(row["raw_hex"]), layout["pilot_skills"])
        row["evidence"] = layout["pilot_skills"]["evidence"].copy()
    for row in output["spirits"]:
        raw = bytes.fromhex(row["raw_hex"])
        commands = []
        for i in range(0, 12, 2):
            command_id = raw[i+1]
            if command_id >= names["spirit_command_count"]:
                raise ValueError(f"Unknown spirit command ID: {command_id}")
            name_id = names["spirit_base"] + command_id
            commands.append({"level": raw[i], "command_id": command_id,
                             "command_name": name(name_id), "text_key": text_key(0, name_id)})
            row["links"].append(link(text_key(0, name_id), f"精神 {command_id} · {name(name_id)}"))
        row["fields"] = [field("精神习得表", commands, 0, 12, "code-confirmed",
                               "800A8098 起比较当前等级；load_0008F4B0:801C9DA8..801C9DB4 以 command_id + 969 显示名称")]
        row["evidence"] = ["pilot_loader", "pilot_consumer", "spirit_names"]

    for index, row in enumerate(output["stage_maps"]):
        raw = bytes.fromhex(row["raw_hex"])
        map_id = raw[0]
        if map_id >= len(output["map_assets"]):
            raise ValueError(f"Dangling stage map: {map_id}")
        row["label"] = f"场景索引 {index} → 地图 {map_id}"
        row["fields"] = [field("初始地图编号", map_id, 0, 1, "code-confirmed",
                                "80209D6C：按场景索引读取，写入 8010F5EE。场景可在后续事件中切换地图。"),
                         field("第二字节", raw[1], 1, 1, "unknown",
                               "80209D90 返回此值；具体用途待追踪。")]
        row["links"].append(link(f"base:map_assets:{map_id:04d}", "初始地图资源"))
        row["evidence"] = ["stage_map_loader"]
        if index == specs["stage_maps"]["count"] - 1:
            row["fields"].append({"name": "物理边界", "value": "末尾全零二字节可能为对齐填充；保留，不据此增加可玩关卡数。", "confidence": "unknown"})
    for index, row in enumerate(output["map_assets"]):
        raw = bytes.fromhex(row["raw_hex"])
        values = struct.unpack(">5H2B", raw)
        row["label"] = f"地图 {index} · 主资源 {values[0]}"
        for slot, resource_id in enumerate(values[:5]):
            role = ["地图布局", "地图图集", "地图调色板", "辅助资源 1", "辅助资源 2"][slot]
            row["fields"].append(field(role, resource_id, slot * 2, 2, "code-confirmed",
                                       "前三槽由 800945D4 按布局、图集、调色板绘制战场底图；辅助槽为 0 时跳过，其内部用途待解析。"))
            if slot < 3 or resource_id:
                row["links"].append(link(f"base:resources:{resource_id:04d}", role))
        row["fields"] += [field("模式字节", values[5], 10, 1, "unknown",
                                "801C6FFC 比较是否为 1；完整含义待确认。"),
                          field("调用参数 +11", values[6], 11, 1, "unknown",
                                "801E02D4..801E02E0 传给 8007E810；暂不解释曲名。")]
        row["evidence"] = ["map_asset_loader", "map_resource_arguments", "map_resource_wrapper",
                           "map_resource_consumer", "map_aux_resources"]

    output["actors"] = []
    for index in range(names["actor_name_count"]):
        row = {"schema": "srw64.original-record.v1", "key": f"base:actors:{index:04d}",
               "label": name(names["actor_short_base"] + index), "confidence": "code-confirmed",
               "links": [link(text_key(0, names[k] + index), label) for k, label in
                         [("actor_short_base", "简称"), ("actor_full_base", "全名")]],
               "fields": [], "evidence": ["actor_names", "pilot_loader"]}
        for mapping, target in [("actor_stats_map", "pilot_stats"), ("actor_spirits_map", "spirits")]:
            if index >= specs[mapping]["count"]:
                row["fields"].append({"name": mapping, "value": None, "confidence": "unknown",
                                       "basis": "此文本身份在已界定的映射表之外，不越界读取相邻表"})
                continue
            source = output[mapping][index]
            mapped = int.from_bytes(bytes.fromhex(source["raw_hex"]), "big", signed=True)
            row["links"].append(link(source["key"], mapping))
            source["fields"] = [field("映射值（负数保留原值）", mapped, 0, 2,
                                        "code-confirmed", specs[mapping]["loader_vram"])]
            if mapped >= 0:
                if mapped >= specs[target]["count"]:
                    raise ValueError(f"Dangling {mapping} reference: {mapped}")
                row["links"].append(link(output[target][mapped]["key"], target))
                output[target][mapped]["links"].append(link(row["key"], "使用此记录的人物"))
                if target == "pilot_stats":
                    if mapped < specs["pilot_thresholds"]["count"]:
                        row["links"].append(link(output["pilot_thresholds"][mapped]["key"], "技能阈值"))
                    else:
                        row["fields"].append({"name": "阈值索引异常", "value": mapped,
                            "confidence": "unknown", "basis": "索引 256 的 30 字节读取落入精神表；不虚构第 257 条技能记录"})
        output["actors"].append(row)
    output["stages"] = [{"schema": "srw64.original-record.v1",
        "key": f"candidate:stage-title:{i:05d}", "label": name(i), "confidence": "candidate",
        "fields": [{"name": "范围", "value": "章节标题文本候选；尚未映射关卡 ID、路线和脚本", "confidence": "candidate"}],
        "links": [link(text_key(0, i), "原始标题文本", "structure-confirmed")]}
        for i in range(names["stage_title_first"], names["stage_title_last"] + 1)]
    return output


def snapshot_observation(ram: bytes, gameplay: dict, metadata: dict) -> dict:
    """Inspect active allocations, including dormant transformations, not map spawns."""
    if len(ram) != 8 * 1024 * 1024:
        raise ValueError("Snapshot must be canonical big-endian 8 MiB RDRAM")
    stage_id, map_id = ram[0x10F5F0], ram[0x10F5EE]
    if stage_id >= len(gameplay["stage_maps"]) or map_id >= len(gameplay["map_assets"]):
        raise ValueError("Snapshot stage/map outside extracted tables")
    units = []
    for bank in range(3):
        for slot in range(140):
            offset = 0x16A210 + bank * 11760 + slot * 84
            raw = checked_slice(ram, offset, 84)
            if raw[0] != 1:
                continue
            unit_id = int.from_bytes(raw[2:4], "big")
            if unit_id >= len(gameplay["units"]):
                raise ValueError("Snapshot unit ID outside extracted table")
            pilot_count = raw[0x34]
            if pilot_count > 5:
                raise ValueError("Invalid snapshot pilot count")
            pilots = []
            for i in range(pilot_count):
                pointer = int.from_bytes(raw[0x38+i*4:0x3C+i*4], "big")
                if not 0x80000000 <= pointer <= 0x807FFFFF:
                    raise ValueError("Snapshot pilot pointer outside RDRAM")
                pilot = checked_slice(ram, pointer - 0x80000000, 76)
                actor_id = int.from_bytes(pilot[2:4], "big")
                if actor_id >= len(gameplay["actors"]):
                    raise ValueError("Snapshot actor ID outside catalog")
                pilots.append({"actor_key": gameplay["actors"][actor_id]["key"],
                               "label": gameplay["actors"][actor_id]["label"],
                               "pointer": hex(pointer), "raw_hex": pilot.hex()})
            units.append({"bank": bank, "slot": slot, "vram": hex(0x80000000 + offset),
                          "unit_key": gameplay["units"][unit_id]["key"],
                          "label": gameplay["units"][unit_id]["label"],
                          "raw_hex": raw.hex(), "pilots": pilots})
    return {"schema": "srw64.original-record.v1", "key": "observation:female-stage1",
            "label": "女性超级系第一话 · 历史内存观察", "confidence": "snapshot-observed",
            "source": metadata, "snapshot_sha256": sha(ram), "instances": units,
            "scene_indices": {"stage_index": stage_id, "map_index": map_id},
            "fields": [{"name": "证据范围", "confidence": "snapshot-observed",
                        "value": "已分配的机体／驾驶员关系，包含备用变形；不是出击数量，也不是关卡脚本。历史运行退出失败，不能作为当前运行验收。"}],
            "links": [link(u["unit_key"], f"bank {u['bank']} / slot {u['slot']}", "snapshot-observed") for u in units]
                + [link(p["actor_key"], "关联驾驶员", "snapshot-observed") for u in units for p in u["pilots"]]
                + [link(f"base:stage_maps:{stage_id:04d}", "快照场景索引", "snapshot-observed"),
                   link(f"base:map_assets:{map_id:04d}", "快照当前地图", "snapshot-observed")],
            "evidence": ["unit_loader", "unit_pilots", "pilot_consumer"]}
