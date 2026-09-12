"""Compile explicit N64 buttons indexed by native VI ticks."""
from __future__ import annotations

import struct

BUTTONS = {"a": 0x8000, "b": 0x4000, "z": 0x2000, "start": 0x1000,
           "up": 0x0800, "down": 0x0400, "left": 0x0200, "right": 0x0100,
           "l": 0x0020, "r": 0x0010, "c_up": 0x0008, "c_down": 0x0004,
           "c_left": 0x0002, "c_right": 0x0001}


def compile_input(document: dict, max_vis: int) -> bytes:
    if document.get("schema") != "srw64.recomp-input.v1":
        raise RuntimeError("input script must use the native VI schema")
    values = [0] * (max_vis + 1)
    for event in document["events"]:
        vi, duration = event["vi"], event["duration"]
        if type(vi) is not int or type(duration) is not int or vi < 0 or duration < 1 or vi + duration > max_vis:
            raise RuntimeError("input event outside native VI run")
        if event["button"] not in BUTTONS:
            raise RuntimeError("unknown N64 button")
        for index in range(vi, vi + duration):
            values[index] |= BUTTONS[event["button"]]
    return b"SRWI" + struct.pack(">I", len(values)) + struct.pack(">" + "H" * len(values), *values)
