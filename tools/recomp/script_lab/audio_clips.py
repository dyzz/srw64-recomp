#!/usr/bin/env python3
"""Cut a mini-stage audio capture into one WAV per executed script command.

The host's bounded capture is a flat stereo PCM stream covering a VI window; the
mini-stage report says which command occupied which VI span. Joining the two
gives a clip per command, which is what a listener needs to say what a sound
actually is. RMS only answers whether a command made a sound at all.

Clip boundaries are the command's own polling span, so a clip is evidence about
that command on that run and nothing more.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import wave

SCHEMA = "srw64.audio-clips.v1"


def capture(run: Path) -> tuple[bytes, int, int, int]:
    """Raw stereo PCM plus its rate and the VI window it covers."""
    meta = json.loads((run / "audio-output.json").read_text())
    if meta.get("schema") != "srw64.native-audio-capture.v1":
        raise ValueError("Unsupported capture schema")
    if "capture_from_vi" not in meta:
        raise ValueError("This capture has no VI window; rerun with SRW64_AUDIO_CAPTURE_FROM/_TO")
    return ((run / "audio-output.s16").read_bytes(), int(meta["frequency"]),
            int(meta["capture_from_vi"]), int(meta["capture_to_vi"]))


def slice_frames(pcm: bytes, window: tuple[int, int], start_vi: int, end_vi: int) -> bytes:
    frames = len(pcm) // 4
    first, last = window
    span = last - first
    a = int((start_vi - first) / span * frames)
    b = int((end_vi - first) / span * frames)
    a, b = max(0, min(frames, a)), max(0, min(frames, b))
    return pcm[a * 4:b * 4] if b > a else b""


def rms(chunk: bytes) -> tuple[float, int]:
    if not chunk:
        return 0.0, 0
    values = struct.unpack(f"<{len(chunk) // 2}h", chunk)
    return (sum(v * v for v in values) / len(values)) ** 0.5, max(abs(v) for v in values)


def write_wav(path: Path, chunk: bytes, rate: int, downmix: int = 1) -> None:
    """Write the clip, optionally mono and decimated by `downmix`.

    Listening only needs enough fidelity to recognise a sound; a page carrying
    several minutes of stereo 44.1 kHz does not fit an artifact. Decimation is a
    plain stride, which is adequate here and keeps the tool dependency-free.
    """
    if downmix > 1:
        values = struct.unpack(f"<{len(chunk) // 2}h", chunk)
        mono = [(values[i] + values[i + 1]) // 2 for i in range(0, len(values) - 1, 2)]
        kept = mono[::downmix]
        chunk = struct.pack(f"<{len(kept)}h", *kept)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1 if downmix > 1 else 2)
        out.setsampwidth(2)
        out.setframerate(rate // downmix if downmix > 1 else rate)
        out.writeframes(chunk)


def clips(run: Path, report: dict, pad_vi: int, only: set[str] | None, downmix: int = 1) -> dict:
    pcm, rate, first, last = capture(run)
    directory = run / "clips"
    directory.mkdir(exist_ok=True)
    rows = []
    for event in report["events"]:
        for span in event["executed"]:
            start, end = span["start_vi"], span["end_vi"]
            if start is None or end is None:
                continue
            if only and span["opcode"] not in only:
                continue
            if end + pad_vi <= first or start >= last:
                continue  # outside the captured window
            chunk = slice_frames(pcm, (first, last), start, end + pad_vi)
            if not chunk:
                continue
            operands = span.get("operands") or []
            name = f"{span['opcode']}" + ("-" + "-".join(str(o) for o in operands) if operands else "")
            path = directory / f"{event['index']}-{start}-{name}.wav"
            write_wav(path, chunk, rate, downmix)
            level, peak = rms(chunk)
            rows.append({"event": event["name"], "opcode": span["opcode"], "operands": operands,
                         "start_vi": start, "end_vi": end, "seconds": round(len(chunk) / 4 / rate, 2),
                         "rms": round(level, 1), "peak": peak, "wav": path.name})
    result = {"schema": SCHEMA, "run": str(run), "rate": rate, "window": {"from_vi": first, "to_vi": last},
              "pad_vi": pad_vi, "downmix": downmix, "clips": rows,
              "scope": "One clip per command span of this run; levels describe these samples only."}
    (directory / "clips.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--report", type=Path, help="mini-stage report; defaults to the run's own")
    parser.add_argument("--pad-vi", type=int, default=90, help="extra VIs kept after each command, for sounds that outlast the command")
    parser.add_argument("--only", help="comma-separated opcodes to cut")
    parser.add_argument("--downmix", type=int, default=1, help="write mono and keep every Nth sample; 2 halves the rate")
    args = parser.parse_args()
    report = json.loads((args.report or args.run / "mini-stage-report.json").read_text())
    only = {o.strip().upper() for o in args.only.split(",")} if args.only else None
    result = clips(args.run, report, args.pad_vi, only, args.downmix)
    print(json.dumps([{k: c[k] for k in ("opcode", "operands", "seconds", "rms", "peak", "wav")}
                      for c in result["clips"]], ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
