#!/usr/bin/env python3
"""Assemble, inject and report caller-written event scripts on the native host.

The assembler only knows the operand layouts recorded in the layout lock; it
refuses opcodes without a handler and never guesses lengths. Injection goes
through the host's script-inject.txt protocol (SRW64_SCRIPT_INJECT=1) and the
report joins the host's script-trace polls to the assembled instruction
boundaries. Observed timing and frames are evidence about this run only.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/recomp"))
from verification_support import write_control  # noqa: E402

SCHEMA = "srw64.debug-script.v1"
NULL_HANDLERS = {0x3D76, 0x3D77}
UNREACHABLE = {0x3D78, 0x3D79}


def layout() -> dict:
    return json.loads((ROOT / "config/data/original-jp-v1.json").read_text())["stage_scripts"]


def opcode_spec(spec: dict, opcode: int) -> tuple[str, dict]:
    key = f"{opcode:04x}"
    if key in spec["commands"]:
        return "command", spec["commands"][key]
    if key in spec["conditions"]:
        return "condition", spec["conditions"][key]
    if key in spec["context_markers"]:
        return "marker", {"operand_words": 0, "name": spec["context_markers"][key]["name"]}
    raise ValueError(f"Unknown opcode {opcode:04X}")


def assemble(document: dict, spec: dict | None = None) -> dict:
    """Return words plus a listing; every instruction keeps its byte offset."""
    spec = spec or layout()
    if document.get("schema") != SCHEMA:
        raise ValueError("Unsupported debug script schema")
    event_type = int(document.get("event_type", 12))
    header = [int(v) for v in document.get("header", [0, 0, 0, 0])]
    if not 0 <= event_type <= 14 or len(header) != 4 or any(not 0 <= v <= 0xFFFF for v in header):
        raise ValueError("Event header must be a type 0..14 and four 16-bit parameters")
    words = [event_type, *header]
    listing = []
    for index, row in enumerate(document["commands"]):
        opcode = int(row["op"], 16) if isinstance(row["op"], str) else int(row["op"])
        family, known = opcode_spec(spec, opcode)
        if opcode in NULL_HANDLERS or opcode in UNREACHABLE:
            raise ValueError(f"{opcode:04X} has no reachable handler")
        count = known["operand_words"]
        args = [int(a) for a in row.get("args", [])]
        if len(args) != count:
            raise ValueError(f"{opcode:04X} takes {count} operand words, got {len(args)}")
        if any(not 0 <= a <= 0xFFFF for a in args):
            raise ValueError(f"{opcode:04X} operands must be 16-bit")
        listing.append({"index": index, "offset": len(words) * 2, "opcode": f"{opcode:04X}", "family": family,
                        "name": known["name"], "operands": args, "note": row.get("note", "")})
        words += [opcode, *args]
    listing.append({"index": len(listing), "offset": len(words) * 2, "opcode": "FFFF", "family": "end", "name": "事件结束", "operands": []})
    words.append(0xFFFF)
    return {"schema": "srw64.debug-script-assembly.v1", "name": document.get("name", ""), "words": words,
            "hex": "".join(f"{w:04X}" for w in words), "listing": listing}


def next_sequence(run: Path) -> int:
    return max([r.get("sequence", 0) for r in events(run)] + [0]) + 1


def live_vi(run: Path) -> int:
    try:
        return int(json.loads((run / "live-state.json").read_text())["vi"])
    except (OSError, ValueError, KeyError):
        return 0


def events(run: Path) -> list[dict]:
    path = run / "script-inject-events.jsonl"
    return [json.loads(l) for l in path.read_text().splitlines()] if path.exists() else []


def inject(run: Path, document: dict, at_vi: int | None, wait: float) -> dict:
    assembled = assemble(document)
    sequence = next_sequence(run)
    at = at_vi if at_vi is not None else live_vi(run) + 30
    write_control(run / "script-inject.txt", f"SRWJ1 {sequence} {at} {assembled['hex']}\n")
    (run / f"script-inject-{sequence}.json").write_text(json.dumps({**assembled, "sequence": sequence, "at_vi": at, "source": document}, ensure_ascii=False, indent=2) + "\n")
    deadline = time.monotonic() + wait
    outcome = None
    while time.monotonic() < deadline:
        for row in events(run):
            if row.get("sequence") == sequence and row["action"] in ("complete", "rejected", "escaped"):
                outcome = row
        if outcome:
            break
        time.sleep(0.5)
    return {"sequence": sequence, "at_vi": at, "outcome": outcome}


def trace_rows(run: Path) -> list[dict]:
    log = run.parent / (run.name + ".native.log")
    prefix = "SRW64_SCRIPT_TRACE "
    rows = []
    for line in log.read_text(errors="replace").splitlines():
        if prefix in line:
            try:
                rows.append(json.loads(line.split(prefix, 1)[1]))
            except json.JSONDecodeError:
                pass
    return rows


def frame_index(run: Path) -> list[dict]:
    path = run / "frame-trace.jsonl"
    if not path.exists():
        return []
    return [json.loads(l) for l in path.read_text().splitlines() if l.strip()]


def save_frame(run: Path, frames: list[dict], vi: int, target: Path) -> dict | None:
    """Nearest sampled frame at or after VI, written as PNG from frame-trace.rgb."""
    candidates = [(i, f) for i, f in enumerate(frames) if f["vi"] >= vi]
    if not candidates:
        return None
    index, frame = min(candidates, key=lambda c: c[1]["vi"])
    width, height = frame["width"], frame["height"]
    size = width * height * 3
    with (run / "frame-trace.rgb").open("rb") as raw:
        raw.seek(index * size)
        data = raw.read(size)
    if len(data) != size:
        return None
    from PIL import Image
    Image.frombytes("RGB", (width, height), data).save(target)
    return {"vi": frame["vi"], "file": target.name, "mean_rgb": frame.get("mean_rgb")}


REGION_DECODERS = {
    "player_progress_and_names": [("side_phase", 0x0, 1), ("turn", 0x2, 2), ("total_turns", 0x4, 2), ("scene", 0x8, 1), ("funds", 0xC, 4)],
}


def decode_roster(raw: bytes) -> dict:
    """map_roster: 3 sides x 30 slots x 0x14 bytes; +0 state, +4 x, +5 y, +0xB flags/group, +0xC unit pointer."""
    slots = {}
    for side in range(3):
        for slot in range(30):
            base = side * 0x258 + slot * 0x14
            if raw[base] == 0:
                continue
            slots[f"{side}/{slot}"] = {"state": raw[base], "x": raw[base + 4], "y": raw[base + 5], "group": raw[base + 0xB] & 0x1F,
                                       "unit_pointer": int.from_bytes(raw[base + 0xC:base + 0x10], "big")}
    return slots


def state_diff(run: Path, sequence: int) -> dict | None:
    files = {}
    for path in run.glob("state-*-script-inject-*.json"):
        data = json.loads(path.read_text())
        if data.get("argument") == sequence:
            files[data["boundary"]] = data
    if "script-inject-applied" not in files or "script-inject-complete" not in files:
        return None
    a, b = files["script-inject-applied"]["regions"], files["script-inject-complete"]["regions"]
    changes, decoded = {}, {}
    for name in a:
        ra, rb = bytes.fromhex(a[name]["bytes"]), bytes.fromhex(b[name]["bytes"])
        diff = [{"offset": i, "before": ra[i], "after": rb[i]} for i in range(min(len(ra), len(rb))) if ra[i] != rb[i]]
        if diff:
            changes[name] = {"changed_bytes": len(diff), "first": diff[:64]}
        for label, offset, size in REGION_DECODERS.get(name, []):
            va, vb = int.from_bytes(ra[offset:offset + size], "big"), int.from_bytes(rb[offset:offset + size], "big")
            if va != vb:
                decoded[label] = {"before": va, "after": vb}
        if name == "map_roster":
            sa, sb = decode_roster(ra), decode_roster(rb)
            roster = {k: {"before": sa.get(k), "after": sb.get(k)} for k in sorted(set(sa) | set(sb)) if sa.get(k) != sb.get(k)}
            if roster:
                decoded["map_roster"] = roster
        if name == "campaign_flags":
            values = {}
            for var in range(200):
                wa = int.from_bytes(ra[var // 8 * 2:var // 8 * 2 + 2], "big"); wb = int.from_bytes(rb[var // 8 * 2:var // 8 * 2 + 2], "big")
                shift = (var % 8) * 2
                if (wa >> shift) & 3 != (wb >> shift) & 3:
                    values[var] = {"before": (wa >> shift) & 3, "after": (wb >> shift) & 3}
            if values:
                decoded["variables"] = values
    return {"applied_vi": files["script-inject-applied"]["vi"], "complete_vi": files["script-inject-complete"]["vi"],
            "changed_regions": changes, "decoded": decoded,
            "scope": "Byte diff of the fixed state-probe regions between the applied and complete boundaries; other memory is not covered."}


def boundaries_from_words(spec: dict, words: list[int], entry: int, start: int = 10) -> list[dict]:
    """Instruction boundaries of one event: header skipped, stops at the first FFFF."""
    boundaries, position = [], start
    while position < len(words) * 2:
        opcode = words[position // 2]
        family, known = opcode_spec(spec, opcode) if opcode != 0xFFFF else ("end", {"operand_words": 0, "name": "事件结束"})
        count = known["operand_words"]
        boundaries.append({"pc": entry + position, "opcode": f"{opcode:04X}", "family": family, "name": known["name"],
                           "operands": words[position // 2 + 1:position // 2 + 1 + count], "size": 2 + 2 * count})
        position += 2 + 2 * count
        if opcode == 0xFFFF:
            break
    return boundaries


def join_spans(spec: dict, rows: list[dict], boundaries: list[dict], words: list[int], entry: int, start_vi: int, end_vi: float) -> list[dict]:
    """Executed commands of one event from script-trace polls between start_vi and end_vi.

    One poll (8009EFDC) scans structural words, then runs at most one command: it is
    either still running (PC sits just past its opcode) or already complete (PC sits
    at the next boundary). Commands inside false blocks or foreign route segments
    therefore never produce a span."""
    by_pc = {b["pc"]: b for b in boundaries}
    commands = [b for b in boundaries if b["family"] == "command"]
    spans, pending = [], None
    for row in rows:
        if not start_vi <= row["vi"] <= end_vi:
            continue
        before, after = row["before"], row["after"]
        if pending is None:
            if after["state"] != 0:
                b = by_pc.get(after["pc"] - 2)
            else:
                b = next((c for c in commands if before["pc"] <= c["pc"] and c["pc"] + c["size"] == after["pc"]), None)
            if b is None or b["family"] != "command":
                continue
            pending = {"pc": b["pc"], "opcode": b["opcode"], "family": b["family"], "name": b["name"], "operands": b["operands"],
                       "start_vi": row["vi"], "end_vi": None, "elapsed_vis": None,
                       "operand_words_observed": words[(b["pc"] - entry) // 2:(b["pc"] - entry) // 2 + 1 + len(b["operands"])] == [int(b["opcode"], 16), *b["operands"]],
                       "semantic_confidence": (spec["commands"].get(b["opcode"].lower()) or spec["conditions"].get(b["opcode"].lower()) or {}).get("semantic_confidence")}
            spans.append(pending)
        if after["state"] == 0 and after["pc"] == pending["pc"] + by_pc[pending["pc"]]["size"]:
            pending["end_vi"] = row["vi"]; pending["elapsed_vis"] = row["vi"] - pending["start_vi"]; pending = None
    return spans


def report(run: Path) -> dict:
    spec = layout()
    rows = trace_rows(run)
    injections = []
    for applied in [e for e in events(run) if e["action"] == "applied"]:
        sequence = applied["sequence"]
        words = [int(applied["words_hex"][i:i + 4], 16) for i in range(0, len(applied["words_hex"]), 4)]
        entry = applied["entry"]
        boundaries = boundaries_from_words(spec, words, entry)
        commands = [b for b in boundaries if b["family"] == "command"]
        outcome = next((e for e in events(run) if e.get("sequence") == sequence and e["action"] in ("complete", "escaped", "stalled")), None)
        window_end = outcome["vi"] if outcome else float("inf")
        spans = join_spans(spec, rows, boundaries, words, entry, applied["vi"], window_end)
        frames = frame_index(run)
        for span in spans:
            for edge in ("start_vi", "end_vi"):
                if span[edge] is not None and frames:
                    span[f"frame_{edge}"] = save_frame(run, frames, span[edge], run / f"inject-{sequence}-{span['opcode']}-{edge}-{span[edge]}.png")
        injections.append({"sequence": sequence, "applied_vi": applied["vi"], "entry": entry, "instructions": boundaries,
                           "state_diff": state_diff(run, sequence),
                           "executed": spans, "outcome": outcome, "before": applied.get("before"),
                           "after": outcome.get("after") if outcome else None,
                           "commands_declared": len(commands), "commands_executed": len(spans),
                           "commands_not_executed": [f"{b['opcode']}@{b['pc'] - entry}" for b in commands if b["pc"] not in {s["pc"] for s in spans}],
                           "all_boundaries_observed": bool(spans) and all(s["end_vi"] is not None for s in spans),
                           "note": "Conditions and markers are evaluated inside one poll (8009F0E8) and never appear as separate command boundaries; commands inside a false block or a foreign route marker are listed under commands_not_executed."})
    result = {"schema": "srw64.script-debug-report.v1", "run": str(run), "injections": injections,
              "events": events(run), "scope": "Observed polling boundaries of injected scripts on this host run; effects must be read from frames, state captures and the game itself."}
    (run / "script-debug-report.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    a = sub.add_parser("assemble"); a.add_argument("script", type=Path)
    i = sub.add_parser("inject"); i.add_argument("run", type=Path); i.add_argument("--script", type=Path, required=True)
    i.add_argument("--at-vi", type=int); i.add_argument("--wait", type=float, default=120)
    r = sub.add_parser("report"); r.add_argument("run", type=Path)
    args = parser.parse_args()
    if args.command == "assemble":
        result = assemble(json.loads(args.script.read_text()))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    if args.command == "inject":
        result = inject(args.run.resolve(), json.loads(args.script.read_text()), args.at_vi, args.wait)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result["outcome"] and result["outcome"]["action"] == "complete" else 1
    result = report(args.run.resolve())
    print(json.dumps([{k: v for k, v in inj.items() if k in ("sequence", "applied_vi", "all_boundaries_observed", "outcome")} for inj in result["injections"]], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
