"""Read-only stage script structure and the full instruction IR of the original VM.

Every event is decoded from its entry to the FFFF terminator with the operand
lengths, condition blocks and context markers recovered from the resident VM
(8009EE98..800A25B0). Pointers establish physical bounds, not reachability; the
decoder never resynchronises after an unknown word. Unknown semantics keep their
technical name. No writer, VM simulation or title-to-stage inference lives here.
"""
from __future__ import annotations

from bisect import bisect_right
from collections import Counter, defaultdict
import struct

from .catalog import sha, text_key
from .original_data import checked_slice, field, link, raw_record

MARKER_FIRST, MARKER_LAST = 0x3DD0, 0x3DDB
CONDITION_FIRST, CONDITION_LAST = 0x3E00, 0x3E1D
END_WORD = 0xFFFF
PROTAGONIST_BASE, RIVAL_BASE, CONTEXT_BASE = 25, 29, 0x3DD1
SIDE_NAMES = {0: "我方", 1: "敌方", 2: "第三方", 4: "任意阶段"}
DEPLOY_SIDE_NAMES = {0: "我方", 1: "敌方", 2: "第三方", 3: "我方（记录值 3）", 4: "第三方（记录值 4）"}


def word(data: bytes, offset: int) -> int:
    return int.from_bytes(checked_slice(data, offset, 4), "big")


def half(data: bytes, offset: int, signed: bool = False) -> int:
    return int.from_bytes(checked_slice(data, offset, 2), "big", signed=signed)


def bank_offset(bank: dict, pointer: int) -> int:
    relative = pointer - bank["vram"]
    if relative < 0 or relative >= bank["byte_size"] or pointer % 2:
        raise ValueError(f"Script pointer outside bank: {pointer:#x}")
    return bank["rom_offset"] + relative


def pointer_table(rom: bytes, bank: dict) -> list[int]:
    start, count = bank["index_rom_offset"], bank["index_count"]
    result = [word(rom, start + i * 4) for i in range(count)]
    if word(rom, start + count * 4) != 0:
        raise ValueError("Stage pointer table has no trailing zero")
    for pointer in result:
        bank_offset(bank, pointer)
    return result


def event_lists(rom: bytes, bank: dict, pointers: list[int]) -> dict[int, list[int]]:
    result = {}
    for pointer in sorted(set(pointers)):
        start = bank_offset(bank, pointer)
        if pointer % 4 or start >= bank["index_rom_offset"]:
            raise ValueError("Invalid event list address")
        entries = []
        for i in range(64):  # caller's 256-byte pointer buffer, including -1
            offset = start + i * 4
            if offset + 4 > bank["index_rom_offset"]:
                raise ValueError("Event list crossed the top-level index")
            event = word(rom, offset)
            if not event:
                break
            if not bank["rom_offset"] <= bank_offset(bank, event) < start:
                raise ValueError("Event entry is outside its payload")
            entries.append(event)
        else:
            raise ValueError("Unterminated event pointer list")
        if not entries or entries != sorted(set(entries)):
            raise ValueError("Empty, duplicate or unordered event pointer list")
        result[pointer] = entries
    return result


# --- machine-code cross checks -------------------------------------------------

def _sext16(value: int) -> int:
    return struct.unpack(">h", (value & 0xFFFF).to_bytes(2, "big"))[0]


def verify_vm_tables(rom: bytes, spec: dict) -> None:
    """Every operand table entry must agree with the ROM jump tables it claims."""
    delta = spec["resident_vram_rom_delta"]
    dispatch = spec["dispatch"]
    for i in range(dispatch["count"]):
        opcode = dispatch["first_opcode"] + i
        known = spec["commands"][f"{opcode:04x}"]
        case = word(rom, dispatch["rom_offset"] + 4 * i)
        if case == dispatch["default_vram"]:
            if known["handler_vram"] is not None:
                raise ValueError(f"Command {opcode:04X} claims a handler but dispatches to the default")
            continue
        hi, jump, lo = struct.unpack(">3I", checked_slice(rom, case - delta, 12))
        if hi >> 16 != 0x3C02 or lo >> 16 != 0x2442 or jump != 0x08028756:
            raise ValueError("Unknown script dispatch stub")
        handler = ((hi & 0xFFFF) << 16) + _sext16(lo)
        if handler != int(known["handler_vram"], 16):
            raise ValueError(f"Command handler identity mismatch: {opcode:04X}")
    cond = spec["condition_dispatch"]
    for i in range(cond["count"]):
        opcode = cond["first_opcode"] + i
        known = spec["conditions"][f"{opcode:04x}"]
        case = word(rom, cond["jump_table_rom_offset"] + 4 * i)
        if known["handler_vram"] is None:
            if case != int(cond["inline_true_vram"], 16):
                raise ValueError(f"Condition {opcode:04X} is not the inline true branch")
            continue
        instruction = word(rom, case - delta)
        if instruction >> 26 != 3 or 0x80000000 | ((instruction & 0x3FFFFFF) << 2) != int(known["handler_vram"], 16):
            raise ValueError(f"Condition handler identity mismatch: {opcode:04X}")
    skip = spec["skip_scan"]
    for i in range(skip["count"]):
        known = spec["conditions"][f"{CONDITION_FIRST + i:04x}"]
        expected = skip["depth_increment_vram"] if known["kind"] == "opener" else skip["advance_vram"]
        if word(rom, skip["jump_table_rom_offset"] + 4 * i) != int(expected, 16):
            raise ValueError(f"Skip-scan nesting rule mismatch: {CONDITION_FIRST + i:04X}")
    trigger = spec["trigger_dispatch"]
    for group in range(trigger["count"]):
        case = word(rom, trigger["jump_table_rom_offset"] + 4 * group)
        hi, second, third = struct.unpack(">3I", checked_slice(rom, case - delta, 12))
        lo = third if second >> 26 == 2 else second
        if hi >> 16 != 0x3C02 or lo >> 16 != 0x2442:
            raise ValueError("Unknown trigger dispatch stub")
        handler = ((hi & 0xFFFF) << 16) + _sext16(lo)
        if handler != int(spec["event_types"][str(group)]["handler_vram"], 16):
            raise ValueError(f"Trigger handler identity mismatch: type {group}")


# --- operand resolution --------------------------------------------------------

class Resolver:
    """Names and links for operands; never invents an identity outside the tables."""

    def __init__(self, categories: dict, sources: dict, headers: dict):
        self.sources, self.headers = sources, headers
        self.actors = categories.get("actors", [])
        self.units = categories.get("units", [])
        self.scene_count = len(categories.get("stage_maps", []))

    def actor(self, value: int) -> dict:
        if 0 <= value < len(self.actors):
            row = self.actors[value]
            return {"key": row["key"], "label": row["label"]}
        if value == 0x34E:
            return {"label": "主角（按当前路线）"}
        return {}

    def unit(self, value: int) -> dict:
        if 0 <= value < len(self.units):
            row = self.units[value]
            return {"key": row["key"], "label": row["label"]}
        return {}

    def speaker(self, key: str) -> dict:
        header = self.headers.get(key)
        if header is None or len(header) < 3 or not header[:3].isdigit():
            return {"speaker_status": "unparsed"}
        speaker = int(header[:3].decode())
        result = {"speaker_id": speaker, "header_digits": header.decode("ascii", "replace")}
        if speaker in (PROTAGONIST_BASE, RIVAL_BASE):
            candidates = [self.actor(speaker + k) for k in range(4)]
            result.update(speaker_status="route-relative",
                          speaker_label="主角" if speaker == PROTAGONIST_BASE else "对手",
                          speaker_candidates=[c for c in candidates if "key" in c])
        else:
            actor = self.actor(speaker)
            result.update(speaker_status="resolved" if "key" in actor else "outside-name-table",
                          speaker_label=actor.get("label", f"角色 {speaker}"))
            if "key" in actor:
                result["speaker_key"] = actor["key"]
        return result

    def text(self, value: int) -> dict:
        key = text_key(0, value)
        if key not in self.sources:
            return {"unresolved_text_id": value}
        return {"text_key": key, "text": self.sources[key], **self.speaker(key)}


def decode_position(value: int) -> dict:
    x, y = value >> 8, value & 0xFF
    relative = {0x40: "同位", 0x41: "上", 0x42: "右", 0x43: "下", 0x44: "左"}
    if x >= 0x40:
        return {"relative_to": "3D54 记住的位置", "direction": relative.get(x, f"未知码 {x:#x}"), "distance": y}
    return {"x": x, "y": y}


def decode_operand(role: str, value: int, resolver: Resolver) -> dict:
    """Return display metadata for one operand; unknown roles stay raw."""
    out: dict = {"role": role, "value": value}
    if role == "actor":
        out.update(resolver.actor(value))
        if value == 999:
            out["meaning"] = "无（999）"
    elif role == "text":
        out.update(resolver.text(value))
    elif role == "unit":
        out.update(resolver.unit(value))
        if value == 999:
            out["meaning"] = "无（999）"
    elif role == "flag":
        out["meaning"] = f"变量 {value}" if value < 200 else "超出 200 个变量范围"
    elif role == "scene":
        if value == 500:
            out["meaning"] = "恢复 8010F5F2 记录的场景"
        elif value < resolver.scene_count:
            out.update(key=f"base:scenarios:{value:04d}", label=f"场景索引 {value}")
    elif role == "position":
        out.update(decode_position(value))
    elif role == "side":
        out["meaning"] = SIDE_NAMES.get(value, f"未知阵营 {value}")
    elif role == "deploy_side":
        out["meaning"] = DEPLOY_SIDE_NAMES.get(value, f"未知阵营 {value}")
    elif role == "actor_or_group":
        if value >= 500:
            out.update(group=value - 500, meaning=f"配套组 {value - 500} 的全部单位")
        else:
            out.update(resolver.actor(value))
    elif role == "se":
        if value == END_WORD:
            out["meaning"] = "停止音效（-1）"
    elif role == "bgm":
        if value == 0:
            out["meaning"] = "停止 BGM"
    elif role == "bool":
        out["meaning"] = "开" if value else "关"
    elif role in ("actor_or_any", "gate_flag"):
        if value == 0:
            out["meaning"] = "任意" if role == "actor_or_any" else "无门槛"
        elif role == "actor_or_any":
            out.update(resolver.actor(value))
        else:
            out["meaning"] = f"变量 {value} 须为 3" if 100 <= value <= 115 else f"变量 {value}（触发器不检查此范围）"
    elif role == "side_or_any":
        out["meaning"] = SIDE_NAMES.get(value, f"未知阵营 {value}")
    elif role == "turn":
        out["meaning"] = f"回合 {value}"
    elif role == "region_target":
        count, target = divmod(value, 1000)
        out["count"] = count
        if target == 21:
            out["meaning"] = "列表 800C9A08 中首个在场角色"
        elif target >= 500:
            out["meaning"] = f"阵营 {target - 500} 的任意单位"
        else:
            out.update(resolver.actor(target))
            out["target"] = target
    elif role in ("region_x", "region_y"):
        out.update(start=value // 10, span=value % 10)
    return out


# --- event decoding ------------------------------------------------------------

def classify(opcode: int, spec: dict) -> tuple[str, dict | None]:
    if opcode == END_WORD:
        return "end", None
    if MARKER_FIRST <= opcode <= MARKER_LAST:
        return "marker", spec["context_markers"][f"{opcode:04x}"]
    if CONDITION_FIRST <= opcode <= CONDITION_LAST:
        return "condition", spec["conditions"][f"{opcode:04x}"]
    known = spec["commands"].get(f"{opcode:04x}")
    if known and known["operand_words"] is not None:
        return "command", known
    return "unknown", None


def emulate_skip_scan(raw: bytes, start: int, spec: dict) -> dict:
    """Word-by-word scan the VM performs after a false condition (8009F288)."""
    depth, position = 1, start
    while position + 2 <= len(raw):
        value = half(raw, position)
        if value == END_WORD:
            return {"landing": position, "reason": "end"}
        if value == CONDITION_LAST:
            depth -= 1
            if depth == 0:
                return {"landing": position + 2, "reason": "block-end"}
        elif CONDITION_FIRST <= value < CONDITION_LAST and (
                value == 0x3E1C or spec["conditions"][f"{value:04x}"]["kind"] == "opener"):
            depth += 1
        position += 2
    return {"landing": position, "reason": "physical-boundary"}


def decode_event(raw: bytes, offset: int, spec: dict, resolver: Resolver) -> dict:
    checked_slice(raw, 0, 10)
    position, instructions = 10, []
    status, depth, max_depth = "physical-boundary", 0, 0
    stack: list[int] = []
    blocks, unbalanced_ends, section = [], 0, None
    while position + 2 <= len(raw):
        opcode = half(raw, position)
        kind, known = classify(opcode, spec)
        if kind == "unknown":
            status = "unknown-opcode"
            break
        count = (known or {}).get("operand_words") or 0
        size = 2 + count * 2
        if position + size > len(raw):
            status = "truncated-operands"
            break
        encoded = raw[position:position + size]
        args = list(struct.unpack(f">{count}H", encoded[2:])) if count else []
        item = {"rom_offset": offset + position, "offset": position, "opcode": opcode,
                "kind": kind, "name": known["name"] if known else "事件结束",
                "operands": args, "raw_hex": encoded.hex(), "depth": depth, "section": section}
        if kind == "command":
            item["opcode_key"] = f"base:script_opcodes:{opcode:04x}"
            if "dialogue_mode" in known:
                item["kind"] = "dialogue"
                item["dialogue_mode"] = known["dialogue_mode"]
                item.update(resolver.text(args[0]))
                narrow_speaker(item, section, resolver)
            if known.get("no_advance"):
                item["no_advance"] = True
        elif kind == "condition":
            item["opcode_key"] = f"base:script_conditions:{opcode:04x}"
            item["block"] = known["kind"]
            if known["kind"] == "opener":
                stack.append(len(instructions))
                depth += 1
                max_depth = max(max_depth, depth)
            elif known["kind"] == "block-end":
                if stack:
                    opener = stack.pop()
                    depth -= 1
                    blocks.append({"opener_index": opener, "end_index": len(instructions),
                                   "opener_offset": instructions[opener]["offset"], "end_offset": position + 2})
                else:
                    unbalanced_ends += 1
                    item["unbalanced"] = True
        elif kind == "marker":
            item["opcode_key"] = f"base:script_markers:{opcode:04x}"
            section = f"{opcode:04X}"
            item["section"] = section
        if known and known.get("operands"):
            item["fields"] = [decode_operand(spec_row["role"], value, resolver) | {"name": spec_row["name"]}
                              for spec_row, value in zip(known["operands"], args)]
        instructions.append(item)
        position += size
        if kind == "end":
            status = "terminator-reached"
            break
    end_offset = position - 2 if status == "terminator-reached" else position
    for opener in stack:  # openers closed by FFFF or the physical boundary
        blocks.append({"opener_index": opener, "end_index": None, "closed_by": "end",
                       "opener_offset": instructions[opener]["offset"], "end_offset": None})
    hazards = []
    for block in sorted(blocks, key=lambda b: b["opener_index"]):
        opener = instructions[block["opener_index"]]
        scan = emulate_skip_scan(raw, opener["offset"] + len(bytes.fromhex(opener["raw_hex"])), spec)
        expected = block["end_offset"] if block["end_offset"] is not None else end_offset
        if scan["landing"] != expected:
            hazards.append({"kind": "skip-scan", "opener_offset": opener["offset"], "structural_end": expected,
                            "engine_landing": scan["landing"], "reason": scan["reason"]})
    for item in instructions:
        for value in item["operands"]:
            looks_like = (MARKER_FIRST <= value <= MARKER_LAST or value == END_WORD
                          or CONDITION_FIRST <= value <= CONDITION_LAST)
            # The marker scan only walks sections a context does not match; 3DD0 is matched by every context.
            if looks_like and item["section"] not in (None, f"{MARKER_FIRST:04X}"):
                hazards.append({"kind": "marker-scan-control-word", "offset": item["offset"],
                                "opcode": item["opcode"], "value": value, "section": item["section"]})
    return {"status": status, "instructions": instructions, "decoded_bytes": position - 10,
            "remainder_offset": position, "remainder_hex": raw[position:].hex(),
            "stop_word": half(raw, position) if status != "terminator-reached" and position + 2 <= len(raw) else None,
            "blocks": blocks, "max_depth": max_depth, "unbalanced_block_ends": unbalanced_ends,
            "hazards": hazards,
            "scope": "按已确认的参数长度顺序读取至结束符；条件块与上下文段为静态结构，未求值实际运行路径。"}


def narrow_speaker(item: dict, section: str | None, resolver: Resolver) -> None:
    """A route-relative speaker inside a protagonist section names one actor; group sections keep two."""
    if item.get("speaker_status") != "route-relative" or section is None:
        return
    marker = int(section, 16)
    base = item["speaker_id"]
    groups = {0x3DD5: (0, 1), 0x3DD6: (2, 3), 0x3DD7: (0, 2), 0x3DD8: (1, 3)}
    if CONTEXT_BASE <= marker <= CONTEXT_BASE + 3:
        actor = resolver.actor(base + marker - CONTEXT_BASE)
        item.update(speaker_key=actor["key"], speaker_label=f"{actor['label']}（按段落 {section}）",
                    speaker_status="route-resolved-by-section")
    elif marker in groups:
        item["speaker_candidates"] = [resolver.actor(base + k) for k in groups[marker]]


def decode_trigger(header: tuple, spec: dict, resolver: Resolver) -> dict:
    kind = spec["event_types"][str(header[0])]
    return {"type": header[0], "name": kind["name"], "polled": kind["polled"],
            "status_code": kind["status_code"], "confidence": kind["confidence"],
            "fields": [decode_operand(f["role"], value, resolver) | {"name": f["name"], "note": f["note"]}
                       for f, value in zip(kind["header"], header[1:])]}


# --- opcode catalogs -----------------------------------------------------------

def opcode_catalogs(rom: bytes, spec: dict) -> dict[str, list[dict]]:
    verify_vm_tables(rom, spec)
    dispatch = spec["dispatch"]
    commands = []
    for i in range(dispatch["count"]):
        opcode = dispatch["first_opcode"] + i
        offset = dispatch["rom_offset"] + 4 * i
        known = spec["commands"][f"{opcode:04x}"]
        row = raw_record(f"base:script_opcodes:{opcode:04x}", f"{opcode:04X} · {known['name']}",
                         checked_slice(rom, offset, 4), offset, "code-confirmed")
        row["opcode"] = {"value": opcode, "case_vram": f"0x{word(rom, offset):08X}", "family": "command",
                         **{k: known.get(k) for k in ("handler_vram", "operand_words", "name", "semantic_confidence",
                                                      "basis", "operands", "dialogue_mode", "no_advance")}}
        row["evidence"] = ["script_dispatch", "script_dispatch_table", "script_command_handlers", "script_command_handlers_tail"]
        row["evidence"].extend(known.get("evidence", []))
        row["summary"] = f"{known['operand_words'] if known['operand_words'] is not None else '—'} 个参数 · {known['semantic_confidence']}"
        commands.append(row)
    conditions = []
    cond = spec["condition_dispatch"]
    for i in range(cond["count"]):
        opcode = cond["first_opcode"] + i
        offset = cond["jump_table_rom_offset"] + 4 * i
        known = spec["conditions"][f"{opcode:04x}"]
        row = raw_record(f"base:script_conditions:{opcode:04x}", f"{opcode:04X} · {known['name']}",
                         checked_slice(rom, offset, 4), offset, "code-confirmed")
        row["opcode"] = {"value": opcode, "case_vram": f"0x{word(rom, offset):08X}", "family": "condition",
                         "block": known["kind"], **{k: known.get(k) for k in
                         ("handler_vram", "operand_words", "name", "semantic_confidence", "basis", "operands")}}
        row["evidence"] = ["script_vm", "script_dispatch", "script_condition_handlers",
                           "script_condition_jump_table", "script_skip_jump_table"]
        row["summary"] = f"{known['operand_words']} 个参数 · {known['kind']}"
        conditions.append(row)
    markers = []
    for opcode in range(MARKER_FIRST, MARKER_LAST + 1):
        known = spec["context_markers"][f"{opcode:04x}"]
        row = {"schema": "srw64.original-record.v1", "key": f"base:script_markers:{opcode:04x}",
               "label": f"{opcode:04X} · {known['name']}", "confidence": "code-confirmed", "links": [],
               "fields": [field("匹配规则", known["matches"], 0, 0, "code-confirmed", known["basis"])],
               "opcode": {"value": opcode, "family": "marker", "name": known["name"], "matches": known["matches"],
                          "basis": known["basis"], "operand_words": 0, "semantic_confidence": "code-confirmed"},
               "evidence": ["script_vm", "script_protagonist_marker"], "summary": known["matches"]}
        if "actor_id" in known:
            row["links"].append(link(f"base:actors:{known['actor_id']:04d}", "对应主角"))
        markers.append(row)
    types = []
    for index in range(15):
        known = spec["event_types"][str(index)]
        row = {"schema": "srw64.original-record.v1", "key": f"base:script_event_types:{index:02d}",
               "label": f"类型 {index} · {known['name']}", "confidence": known["confidence"], "links": [],
               "fields": [field(f["name"], f["note"] or f["role"], 2 + 2 * i, 2, known["confidence"], "事件头第 %d 字" % (i + 1))
                          for i, f in enumerate(known["header"])],
               "event_type": {k: known.get(k) for k in ("name", "group", "slot", "polled", "status_code", "handler_vram", "confidence")},
               "evidence": ["stage_script_load_register", "script_event_triggers", "script_event_type_table"],
               "summary": f"{known['polled']} · {known['status_code'] or '—'}"}
        types.append(row)
    return {"script_opcodes": commands, "script_conditions": conditions,
            "script_markers": markers, "script_event_types": types}


# --- auxiliary (deployment) records --------------------------------------------

def parse_auxiliary_block(raw: bytes, offset: int, spec: dict, resolver: Resolver, block_key: str) -> tuple[list[dict], dict]:
    layout = spec["auxiliary_record"]
    stride = layout["halfwords"] * 2
    records, position, terminated = [], 0, False
    while position + 2 <= len(raw):
        group = half(raw, position, signed=True)
        if group == layout["terminator"]:
            terminated = True
            break
        if position + stride > len(raw):
            break
        record = checked_slice(raw, position, stride)
        row = raw_record(f"base:stage_deployments:{offset + position:08x}", f"配套记录 {offset + position:08X}", record, offset + position)
        fields = []
        for f in layout["fields"]:
            value = int.from_bytes(record[f["offset"]:f["offset"] + f["size"]], "big", signed=f["size"] == 2)
            decoded = decode_operand(f["role"], value, resolver)
            fields.append(field(f["name"], decoded.get("label") or decoded.get("meaning", value), f["offset"], f["size"],
                                f["confidence"], f["basis"]) | {"role": f["role"], "encoded_value": value} |
                          ({"key": decoded["key"]} if "key" in decoded else {}))
        row["fields"] = fields
        row["deployment"] = {"group": group, "x": half(record, 2, True), "y": half(record, 4, True),
                             "actor": half(record, 6, True), "level_offset": record[9], "unit": half(record, 10, True),
                             "upgrade_index": half(record, 12, True), "side": half(record, 20, True),
                             "behaviour": half(record, 22), "extra": half(record, 24), "block_key": block_key,
                             "index_in_block": len(records)}
        for f in fields:
            if "key" in f:
                row["links"].append(link(f["key"], f["name"]))
        row["links"].append(link(block_key, "所属配套数据块"))
        row["evidence"] = ["stage_script_load_register", "stage_script_dma", "script_aux_record_spawn", "script_aux_record_effect"]
        actor, unit = resolver.actor(row["deployment"]["actor"]), resolver.unit(row["deployment"]["unit"])
        row["label"] = f"组 {group} · {actor.get('label', '角色 %d' % row['deployment']['actor'])} · {unit.get('label', '机体 %d' % row['deployment']['unit'])}"
        row["summary"] = f"({row['deployment']['x']}, {row['deployment']['y']}) · {DEPLOY_SIDE_NAMES.get(row['deployment']['side'], row['deployment']['side'])}"
        row["search_terms"] = " ".join(filter(None, [actor.get("label"), unit.get("label"), f"组{group}"]))
        records.append(row)
        position += stride
    return records, {"terminated": terminated, "record_bytes": position,
                     "trailing_bytes": len(raw) - position - (2 if terminated else 0)}


# --- catalog assembly ----------------------------------------------------------

def attach_script_catalog(categories: dict, rom: bytes, layout: dict, sources: dict, headers: dict) -> dict:
    spec = layout["stage_scripts"]
    if spec.get("schema") != "srw64.stage-script-layout.v2":
        raise ValueError("Unsupported stage script layout")
    banks = spec["banks"]
    for bank in banks.values():
        if sha(checked_slice(rom, bank["rom_offset"], bank["byte_size"])) != bank["sha256"]:
            raise ValueError("Stage script bank identity changed")
    resolver = Resolver(categories, sources, headers)
    catalogs = opcode_catalogs(rom, spec)
    event_bank, aux_bank = banks["events"], banks["auxiliary"]
    pointers = pointer_table(rom, event_bank)
    auxiliary = pointer_table(rom, aux_bank)
    lists = event_lists(rom, event_bank, pointers)
    memberships = defaultdict(list)
    for scene, pointer in enumerate(pointers):
        for slot, event in enumerate(lists[pointer]):
            memberships[event].append((scene, slot))
    # Adjacent referenced structures bound a span. Trailing bytes stay opaque.
    boundaries = sorted(set(memberships) | set(pointers) | {
        event_bank["vram"] + event_bank["index_rom_offset"] - event_bank["rom_offset"]})

    # Auxiliary blocks and their deployment records.
    aux_rows, aux_by_pointer, deployments, deployment_index = [], {}, [], {}
    starts = sorted(set(auxiliary))
    for i, pointer in enumerate(starts):
        offset = bank_offset(aux_bank, pointer)
        end = bank_offset(aux_bank, starts[i + 1]) if i + 1 < len(starts) else aux_bank["index_rom_offset"]
        raw = checked_slice(rom, offset, end - offset)
        row = raw_record(f"base:stage_auxiliary:{offset:08x}", f"场景配套数据 {offset:08X}", raw, offset)
        records, audit = parse_auxiliary_block(raw, offset, spec, resolver, row["key"])
        row["auxiliary"] = {**audit, "record_count": len(records), "record_keys": [r["key"] for r in records],
                            "groups": sorted({r["deployment"]["group"] for r in records})}
        row["fields"] = [field("记录数", len(records), 0, audit["record_bytes"], "structure-confirmed",
                               "按 14 个半字步进读取到首字 999；" + ("已读到 999。" if audit["terminated"] else "本块上界前没有对齐的 999，保留原始字节。")),
                         field("尾部字节", audit["trailing_bytes"], audit["record_bytes"], audit["trailing_bytes"], "unknown",
                               "结束符之后或未对齐的字节；不解释为记录。")]
        row["evidence"] = ["stage_script_load_register", "stage_script_dma", "script_aux_record_spawn"]
        row["links"] = [link(f"base:stage_maps:{scene:04d}", f"场景索引 {scene}")
                        for scene, p in enumerate(auxiliary) if p == pointer]
        row["links"] += [link(r["key"], f"组 {r['deployment']['group']} 记录") for r in records]
        row["summary"] = f"{len(records)} 条记录 · {len(row['auxiliary']['groups'])} 组" + ("" if audit["terminated"] else " · 无 999")
        aux_rows.append(row)
        aux_by_pointer[pointer] = row
        deployments += records
        deployment_index[row["key"]] = defaultdict(list)
        for r in records:
            deployment_index[row["key"]][r["deployment"]["group"]].append(r["key"])

    events, event_by_pointer = [], {}
    for pointer in sorted(memberships):
        end = boundaries[bisect_right(boundaries, pointer)]
        offset = bank_offset(event_bank, pointer)
        raw = checked_slice(rom, offset, end - pointer)
        header = struct.unpack(">5H", checked_slice(raw, 0, 10))
        if header[0] > 14:
            raise ValueError("Unexpected event registration type")
        trigger = decode_trigger(header, spec, resolver)
        row = raw_record(f"base:stage_events:{offset:08x}", f"事件 {offset:08X} · {trigger['name']}", raw, offset)
        script = decode_event(raw, offset, spec, resolver)
        row["script"] = {"header_words": list(header), "entry_vram": f"0x{pointer:08X}",
                         "span_basis": "至下一已引用结构；包含可能的填充，不等于执行长度。",
                         "scenes": [s for s, _ in memberships[pointer]], "trigger": trigger, **script}
        row["evidence"] = ["stage_script_load_register", "script_vm", "script_dispatch", "script_command_handlers",
                           "script_command_handlers_tail", "script_condition_handlers", "script_event_triggers",
                           "script_dialogue_source", "script_speaker_header"]
        row["links"] = [link(f"base:scenarios:{scene:04d}", f"场景索引 {scene} · 入口槽 {slot}")
                        for scene, slot in memberships[pointer]]
        row["links"] += [link(f["key"], f"触发条件 · {f['name']}") for f in trigger["fields"] if "key" in f]
        seen = set()
        for item in script["instructions"]:
            for key_name in ("text_key", "opcode_key", "speaker_key"):
                key = item.get(key_name)
                if key and key not in seen:
                    seen.add(key)
                    row["links"].append(link(key, {"text_key": "对白原文", "opcode_key": "指令", "speaker_key": "说话人"}[key_name]))
            for f in item.get("fields", []):
                if "key" in f and f["key"] not in seen:
                    seen.add(f["key"])
                    row["links"].append(link(f["key"], f"参数 · {f['name']}"))
        dialogue = [i for i in script["instructions"] if i["kind"] == "dialogue"]
        row["summary"] = (f"{len(script['instructions'])} 条指令 · {len(dialogue)} 条对白 · {len(script['blocks'])} 个条件块 · "
                          + {"terminator-reached": "已读至结束符"}.get(script["status"], script["status"]))
        row["search_terms"] = " ".join(filter(None, [i.get("text", "") for i in dialogue] +
                                              [i.get("speaker_label", "") for i in dialogue] + [trigger["name"]]))
        events.append(row)
        event_by_pointer[pointer] = row

    scenarios, flow_edges = [], 0
    for scene, pointer in enumerate(pointers):
        entries = lists[pointer]
        offset = event_bank["index_rom_offset"] + scene * 4
        row = raw_record(f"base:scenarios:{scene:04d}", f"场景索引 {scene:03d} · {len(entries)} 个事件入口",
                         checked_slice(rom, offset, 4), offset, "code-confirmed")
        list_offset = bank_offset(event_bank, pointer)
        aux_row = aux_by_pointer[auxiliary[scene]]
        by_type: dict[int, list[str]] = defaultdict(list)
        for e in entries:
            by_type[event_by_pointer[e]["script"]["header_words"][0]].append(event_by_pointer[e]["key"])
        next_scenes, speakers, flags, groups = set(), {}, set(), set()
        for e in entries:
            script = event_by_pointer[e]["script"]
            for item in script["instructions"]:
                if item.get("speaker_key"):
                    speakers[item["speaker_key"]] = item["speaker_label"]
                for f in item.get("fields", []):
                    if f["role"] == "scene" and "key" in f:
                        next_scenes.add(f["key"])
                    elif f["role"] == "flag" and f["value"] < 200:
                        flags.add(f["value"])
                    elif f["role"] == "group":
                        groups.add(f["value"])
        # Per-scene slot links: 3D52/3D53 index type-1 events, 3D57 indexes type-8 events, 3D45/3D3D groups index deployments.
        slot_links = []
        for e in entries:
            event_row = event_by_pointer[e]
            for item in event_row["script"]["instructions"]:
                for f in item.get("fields", []):
                    targets = []
                    if f["role"] == "event_slot_type1":
                        targets = by_type[1][f["value"]:f["value"] + 1]
                    elif f["role"] == "event_slot_type8":
                        targets = by_type[8][f["value"]:f["value"] + 1]
                    elif f["role"] == "group" or (f["role"] == "actor_or_group" and "group" in f):
                        targets = deployment_index[aux_row["key"]].get(f.get("group", f["value"]), [])
                    for target in targets:
                        f.setdefault("scene_targets", []).append({"scene": scene, "key": target})
                        slot_links.append(link(target, f"场景 {scene} · {item['name']} 参数 {f['value']}"))
                    if (f["role"] in ("event_slot_type1", "event_slot_type8", "group")
                            or (f["role"] == "actor_or_group" and "group" in f)) and not targets:
                        f.setdefault("scene_misses", []).append(scene)
            for l in slot_links:
                if l not in event_row["links"]:
                    event_row["links"].append(l)
            slot_links = []
        flow_edges += len(next_scenes)
        title_id = layout["names"]["stage_title_first"] + scene
        title_key = text_key(0, title_id)
        if title_id <= layout["names"]["stage_title_last"] and title_key in sources:
            row["title"] = {"key": title_key, "text_id": title_id,
                            "text": sources[title_key].replace("<END>", "").replace("<BR>", " "),
                            "basis": "整备画面 801CE0F8 以 8010F5F1 + 281 显示章节标题；存档列表 801C6944/801C754C 同公式。"}
            row["label"] = f"场景索引 {scene:03d} · {row['title']['text']}"
            row["label_confidence"] = "code-confirmed"
        row["scenario"] = {"scene_index": scene, "map_key": f"base:stage_maps:{scene:04d}",
            "auxiliary_key": aux_row["key"], "pointer_list_rom_offset": list_offset,
            "pointer_list_raw_hex": checked_slice(rom, list_offset, 4 * (len(entries) + 1)).hex(),
            "shared_scene_indices": [i for i, p in enumerate(pointers) if p == pointer and i != scene],
            "events": [{"key": event_by_pointer[e]["key"], "slot": slot,
                        "type": event_by_pointer[e]["script"]["header_words"][0],
                        "type_name": event_by_pointer[e]["script"]["trigger"]["name"],
                        "header_parameters": event_by_pointer[e]["script"]["header_words"][1:],
                        "trigger_fields": event_by_pointer[e]["script"]["trigger"]["fields"],
                        "runtime_entry_vram": f"0x{0x8019B400 + e - entries[0]:08X}",
                        "status": event_by_pointer[e]["script"]["status"],
                        "instruction_count": len(event_by_pointer[e]["script"]["instructions"]),
                        "dialogue_count": sum(i["kind"] == "dialogue" for i in event_by_pointer[e]["script"]["instructions"])}
                       for slot, e in enumerate(entries)],
            "events_by_type": {str(k): v for k, v in sorted(by_type.items())},
            "next_scene_keys": sorted(next_scenes), "speaker_keys": speakers,
            "flags_referenced": sorted(flags), "deployment_groups": sorted(groups),
            "deployment_group_keys": {str(g): deployment_index[aux_row["key"]].get(g, []) for g in sorted(groups)}}
        row["summary"] = (f"{len(entries)} 个事件 · {sum(e['dialogue_count'] for e in row['scenario']['events'])} 条对白 · "
                          f"{len(next_scenes)} 个后续场景")
        row["search_terms"] = " ".join(speakers.values())
        row["links"] = [link(row["scenario"]["map_key"], "同索引地图"), link(aux_row["key"], "同索引配套数据")]
        if "title" in row:
            row["links"].append(link(row["title"]["key"], "章节标题原文"))
            stage_row = categories["stages"][scene]
            stage_row["confidence"] = "code-confirmed"
            stage_row["fields"] = [{"name": "范围", "value": "章节标题；由整备画面按“281 + 场景索引”显示，已关联同索引场景脚本",
                                    "confidence": "code-confirmed"}]
            stage_row["links"].append(link(row["key"], "同索引场景脚本"))
            stage_row.setdefault("evidence", []).append("stage_title_display")
        row["links"] += [link(e["key"], f"入口 {e['slot']} · {e['type_name']}") for e in row["scenario"]["events"]]
        row["links"] += [link(k, "后续场景（3D4B）") for k in sorted(next_scenes)]
        row["links"] += [link(k, f"说话人 · {v}") for k, v in speakers.items()]
        row["evidence"] = ["stage_script_scene_argument", "stage_script_load_register", "stage_script_dma",
                           "script_event_triggers", "script_protagonist_marker"] + (["stage_title_display"] if "title" in row else [])
        categories["stage_maps"][scene]["links"].append(link(row["key"], "场景事件脚本"))
        scenarios.append(row)

    categories.update(scenarios=scenarios, stage_events=events, stage_auxiliary=aux_rows,
                      stage_deployments=deployments, **catalogs)
    all_items = [i for r in events for i in r["script"]["instructions"]]
    dialogue = [i for i in all_items if i["kind"] == "dialogue"]
    return {"scene_index_slots": len(pointers), "unique_event_lists": len(lists),
            "event_references": sum(map(len, (lists[p] for p in pointers))), "unique_events": len(events),
            "auxiliary_index_slots": len(auxiliary), "unique_auxiliary_blocks": len(aux_rows),
            "deployment_records": len(deployments),
            "auxiliary_blocks_without_terminator": sum(not r["auxiliary"]["terminated"] for r in aux_rows),
            "dispatch_slots": spec["dispatch"]["count"], "known_operand_layouts": sum(c["operand_words"] is not None for c in spec["commands"].values()),
            "condition_opcodes": len(spec["conditions"]), "context_markers": len(spec["context_markers"]),
            "decode_status": dict(Counter(r["script"]["status"] for r in events)),
            "stop_words": dict(Counter(f"{r['script']['stop_word']:04X}" for r in events if r["script"]["stop_word"] is not None)),
            "instructions": len(all_items),
            "opcode_frequency": dict(sorted(Counter(f"{i['opcode']:04X}" for i in all_items).items())),
            "dialogue_references": len(dialogue),
            "dialogue_speakers": dict(Counter(i.get("speaker_status", "n/a") for i in dialogue)),
            "condition_blocks": sum(len(r["script"]["blocks"]) for r in events),
            "blocks_closed_by_end": sum(b.get("closed_by") == "end" for r in events for b in r["script"]["blocks"]),
            "max_block_depth": max(r["script"]["max_depth"] for r in events),
            "unbalanced_block_ends": sum(r["script"]["unbalanced_block_ends"] for r in events),
            "hazards": dict(Counter(h["kind"] for r in events for h in r["script"]["hazards"])),
            "trailing_bytes_after_end": dict(sorted(Counter(len(bytes.fromhex(r["script"]["remainder_hex"]))
                                                      for r in events if r["script"]["status"] == "terminator-reached").items())),
            "scene_flow_edges": flow_edges,
            "event_types": dict(Counter(str(r["script"]["header_words"][0]) for r in events)),
            "scope": "静态结构与完整指令序列；不等于可玩关卡数，不代表实际执行路径或运行验证。"}
