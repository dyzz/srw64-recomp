#!/usr/bin/env python3
"""Our save files in emulators and theirs in our library (docs/design/save-slots-autosave.md S5).

No game window: the graphics host is used only for --export-save and --import-save.

RetroArch (the pinned mupen64plus-next core, build/libretro/cores): the card exported as
.srm goes into the core's save memory; scripted input loads slot 1 from the title,
saves it into slot 2 from データセーブ; the core's save memory, written out as .srm, is
imported, and slot 2 must be the core's copy of slot 1 with its checksum right.

ares (/Applications/ares.app, with its GDB server): the card as save.ram is read at boot
(8009171C would clear a card without SRW64V3), so the seen bitmaps at 8010F4D0 must
come back from ares's memory as they are in the card; the same card word-swapped is
the control, read as no card. A blank card that the game formats in ares, written by
ares's own memory autosave, imports as it is."""
import ctypes
import json
from pathlib import Path
import os
import shutil
import signal
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.probes import ares_runner
from recomp.probes.ares_rsp_probe import AresRSPClient
from recomp.probes.libretro_runner import BUTTON_IDS, LibretroFrontend
from recomp.toolchain.audit_rom_variant import load_variant

ROOT = Path(__file__).resolve().parents[3]
HOST = ROOT / 'build/recomp/gfx-build/srw64-gfx-host'
CARD = ROOT / 'build/recomp/save-recovery-check/intermission-cold-1.source.sram'
CORE = ROOT / 'build/libretro/cores/mupen64plus_next_libretro.dylib'
SLOT, SEEN = 0x1F00, (0x78F0, 0xA0)
work = ROOT / 'build/recomp/save-slots-check' / time.strftime('interop-%Y%m%dT%H%M%S')
work.mkdir(parents=True)
card = CARD.read_bytes()
checks = []


def check(name, passed, state=None):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (work / 'interop-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    assert passed, (name, state)
    print(name, 'PASS', flush=True)


def tool(*args):
    result = subprocess.run([str(HOST), '--play', *map(str, args)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-800:])
    return result.stderr.strip().splitlines()[-1]


def library(name, content=None):
    user = work / name
    (user / 'saves').mkdir(parents=True)
    if content is not None:
        (user / 'saves/cartridge.sram').write_bytes(content)
    return user


def slot(data, index):
    return data[0x10 + index * SLOT:0x10 + (index + 1) * SLOT]


def intact(record):
    return bool(record[0] & 0x80) and record[1] == (sum(record[2:] + b'\0\0') & 0xFF)


def header(record):
    return {'turns': int.from_bytes(record[0x4C:0x4E], 'big'), 'episode': record[0x4F], 'title': record[0x51],
            'funds': int.from_bytes(record[0x54:0x58], 'big'), 'names': record[0x98:0xA4].hex()}


# --- RetroArch: mupen64plus-next ------------------------------------------------------
user = library('retroarch', card)
srm = work / 'game.srm'
check('export-srm', tool('--export-save', srm, '--user-dir', user).endswith('game.srm') and srm.stat().st_size == 0x48800, {})
# Libretro frames: past the intro and the title, the ring to ロード, ROMカートリッジ,
# slot 1, はい; in the intermission データセーブ, ROMカートリッジ, down to slot 2, save.
presses = [(120, 'start'), (678, 'start'), (1158, 'right'), (1734, 'right'), (2300, 'right'), (2900, 'n64_a'),
           (3200, 'n64_a'), (3500, 'n64_a'), (3800, 'n64_a'), (5600, 'n64_a'), (5900, 'n64_a'), (6200, 'down'), (6400, 'n64_a')]
shots = {3400: 'retroarch-load.png', 4400: 'retroarch-intermission.png', 7100: 'retroarch-saved.png'}
rom, variant, _ = load_variant('jp')
frontend = LibretroFrontend(CORE, rom, work / 'retroarch-core', {
    'mupen64plus-rdp-plugin': 'angrylion', 'mupen64plus-rsp-plugin': 'cxd4',
    'mupen64plus-cpucore': 'dynamic_recompiler', 'mupen64plus-angrylion-multithread': 'all threads'})
try:
    frontend.initialize()
    frontend.core.retro_get_memory_size.argtypes = [ctypes.c_uint]
    frontend.core.retro_get_memory_size.restype = ctypes.c_size_t
    frontend.core.retro_get_memory_data.argtypes = [ctypes.c_uint]
    frontend.core.retro_get_memory_data.restype = ctypes.c_void_p
    size, pointer = frontend.core.retro_get_memory_size(0), frontend.core.retro_get_memory_data(0)
    check('core-save-memory', size == 0x48800 and pointer, {'size': size})
    ctypes.memmove(pointer, srm.read_bytes(), size)   # what RetroArch does with a .srm
    held = {}
    for frame, button in presses:
        for n in range(6):
            held.setdefault(frame + n, set()).add(BUTTON_IDS[button])
    for frame in range(7200):
        frontend.run_frame(held.get(frame, set()))
        if frame in shots:
            frontend.frame_image().save(work / shots[frame])
    (work / 'core.srm').write_bytes(ctypes.string_at(pointer, size))
finally:
    frontend.close()
back = library('retroarch-back', card)
line = tool('--import-save', work / 'core.srm', '--user-dir', back)
imported = (back / 'saves/cartridge.sram').read_bytes()
check('import-srm', 'retroarch' in line and imported[:7] == b'SRW64V3', {'tool': line})
check('slot-2-from-the-core', intact(slot(imported, 1)) and header(slot(imported, 1)) == header(slot(card, 0)) and slot(imported, 0) == slot(card, 0),
      {'slot2': header(slot(imported, 1)), 'card-slot1': header(slot(card, 0))})


# --- ares -------------------------------------------------------------------------------
def ares(name, save, seconds=12.0, then=0.0):
    folder = work / name
    folder.mkdir()
    rom_copy = folder / 'srw64.z64'
    shutil.copyfile(ROOT / 'rom.z64', rom_copy)
    if save is not None:
        (folder / 'srw64.ram').write_bytes(save)
    command = [c.replace('General/AutoSaveMemory=false', 'General/AutoSaveMemory=true')
               for c in ares_runner._command(rom_copy, folder, 9131, False)]
    process = subprocess.Popen(command, cwd=folder, stdout=open(folder / 'ares.log', 'wb'), stderr=subprocess.STDOUT, start_new_session=True)
    try:
        end = time.monotonic() + 20
        while not ares_runner._port_open('::1', 9131, process.pid):
            if time.monotonic() > end:
                raise RuntimeError('ares did not open its GDB port')
            time.sleep(.2)
        time.sleep(seconds)
        client = AresRSPClient('::1', 9131, 8.0)
        client.connect()
        client.handshake()
        client.halt()
        seen, _ = client.memory(0x8010F4D0, SEEN[1])
        client.resume()
        client.close()
        time.sleep(then)   # ares writes save memory every 30 s with AutoSaveMemory
    finally:
        os.kill(process.pid, signal.SIGTERM)
        process.wait(10)
    written = folder / 'srw64.ram'
    return bytes.fromhex(seen), written.read_bytes() if written.exists() else None


seen, _ = ares('ares-read', card)
check('ares-reads-the-card', seen == card[SEEN[0]:SEEN[0] + SEEN[1]] and any(seen), {'seen': seen[:16].hex()})
swapped = b''.join(card[i:i + 4][::-1] for i in range(0, len(card), 4))
seen, _ = ares('ares-control', swapped)
check('ares-control-swapped', not any(seen), {'seen': seen[:16].hex()})
_, written = ares('ares-write', None, then=35)
fresh = library('ares-back')
line = tool('--import-save', work / 'ares-write/srw64.ram', '--user-dir', fresh)
check('ares-format-imports', written is not None and written[:7] == b'SRW64V3' and 'big-endian' in line
      and (fresh / 'saves/cartridge.sram').read_bytes() == written, {'tool': line})
print('ALL', len(checks), 'PASS', flush=True)
