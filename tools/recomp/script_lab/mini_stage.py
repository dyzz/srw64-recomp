#!/usr/bin/env python3
"""Compile a caller-written mini stage into the image the native host substitutes
when the game registers a scene's scripts.

Events use the same layout-locked assembler as debug scripts; deployment records
use the 14-halfword auxiliary record format. The image describes only what the
host writes into the game's own per-scene buffers (events at 8019B400, at most
0x1A00 bytes; deployment block at 80199400, at most 0x2000 bytes). Nothing here
touches the ROM, the catalog or the game's other scenes.
"""
from __future__ import annotations

import argparse
import functools
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from recomp.script_lab.script_debug import (SCHEMA as SCRIPT_SCHEMA, assemble, boundaries_from_words, frame_index,  # noqa: E402
                          join_spans, layout, save_frame, trace_rows)

SCHEMA = "srw64.mini-stage.v1"
IMAGE_SCHEMA = "srw64.mini-stage-image.v1"
EVENT_BLOCK = 0x8019B400
EVENT_LIMIT = 0x1A00
AUX_BLOCK = 0x80199400
AUX_LIMIT = 0x2000
MAX_EVENTS = 63  # 8009DD58 keeps the pointer table in a 0x100-byte stack buffer
RECORD_BYTES = 28
TERMINATOR = 999
# 14-halfword deployment record (layout lock auxiliary_record): name, offset, size.
RECORD_FIELDS = [("group", 0, 2), ("x", 2, 2), ("y", 4, 2), ("actor", 6, 2), ("byte8", 8, 1), ("level_offset", 9, 1),
                 ("unit", 10, 2), ("upgrade", 12, 2), ("raw14", 14, 2), ("raw16", 16, 2), ("raw18", 18, 2),
                 ("faction", 20, 2), ("behavior", 22, 2), ("extra", 24, 2), ("raw26", 26, 2)]
RECORDS = ROOT / "assets/original-data/records"


@functools.cache
def catalog_index(collection: str) -> dict[str, dict]:
    """Every record of one extracted collection by key; the files are tens of MB, so read each once."""
    path = RECORDS / f"{collection}.jsonl"
    if not path.exists():
        raise ValueError(f"Catalog collection {collection} is not extracted; run tools/content/extract_original.py")
    return {row["key"]: row for row in map(json.loads, path.read_text().splitlines())}


def catalog_record(collection: str, key: str) -> dict:
    row = catalog_index(collection).get(key)
    if row is None:
        raise ValueError(f"Unknown catalog record {key}")
    return row


def parse_block(raw: bytes) -> list[bytes]:
    """Records up to (excluding) the first one whose group halfword is 999."""
    records = []
    for offset in range(0, len(raw) - 1, RECORD_BYTES):
        if int.from_bytes(raw[offset:offset + 2], "big") == TERMINATOR:
            break
        if offset + RECORD_BYTES > len(raw):
            raise ValueError("Deployment block ends inside a record")
        records.append(raw[offset:offset + RECORD_BYTES])
    return records


def encode_record(spec: dict) -> bytes:
    if "template" in spec:
        record = bytearray(bytes.fromhex(catalog_record("stage_deployments", spec["template"])["raw_hex"]))
        if len(record) != RECORD_BYTES:
            raise ValueError(f"Template {spec['template']} is not one 28-byte record")
    else:
        record = bytearray(RECORD_BYTES)
    for name, offset, size in RECORD_FIELDS:
        if name not in spec:
            continue
        value = int(spec[name])
        if not 0 <= value < (1 << (8 * size)):
            raise ValueError(f"Deployment field {name} = {value} does not fit {size} byte(s)")
        record[offset:offset + size] = value.to_bytes(size, "big")
    unknown = set(spec) - {n for n, _, _ in RECORD_FIELDS} - {"template", "note"}
    if unknown:
        raise ValueError(f"Unknown deployment fields {sorted(unknown)}")
    return bytes(record)


def decode_record(record: bytes) -> dict:
    return {name: int.from_bytes(record[offset:offset + size], "big") for name, offset, size in RECORD_FIELDS}


def copied_event(key: str, spec: dict) -> tuple[list[int], list[dict]]:
    """An original event's words, unchanged, plus a listing of its instructions.

    Copying an event verbatim is how a command gets observed in the context the
    original game gives it, rather than one this tool made up: the bytes, the
    operands and the surrounding commands are all the game's own.
    """
    raw = bytes.fromhex(catalog_record("stage_events", key)["raw_hex"])
    words = [int.from_bytes(raw[i:i + 2], "big") for i in range(0, len(raw) - 1, 2)]
    if len(words) < 6 or words[0] > 14:
        raise ValueError(f"{key} is not one event")
    # Walk the instruction stream rather than scanning for FFFF: that value also
    # occurs as an operand, and the record's trailing bytes are not instructions.
    listing = boundaries_from_words(spec, words, 0)
    if not listing or listing[-1]["opcode"] != "FFFF":
        raise ValueError(f"{key} does not end in a terminator")
    end = (listing[-1]["pc"] + listing[-1]["size"]) // 2
    return words[:end], listing


def compile_stage(document: dict, spec: dict | None = None) -> dict:
    if document.get("schema") != SCHEMA:
        raise ValueError("Unsupported mini stage schema")
    spec = spec or layout()
    events, blob = [], bytearray()
    for index, event in enumerate(document.get("events", [])):
        if "copy_from" in event:
            words, listing = copied_event(event["copy_from"], spec)
            # An optional header override keeps every command byte of the original
            # while relaxing the trigger, so an event gated behind conditions a
            # bounded run cannot reach (a late turn, a clear count) still fires.
            # The commands are the game's; only when it runs changes.
            if "header" in event:
                header = [int(v) for v in event["header"]]
                if len(header) != 4 or any(not 0 <= v <= 0xFFFF for v in header):
                    raise ValueError("A header override is four 16-bit parameters")
                words = [words[0], *header, *words[5:]]
            assembled = {"words": words, "listing": listing}
            event_type = words[0]
        else:
            event_type = int(event["type"])
            assembled = assemble({"schema": SCRIPT_SCHEMA, "event_type": event_type,
                                  "header": [int(v) for v in event.get("header", [0, 0, 0, 0])],
                                  "commands": event.get("commands", [])}, spec)
        offset = len(blob)
        data = b"".join(w.to_bytes(2, "big") for w in assembled["words"])
        blob += data + b"\0" * (-len(data) % 4)
        events.append({"index": index, "name": event.get("name", f"event-{index}"), "type": event_type,
                       "header": assembled["words"][1:5], "offset": offset, "size": len(data),
                       "pointer": EVENT_BLOCK + offset, "listing": assembled["listing"]})
    if len(events) > MAX_EVENTS:
        raise ValueError(f"At most {MAX_EVENTS} events fit the pointer table")
    if len(blob) > EVENT_LIMIT:
        raise ValueError(f"Events take {len(blob)} bytes; the per-scene buffer holds {EVENT_LIMIT}")
    records = []
    if "deployments_from" in document:
        records += parse_block(bytes.fromhex(catalog_record("stage_auxiliary", document["deployments_from"])["raw_hex"]))
    records += [encode_record(r) for r in document.get("deployments", [])]
    aux = b"".join(records) + TERMINATOR.to_bytes(2, "big") + b"\0\0"
    if len(aux) > AUX_LIMIT:
        raise ValueError(f"Deployment block takes {len(aux)} bytes; the buffer holds {AUX_LIMIT}")
    map_id = document.get("map")
    if map_id is not None and not 0 <= int(map_id) <= 255:
        raise ValueError("map must be a map index 0..255")
    slot = document.get("slot")
    if slot is not None and not 0 <= int(slot) <= 255:
        raise ValueError("slot must be a scene index 0..255")
    return {"schema": IMAGE_SCHEMA, "name": document.get("name", "mini-stage"),
            "map": None if map_id is None else int(map_id), "slot": None if slot is None else int(slot),
            "event_block": EVENT_BLOCK, "aux_block": AUX_BLOCK,
            "events": events, "events_hex": blob.hex().upper(), "aux_hex": aux.hex().upper(),
            "deployments": [decode_record(r) for r in records], "source": document,
            "scope": "Bytes the host writes into the per-scene event and deployment buffers at registration; the game's own tables, ROM and other scenes are untouched."}


def host_events(run: Path) -> list[dict]:
    path = run / "mini-stage-events.jsonl"
    return [json.loads(l) for l in path.read_text().splitlines() if l.strip()] if path.exists() else []


def report(run: Path, image: dict) -> dict:
    """Join the run's script-trace polls to every event of the image."""
    spec = layout()
    rows = trace_rows(run)
    frames = frame_index(run)
    blob = bytes.fromhex(image["events_hex"])
    events = []
    for event in image["events"]:
        entry, size = event["pointer"], event["size"]
        words = [int.from_bytes(blob[i:i + 2], "big") for i in range(event["offset"], event["offset"] + size, 2)]
        boundaries = boundaries_from_words(spec, words, entry)
        commands = [b for b in boundaries if b["family"] == "command"]
        polls = [r for r in rows if entry <= r["before"]["pc"] < entry + size]
        spans = join_spans(spec, rows, boundaries, words, entry, polls[0]["vi"], polls[-1]["vi"]) if polls else []
        for span in spans:
            for edge in ("start_vi", "end_vi"):
                if span[edge] is not None and frames:
                    span[f"frame_{edge}"] = save_frame(run, frames, span[edge], run / f"stage-{event['index']}-{span['opcode']}-{edge}-{span[edge]}.png")
        events.append({"index": event["index"], "name": event["name"], "type": event["type"], "header": event["header"],
                       "pointer": entry, "fired": bool(polls), "first_vi": polls[0]["vi"] if polls else None,
                       "last_vi": polls[-1]["vi"] if polls else None, "polls": len(polls), "executed": spans,
                       "commands_declared": len(commands), "commands_executed": len(spans),
                       "commands_not_executed": [f"{b['opcode']}@{b['pc'] - entry}" for b in commands if b["pc"] not in {s["pc"] for s in spans}],
                       "all_executed_bounded": all(s["end_vi"] is not None for s in spans)})
    result = {"schema": "srw64.mini-stage-report.v1", "run": str(run), "image": image["name"], "host_events": host_events(run),
              "events": events, "scope": "Polling boundaries of the substituted scene's events on this host run; effects must be read from frames, state captures and the game itself."}
    (run / "mini-stage-report.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compile"); c.add_argument("stage", type=Path); c.add_argument("--out", type=Path, required=True)
    r = sub.add_parser("report"); r.add_argument("run", type=Path); r.add_argument("--image", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "report":
        result = report(args.run, json.loads(args.image.read_text()))
        print(json.dumps([{k: e[k] for k in ("name", "type", "fired", "first_vi", "last_vi", "commands_declared", "commands_executed", "commands_not_executed")} for e in result["events"]], ensure_ascii=False, indent=1))
        return 0
    if args.command == "compile":
        image = compile_stage(json.loads(args.stage.read_text()))
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(image, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps({"name": image["name"], "events": [(e["type"], e["offset"], e["size"]) for e in image["events"]],
                          "event_bytes": len(image["events_hex"]) // 2, "aux_bytes": len(image["aux_hex"]) // 2,
                          "deployments": len(image["deployments"]), "map": image["map"], "out": str(args.out)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
