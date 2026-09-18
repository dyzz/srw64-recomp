#!/usr/bin/env python3
"""Minimal deterministic libretro frontend for SRW64 runtime smoke tests.

This deliberately drives RetroPad state through the libretro callbacks instead
of synthesising macOS keyboard events.  It supports software-rendered cores,
frame-addressed input scripts, PNG capture, and optional save-state I/O.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import (
    CFUNCTYPE,
    POINTER,
    Structure,
    c_bool,
    c_char_p,
    c_double,
    c_int,
    c_int16,
    c_size_t,
    c_uint,
    c_uint32,
    c_void_p,
)
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from typing import Any

from PIL import Image


RETRO_API_VERSION = 1
INPUT_SCRIPT_SCHEMA = "srw64.libretro-input.v1"
RETRO_DEVICE_JOYPAD = 1
RETRO_DEVICE_ID_JOYPAD_MASK = 256
RETRO_HW_FRAME_BUFFER_VALID = c_void_p(-1).value

RETRO_ENVIRONMENT_GET_OVERSCAN = 2
RETRO_ENVIRONMENT_GET_CAN_DUPE = 3
RETRO_ENVIRONMENT_SET_MESSAGE = 6
RETRO_ENVIRONMENT_SHUTDOWN = 7
RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL = 8
RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY = 9
RETRO_ENVIRONMENT_SET_PIXEL_FORMAT = 10
RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS = 11
RETRO_ENVIRONMENT_SET_HW_RENDER = 14
RETRO_ENVIRONMENT_GET_VARIABLE = 15
RETRO_ENVIRONMENT_SET_VARIABLES = 16
RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE = 17
RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME = 18
RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE = 23
RETRO_ENVIRONMENT_GET_LOG_INTERFACE = 27
RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY = 30
RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY = 31
RETRO_ENVIRONMENT_SET_CONTROLLER_INFO = 35
RETRO_ENVIRONMENT_GET_USERNAME = 38
RETRO_ENVIRONMENT_GET_LANGUAGE = 39
RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS = 44
RETRO_ENVIRONMENT_GET_VFS_INTERFACE = 45
RETRO_ENVIRONMENT_GET_LED_INTERFACE = 46
RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE = 47
RETRO_ENVIRONMENT_GET_INPUT_BITMASKS = 51
RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION = 52
RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 = 67
RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL = 68

RETRO_PIXEL_FORMAT_0RGB1555 = 0
RETRO_PIXEL_FORMAT_XRGB8888 = 1
RETRO_PIXEL_FORMAT_RGB565 = 2

# RetroPad IDs.  For Mupen64Plus-Next, RetroPad B is the default N64 A button
# and RetroPad Y is the default N64 B button.
BUTTON_IDS = {
    "n64_a": 0,
    "n64_b": 1,
    "select": 2,
    "start": 3,
    "up": 4,
    "down": 5,
    "left": 6,
    "right": 7,
    "retropad_a": 8,
    "x": 9,
    "l": 10,
    "r": 11,
    "l2": 12,
    "r2": 13,
    "l3": 14,
    "r3": 15,
}


class RetroSystemInfo(Structure):
    _fields_ = [
        ("library_name", c_char_p),
        ("library_version", c_char_p),
        ("valid_extensions", c_char_p),
        ("need_fullpath", c_bool),
        ("block_extract", c_bool),
    ]


class RetroGameGeometry(Structure):
    _fields_ = [
        ("base_width", c_uint),
        ("base_height", c_uint),
        ("max_width", c_uint),
        ("max_height", c_uint),
        ("aspect_ratio", ctypes.c_float),
    ]


class RetroSystemTiming(Structure):
    _fields_ = [("fps", c_double), ("sample_rate", c_double)]


class RetroSystemAVInfo(Structure):
    _fields_ = [("geometry", RetroGameGeometry), ("timing", RetroSystemTiming)]


class RetroGameInfo(Structure):
    _fields_ = [
        ("path", c_char_p),
        ("data", c_void_p),
        ("size", c_size_t),
        ("meta", c_char_p),
    ]


class RetroVariable(Structure):
    _fields_ = [("key", c_char_p), ("value", c_char_p)]


class RetroInputDescriptor(Structure):
    _fields_ = [
        ("port", c_uint),
        ("device", c_uint),
        ("index", c_uint),
        ("id", c_uint),
        ("description", c_char_p),
    ]


EnvironmentCallback = CFUNCTYPE(c_bool, c_uint, c_void_p)
VideoCallback = CFUNCTYPE(None, c_void_p, c_uint, c_uint, c_size_t)
AudioSampleCallback = CFUNCTYPE(None, c_int16, c_int16)
AudioBatchCallback = CFUNCTYPE(c_size_t, POINTER(c_int16), c_size_t)
InputPollCallback = CFUNCTYPE(None)
InputStateCallback = CFUNCTYPE(c_int16, c_uint, c_uint, c_uint, c_uint)
LogPrintfCallback = CFUNCTYPE(None, c_int, c_char_p)


class RetroLogCallback(Structure):
    _fields_ = [("log", LogPrintfCallback)]


class RunnerError(RuntimeError):
    pass


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _decode(value: bytes | None) -> str:
    return value.decode("utf-8", "replace") if value else ""


class LibretroFrontend:
    def __init__(
        self,
        core_path: Path,
        rom_path: Path,
        work_dir: Path,
        option_overrides: dict[str, str],
    ) -> None:
        self.core_path = core_path.resolve()
        self.rom_path = rom_path.resolve()
        self.work_dir = work_dir.resolve()
        self.system_dir = self.work_dir / "system"
        self.save_dir = self.work_dir / "saves"
        self.screenshot_dir = self.work_dir / "screenshots"
        for directory in (self.system_dir, self.save_dir, self.screenshot_dir):
            directory.mkdir(parents=True, exist_ok=True)

        self._system_dir_bytes = str(self.system_dir).encode()
        self._save_dir_bytes = str(self.save_dir).encode()
        self._content_dir_bytes = str(self.rom_path.parent).encode()
        self._option_overrides = option_overrides
        self._trace = os.environ.get("LIBRETRO_RUNNER_TRACE") == "1"
        self._option_bytes: dict[str, bytes] = {}
        self._option_defaults: dict[str, str] = {}
        self._environment_commands: dict[int, int] = {}
        self._unknown_environment_commands: dict[int, int] = {}
        self.input_descriptors: list[dict[str, Any]] = []
        self.shutdown_requested = False
        self.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555
        self.active_buttons: set[int] = set()
        self.last_frame: bytes | None = None
        self.last_width = 0
        self.last_height = 0
        self.last_pitch = 0
        self.video_callback_count = 0
        self.hardware_frame_count = 0
        self.dupe_frame_count = 0

        self._environment_cb = EnvironmentCallback(self._environment)
        self._video_cb = VideoCallback(self._video)
        # retro_log_printf_t is variadic. The first two arguments are stable on
        # the C ABI; this fixed-prefix closure safely accepts and ignores any
        # additional formatting arguments supplied by the core.
        self._log_cb = LogPrintfCallback(self._log)
        self._audio_sample_cb = AudioSampleCallback(lambda _left, _right: None)
        self._audio_batch_cb = AudioBatchCallback(lambda _data, frames: frames)
        self._input_poll_cb = InputPollCallback(lambda: None)
        self._input_state_cb = InputStateCallback(self._input_state)

        self.core = ctypes.CDLL(str(self.core_path))
        self._configure_abi()
        self._rom_bytes = self.rom_path.read_bytes()
        self._rom_buffer = ctypes.create_string_buffer(self._rom_bytes)
        self._loaded = False
        self._initialized = False

    def _configure_abi(self) -> None:
        core = self.core
        core.retro_api_version.argtypes = []
        core.retro_api_version.restype = c_uint
        core.retro_set_environment.argtypes = [EnvironmentCallback]
        core.retro_set_video_refresh.argtypes = [VideoCallback]
        core.retro_set_audio_sample.argtypes = [AudioSampleCallback]
        core.retro_set_audio_sample_batch.argtypes = [AudioBatchCallback]
        core.retro_set_input_poll.argtypes = [InputPollCallback]
        core.retro_set_input_state.argtypes = [InputStateCallback]
        core.retro_init.argtypes = []
        core.retro_deinit.argtypes = []
        core.retro_get_system_info.argtypes = [POINTER(RetroSystemInfo)]
        core.retro_get_system_av_info.argtypes = [POINTER(RetroSystemAVInfo)]
        core.retro_set_controller_port_device.argtypes = [c_uint, c_uint]
        core.retro_load_game.argtypes = [POINTER(RetroGameInfo)]
        core.retro_load_game.restype = c_bool
        core.retro_unload_game.argtypes = []
        core.retro_run.argtypes = []
        core.retro_serialize_size.argtypes = []
        core.retro_serialize_size.restype = c_size_t
        core.retro_serialize.argtypes = [c_void_p, c_size_t]
        core.retro_serialize.restype = c_bool
        core.retro_unserialize.argtypes = [c_void_p, c_size_t]
        core.retro_unserialize.restype = c_bool

    def _environment(self, command: int, data: int | None) -> bool:
        self._environment_commands[command] = self._environment_commands.get(command, 0) + 1
        if self._trace:
            print(f"libretro environment command={command} data={data!r}", file=sys.stderr, flush=True)
        if command == RETRO_ENVIRONMENT_GET_OVERSCAN:
            ctypes.cast(data, POINTER(c_bool))[0] = False
            return True
        if command == RETRO_ENVIRONMENT_GET_CAN_DUPE:
            ctypes.cast(data, POINTER(c_bool))[0] = True
            return True
        if command == RETRO_ENVIRONMENT_SET_MESSAGE:
            return True
        if command == RETRO_ENVIRONMENT_SHUTDOWN:
            self.shutdown_requested = True
            return True
        if command == RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
            return True
        if command == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            ctypes.cast(data, POINTER(c_char_p))[0] = self._system_dir_bytes
            return True
        if command == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            requested = ctypes.cast(data, POINTER(c_uint))[0]
            if requested not in (
                RETRO_PIXEL_FORMAT_0RGB1555,
                RETRO_PIXEL_FORMAT_XRGB8888,
                RETRO_PIXEL_FORMAT_RGB565,
            ):
                return False
            self.pixel_format = int(requested)
            return True
        if command == RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            descriptors = ctypes.cast(data, POINTER(RetroInputDescriptor))
            self.input_descriptors.clear()
            for index in range(256):
                descriptor = descriptors[index]
                if not descriptor.description:
                    break
                self.input_descriptors.append(
                    {
                        "port": int(descriptor.port),
                        "device": int(descriptor.device),
                        "index": int(descriptor.index),
                        "id": int(descriptor.id),
                        "description": _decode(descriptor.description),
                    }
                )
            return True
        if command == RETRO_ENVIRONMENT_SET_HW_RENDER:
            # This frontend intentionally supports software frames only.  The
            # RDP option override selects Angrylion before retro_load_game().
            return False
        if command == RETRO_ENVIRONMENT_GET_VARIABLE:
            variable = ctypes.cast(data, POINTER(RetroVariable)).contents
            key = _decode(variable.key)
            value = self._option_overrides.get(key, self._option_defaults.get(key))
            if self._trace:
                print(f"libretro option {key}={value!r}", file=sys.stderr, flush=True)
            if value is None:
                variable.value = None
                return True
            encoded = value.encode()
            self._option_bytes[key] = encoded
            variable.value = encoded
            return True
        if command == RETRO_ENVIRONMENT_SET_VARIABLES:
            variables = ctypes.cast(data, POINTER(RetroVariable))
            for index in range(4096):
                variable = variables[index]
                if not variable.key:
                    break
                key = _decode(variable.key)
                definition = _decode(variable.value)
                choices = definition.partition(";")[2].strip()
                if choices:
                    self._option_defaults[key] = choices.split("|", 1)[0]
            return True
        if command == RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            ctypes.cast(data, POINTER(c_bool))[0] = False
            return True
        if command == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return True
        if command == RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
            return False
        if command == RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            ctypes.cast(data, POINTER(RetroLogCallback)).contents.log = self._log_cb
            return True
        if command == RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY:
            ctypes.cast(data, POINTER(c_char_p))[0] = self._content_dir_bytes
            return True
        if command == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            ctypes.cast(data, POINTER(c_char_p))[0] = self._save_dir_bytes
            return True
        if command == RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            return True
        if command == RETRO_ENVIRONMENT_GET_USERNAME:
            ctypes.cast(data, POINTER(c_char_p))[0] = None
            return True
        if command == RETRO_ENVIRONMENT_GET_LANGUAGE:
            ctypes.cast(data, POINTER(c_uint))[0] = 0  # English
            return True
        if command == RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return True
        if command in (
            RETRO_ENVIRONMENT_GET_VFS_INTERFACE,
            RETRO_ENVIRONMENT_GET_LED_INTERFACE,
        ):
            return False
        if command == RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            ctypes.cast(data, POINTER(c_uint))[0] = 3
            return True
        if command == RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return True
        if command == RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            # Request the simple RETRO_ENVIRONMENT_SET_VARIABLES fallback.  It
            # is sufficient for deterministic option overrides and keeps this
            # frontend independent of the larger v2 option structures.
            ctypes.cast(data, POINTER(c_uint))[0] = 0
            return True
        if command in (
            RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
        ):
            return False
        self._unknown_environment_commands[command] = (
            self._unknown_environment_commands.get(command, 0) + 1
        )
        return False

    def _log(self, level: int, message: bytes | None) -> None:
        if self._trace:
            text = _decode(message).rstrip()
            print(f"libretro log level={level}: {text}", file=sys.stderr, flush=True)

    def _video(
        self, data: int | None, width: int, height: int, pitch: int
    ) -> None:
        self.video_callback_count += 1
        if data is None:
            self.dupe_frame_count += 1
            return
        if data == RETRO_HW_FRAME_BUFFER_VALID:
            self.hardware_frame_count += 1
            return
        byte_count = int(pitch) * int(height)
        self.last_frame = ctypes.string_at(data, byte_count)
        self.last_width = int(width)
        self.last_height = int(height)
        self.last_pitch = int(pitch)

    def _input_state(
        self, port: int, device: int, index: int, button_id: int
    ) -> int:
        if port != 0 or (device & 0xFF) != RETRO_DEVICE_JOYPAD or index != 0:
            return 0
        if button_id == RETRO_DEVICE_ID_JOYPAD_MASK:
            mask = sum(1 << button for button in self.active_buttons)
            return c_int16(mask).value
        return 1 if button_id in self.active_buttons else 0

    def initialize(self) -> dict[str, Any]:
        self.core.retro_set_environment(self._environment_cb)
        self.core.retro_set_video_refresh(self._video_cb)
        self.core.retro_set_audio_sample(self._audio_sample_cb)
        self.core.retro_set_audio_sample_batch(self._audio_batch_cb)
        self.core.retro_set_input_poll(self._input_poll_cb)
        self.core.retro_set_input_state(self._input_state_cb)
        version = int(self.core.retro_api_version())
        if version != RETRO_API_VERSION:
            raise RunnerError(f"libretro API mismatch: core={version}, frontend=1")

        system_info = RetroSystemInfo()
        self.core.retro_get_system_info(ctypes.byref(system_info))
        self.core.retro_init()
        self._initialized = True
        self.core.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD)

        game = RetroGameInfo(
            path=str(self.rom_path).encode(),
            data=ctypes.cast(self._rom_buffer, c_void_p),
            size=len(self._rom_bytes),
            meta=None,
        )
        if not self.core.retro_load_game(ctypes.byref(game)):
            raise RunnerError("retro_load_game returned false")
        self._loaded = True

        av_info = RetroSystemAVInfo()
        self.core.retro_get_system_av_info(ctypes.byref(av_info))
        return {
            "api_version": version,
            "library_name": _decode(system_info.library_name),
            "library_version": _decode(system_info.library_version),
            "valid_extensions": _decode(system_info.valid_extensions),
            "need_fullpath": bool(system_info.need_fullpath),
            "av": {
                "base_width": int(av_info.geometry.base_width),
                "base_height": int(av_info.geometry.base_height),
                "max_width": int(av_info.geometry.max_width),
                "max_height": int(av_info.geometry.max_height),
                "aspect_ratio": float(av_info.geometry.aspect_ratio),
                "fps": float(av_info.timing.fps),
                "sample_rate": float(av_info.timing.sample_rate),
            },
        }

    def load_state(self, path: Path) -> None:
        data = path.read_bytes()
        expected = int(self.core.retro_serialize_size())
        if len(data) != expected:
            raise RunnerError(
                f"save-state size mismatch: file={len(data)}, core={expected}"
            )
        buffer = ctypes.create_string_buffer(data)
        if not self.core.retro_unserialize(buffer, len(data)):
            raise RunnerError(f"retro_unserialize failed for {path}")

    def save_state(self, path: Path) -> dict[str, Any]:
        size = int(self.core.retro_serialize_size())
        if size <= 0:
            raise RunnerError("core reported zero save-state size")
        buffer = ctypes.create_string_buffer(size)
        if not self.core.retro_serialize(buffer, size):
            raise RunnerError("retro_serialize returned false")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(buffer.raw)
        return {"path": str(path.resolve()), "size": size, "sha256": _sha256(path)}

    def run_frame(self, buttons: set[int]) -> None:
        self.active_buttons = buttons
        self.core.retro_run()

    def frame_image(self) -> Image.Image:
        if self.last_frame is None:
            raise RunnerError("core has not produced a software video frame")
        size = (self.last_width, self.last_height)
        if self.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888:
            return Image.frombuffer(
                "RGB", size, self.last_frame, "raw", "BGRX", self.last_pitch, 1
            ).copy()
        if self.pixel_format == RETRO_PIXEL_FORMAT_RGB565:
            return Image.frombuffer(
                "RGB", size, self.last_frame, "raw", "BGR;16", self.last_pitch, 1
            ).copy()
        if self.pixel_format == RETRO_PIXEL_FORMAT_0RGB1555:
            return Image.frombuffer(
                "RGB", size, self.last_frame, "raw", "BGR;15", self.last_pitch, 1
            ).copy()
        raise RunnerError(f"unsupported pixel format {self.pixel_format}")

    def close(self) -> None:
        if self._loaded:
            self.core.retro_unload_game()
            self._loaded = False
        if self._initialized:
            self.core.retro_deinit()
            self._initialized = False

    def diagnostics(self) -> dict[str, Any]:
        return {
            "pixel_format": self.pixel_format,
            "video_callback_count": self.video_callback_count,
            "hardware_frame_count": self.hardware_frame_count,
            "dupe_frame_count": self.dupe_frame_count,
            "last_frame": {
                "width": self.last_width,
                "height": self.last_height,
                "pitch": self.last_pitch,
            },
            "options": {
                key: self._option_overrides.get(key, default)
                for key, default in sorted(self._option_defaults.items())
            },
            "input_descriptors": self.input_descriptors,
            "environment_commands": {
                str(key): value
                for key, value in sorted(self._environment_commands.items())
            },
            "unknown_environment_commands": {
                str(key): value
                for key, value in sorted(self._unknown_environment_commands.items())
            },
        }


def _load_script(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"events": [], "screenshots": []}
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RunnerError(f"cannot read input script {path}: {exc}") from exc
    if not isinstance(document, dict):
        raise RunnerError("input script must be a JSON object")
    if document.get("schema") != INPUT_SCRIPT_SCHEMA:
        raise RunnerError(
            f"input script schema must be {INPUT_SCRIPT_SCHEMA!r}"
        )
    events = document.get("events", [])
    screenshots = document.get("screenshots", [])
    if not isinstance(events, list) or not isinstance(screenshots, list):
        raise RunnerError("input script events/screenshots must be arrays")
    return document


def _compile_events(document: dict[str, Any], frame_count: int) -> dict[int, set[int]]:
    states: dict[int, set[int]] = {}
    for index, event in enumerate(document.get("events", []), start=1):
        if not isinstance(event, dict):
            raise RunnerError(f"event {index} must be an object")
        frame = int(event.get("frame", -1))
        duration = int(event.get("duration", 1))
        button_name = str(event.get("button", "")).lower()
        if frame < 0 or duration <= 0 or frame + duration > frame_count:
            raise RunnerError(f"event {index} has an invalid frame range")
        if button_name not in BUTTON_IDS:
            raise RunnerError(f"event {index} has unknown button {button_name!r}")
        button = BUTTON_IDS[button_name]
        for active_frame in range(frame, frame + duration):
            states.setdefault(active_frame, set()).add(button)
    return states


def _contact_sheet(paths: list[Path], destination: Path) -> None:
    if not paths:
        return
    images = [Image.open(path).convert("RGB") for path in paths]
    width = max(image.width for image in images)
    thumb_height = max(image.height for image in images)
    sheet = Image.new("RGB", (width, thumb_height * len(images)), "black")
    for index, image in enumerate(images):
        sheet.paste(image, (0, index * thumb_height))
    destination.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(destination)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="libretro_runner.py")
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--work-dir", type=Path, default=Path("build/libretro/runtime"))
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--script", type=Path)
    parser.add_argument("--load-state", type=Path)
    parser.add_argument("--save-state", type=Path)
    args = parser.parse_args(argv)

    if args.frames <= 0:
        parser.error("--frames must be positive")
    if not args.core.is_file() or not args.rom.is_file():
        parser.error("--core and --rom must point to files")

    try:
        script = _load_script(args.script)
        event_states = _compile_events(script, args.frames)
        screenshot_frames = sorted(
            {int(frame) for frame in script.get("screenshots", [])}
        )
        if any(frame < 0 or frame >= args.frames for frame in screenshot_frames):
            raise RunnerError("screenshot frame is outside the run")

        frontend = LibretroFrontend(
            args.core,
            args.rom,
            args.work_dir,
            option_overrides={
                "mupen64plus-rdp-plugin": "angrylion",
                "mupen64plus-rsp-plugin": "cxd4",
                "mupen64plus-cpucore": "dynamic_recompiler",
                "mupen64plus-angrylion-multithread": "all threads",
            },
        )
        started = time.monotonic()
        captures: list[dict[str, Any]] = []
        capture_paths: list[Path] = []
        try:
            core_info = frontend.initialize()
            if args.load_state:
                # Mupen64Plus-Next creates its graphics/plugin state lazily on
                # the first retro_run().  Loading a state immediately after
                # retro_load_game() therefore returns false even when the
                # serialized size and core/ROM pair are correct.
                frontend.run_frame(set())
                frontend.load_state(args.load_state)
            for frame in range(args.frames):
                frontend.run_frame(event_states.get(frame, set()))
                if frame in screenshot_frames:
                    if frontend.last_frame is None:
                        captures.append(
                            {
                                "frame": frame,
                                "status": "no-software-frame-yet",
                            }
                        )
                        continue
                    image = frontend.frame_image()
                    path = args.work_dir / "screenshots" / f"frame-{frame:06d}.png"
                    path.parent.mkdir(parents=True, exist_ok=True)
                    image.save(path)
                    capture_paths.append(path)
                    captures.append(
                        {
                            "frame": frame,
                            "status": "captured",
                            "path": str(path.resolve()),
                            "sha256": _sha256(path),
                            "size": [image.width, image.height],
                        }
                    )
                if frontend.shutdown_requested:
                    raise RunnerError(f"core requested shutdown at frame {frame}")
            state_report = frontend.save_state(args.save_state) if args.save_state else None
            diagnostics = frontend.diagnostics()
        finally:
            frontend.close()

        contact_sheet = args.work_dir / "contact-sheet.png"
        _contact_sheet(capture_paths, contact_sheet)
        elapsed = time.monotonic() - started
        report = {
            "schema": "srw64.libretro-run.v1",
            "status": "passed",
            "core": {
                "path": str(args.core.resolve()),
                "sha256": _sha256(args.core),
                **core_info,
            },
            "rom": {
                "path": str(args.rom.resolve()),
                "sha256": _sha256(args.rom),
                "size": args.rom.stat().st_size,
            },
            "run": {
                "frames": args.frames,
                "elapsed_seconds": round(elapsed, 3),
                "frames_per_wall_second": round(args.frames / elapsed, 3),
                "script": str(args.script.resolve()) if args.script else None,
                "script_sha256": _sha256(args.script) if args.script else None,
                "load_state": (
                    {
                        "path": str(args.load_state.resolve()),
                        "size": args.load_state.stat().st_size,
                        "sha256": _sha256(args.load_state),
                    }
                    if args.load_state
                    else None
                ),
                "captures": captures,
                "contact_sheet": str(contact_sheet.resolve()) if captures else None,
                "save_state": state_report,
            },
            "diagnostics": diagnostics,
        }
        report_path = args.work_dir / "run-report.json"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
        return 0
    except (RunnerError, OSError, ValueError) as exc:
        print(json.dumps({"status": "error", "error": str(exc)}, indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
