"""Select intact, completed SRAM copies without claiming game-slot validity.

The run report is the prior integrity record. Never bless an unrecorded file by
hashing it now, or alter a historical SRAM to repair it. Gameplay checksums and
safe-node serialization remain separate work.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re

SRAM_SIZE = 0x8000
SESSION_ID = re.compile(r"\d{8}T\d{6}\.\d{6}Z\Z")
COMPLETED_STATUSES = {"native-task-submissions-observed", "native-graphics-frames-captured",
                      "native-no-GPU-frame-captured", "native-graphics-run-completed",
                      "native-run-ended-by-control", "native-run-ended-before-VI-limit"}


class SaveHistoryError(ValueError):
    """No acceptable source, or a selected source changed before staging."""


@dataclass(frozen=True)
class SaveCandidate:
    session: str
    path: Path
    accepted: bool
    reason: str
    sha256: str | None = None
    record_sha256: str | None = None

    def summary(self) -> dict:
        return {"session": self.session, "path": str(self.path),
                "integrity_verified": self.accepted, "reason": self.reason,
                "sha256": self.sha256, "record_sha256": self.record_sha256,
                "game_slots_verified": False}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_bytes(path: Path, expected: str | None) -> bytes:
    if not isinstance(expected, str) or re.fullmatch(r"[0-9a-f]{64}", expected) is None:
        raise SaveHistoryError("缺少有效的既有存档摘要")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SaveHistoryError(f"无法读取存档：{error}") from error
    if len(data) != SRAM_SIZE:
        raise SaveHistoryError(f"存档长度异常：{len(data)}，应为 {SRAM_SIZE}")
    if digest(data) != expected:
        raise SaveHistoryError("存档摘要与既有记录不一致")
    if data in (bytes(SRAM_SIZE), b"\xff" * SRAM_SIZE):
        raise SaveHistoryError("空白 SRAM，不能作为继续游戏的来源")
    return data


def inspect_session(session: Path, *, game_id: str, variant: str, rom_sha256: str) -> SaveCandidate:
    path = session / "runtime-data/saves" / f"{game_id}.bin"
    record_hash = None
    try:
        if session.is_symlink() or path.is_symlink() or not path.resolve().is_relative_to(session.resolve()):
            raise SaveHistoryError("存档路径超出会话目录或使用符号链接")
        report_data = (session / "report.json").read_bytes()
        record_hash = digest(report_data)
        report = json.loads(report_data)
        if not isinstance(report, dict) or report.get("schema") != "srw64.recomp-native-host-probe.v1":
            raise SaveHistoryError("缺少可识别的运行报告")
        if type(report.get("exit_code")) is not int or report["exit_code"] != 0:
            raise SaveHistoryError("会话未正常完成，需单独检查")
        if report.get("status") not in COMPLETED_STATUSES:
            raise SaveHistoryError("运行报告尚未完成或记录失败")
        if report.get("rom_variant") != variant or report.get("rom_sha256") != rom_sha256:
            raise SaveHistoryError("运行报告的 ROM 身份不匹配")
        if report.get("comparison_fixture") or report.get("script_move_probe"):
            raise SaveHistoryError("诊断实验修改过游戏状态，不作为正常存档恢复来源")
        profile = report.get("profile")
        if profile is not None:
            if not isinstance(profile, dict) or profile.get("profile", {}).get("gameplay_mods") != []:
                raise SaveHistoryError("存档玩法配置不受当前原版恢复流程支持")
        saved = report.get("final_save")
        if not isinstance(saved, dict) or saved.get("size") != SRAM_SIZE:
            raise SaveHistoryError("缺少完整的最终 SRAM 记录")
        data = checked_bytes(path, saved.get("sha256"))
        return SaveCandidate(session.name, path, True, "正常会话，文件大小及既有摘要一致", digest(data), record_hash)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        return SaveCandidate(session.name, path, False, str(error), record_sha256=record_hash)


def inventory(sessions: Path, *, game_id: str, variant: str, rom_sha256: str) -> list[SaveCandidate]:
    # Content compilation directories and staged input files are not sessions.
    return [inspect_session(path, game_id=game_id, variant=variant, rom_sha256=rom_sha256)
            for path in sorted(sessions.iterdir(), reverse=True)
            if SESSION_ID.fullmatch(path.name) and path.is_dir()] if sessions.exists() else []


def inspect_initial(path: Path, expected: str) -> SaveCandidate:
    try:
        data = checked_bytes(path, expected)
        return SaveCandidate("initial", path, True, "冻结初始备份摘要一致", digest(data))
    except SaveHistoryError as error:
        return SaveCandidate("initial", path, False, str(error))


def select(candidates: list[SaveCandidate], initial: SaveCandidate,
           requested_session: str | None = None) -> tuple[SaveCandidate, list[SaveCandidate]]:
    if requested_session is not None:
        if requested_session == "initial":
            chosen = initial
        else:
            if SESSION_ID.fullmatch(requested_session) is None:
                raise SaveHistoryError("恢复会话 ID 格式不正确")
            chosen = next((row for row in candidates if row.session == requested_session), None)
            if chosen is None:
                raise SaveHistoryError("指定会话不存在；可用 --list-saves 查看")
        if not chosen.accepted:
            raise SaveHistoryError(f"指定存档不能读取：{chosen.reason}；不会自动改选其他进度")
        return chosen, []
    skipped = []
    for candidate in candidates:
        if candidate.accepted:
            return candidate, skipped
        skipped.append(candidate)
    if initial.accepted:
        return initial, skipped
    raise SaveHistoryError("历史会话和冻结备份均无可核验副本；使用 --list-saves 检查，或显式 --new-game")


def stage_selection(output: Path, chosen: SaveCandidate, skipped: list[SaveCandidate]) -> Path:
    """Freeze the already checked bytes once, before building/launching a host.

    These files sit beside the future run directory, which the probe must create
    itself. Exclusive creation preserves previous attempts and history.
    """
    data = checked_bytes(chosen.path, chosen.sha256)
    staged = output.parent / (output.name + ".source.sram")
    with staged.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    record = {"schema": "srw64.save-selection.v1", "selected": chosen.summary(),
              "skipped": [row.summary() for row in skipped], "staged_path": str(staged),
              "staged_sha256": digest(data), "game_slots_verified": False}
    record_path = output.parent / (output.name + ".save-selection.json")
    with record_path.open("x") as stream:
        json.dump(record, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    return staged
