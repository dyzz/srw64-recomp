#!/usr/bin/env python3
"""Read existing live task dumps without writing to game memory or inputs."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time


def inspect(ram: bytes, task: bytes, resource: bytes) -> dict:
    if len(ram) != 0x800000 or len(task) != 64:
        raise ValueError('Incomplete task dump')
    words = struct.unpack('<16I', task)
    start, size = words[12] & 0x1FFFFFFF, words[13]
    if words[0] != 1 or size % 8 or start+size > len(ram):
        raise ValueError('Invalid root task')
    base = ram.find(resource)
    result = {'resident': base >= 0, 'root_calls': [], 'root': start, 'root_size': size}
    if base < 0:
        return result
    result['resource_address'] = base
    entry = struct.unpack_from('>I', resource, 20)[0]
    segments = [0]*16
    matrix = None
    def physical(address):
        return segments[(address >> 24) & 15]+(address & 0xFFFFFF)
    for p in range(start, start+size, 8):
        a, b = struct.unpack_from('>II', ram, p)
        if a >> 24 == 0xDB and (a >> 16) & 255 == 6:
            index = (a & 0xFFFF)//4
            if index < 16:
                segments[index] = b & 0x1FFFFFFF
        elif a >> 24 == 0xDA:
            address = physical(b)
            if address+64 <= len(ram):
                matrix = {'address': address, 'bytes_hex': ram[address:address+64].hex()}
        elif a >> 24 == 0xDE and physical(b) == base+entry and segments[4] == base:
            result['root_calls'].append({'command_address': p, 'entry': base+entry, 'segment4': base, 'modelview': matrix})
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--resource', type=Path, required=True)
    parser.add_argument('--max-seconds', type=int, default=600)
    args = parser.parse_args()
    resource = args.resource.read_bytes()
    root = args.directory.resolve()
    started = time.monotonic()
    previous = None
    records = []
    saved = 0
    last_saved_vi = -1000
    while time.monotonic()-started < args.max_seconds:
        ram_path, task_path = root/'latest-gfx-rdram.bin', root/'latest-gfx-task.bin'
        try:
            signature = (ram_path.stat().st_mtime_ns, task_path.stat().st_mtime_ns)
            if signature != previous:
                task = task_path.read_bytes()
                ram = ram_path.read_bytes()
                if signature != (ram_path.stat().st_mtime_ns, task_path.stat().st_mtime_ns):
                    continue
                observation = inspect(ram, task, resource)
                state = json.loads((root/'live-state.json').read_text())
                observation['observed_vi'] = state['vi']
                observation['rdram_sha256'] = hashlib.sha256(ram).hexdigest()
                observation['task_sha256'] = hashlib.sha256(task).hexdigest()
                if observation['root_calls'] and saved < 8 and state['vi']-last_saved_vi >= 180:
                    directory = root/'model-samples'/f'sample-{saved:02d}'
                    directory.mkdir(parents=True, exist_ok=False)
                    (directory/'latest-gfx-rdram.bin').write_bytes(ram)
                    (directory/'latest-gfx-task.bin').write_bytes(task)
                    observation['snapshot'] = str(directory.relative_to(root))
                    saved += 1
                    last_saved_vi = state['vi']
                records.append(observation)
                with (root/'model-5600-observations.jsonl').open('a') as out:
                    out.write(json.dumps(observation)+'\n')
                previous = signature
        except (FileNotFoundError, ValueError, json.JSONDecodeError):
            pass
        if (root/'report.json').exists():
            break
        time.sleep(.5)
    calls = [r for r in records if r['root_calls']]
    matrices = {c['modelview']['bytes_hex'] for r in calls for c in r['root_calls'] if c['modelview']}
    summary = {'schema': 'srw64.model-5600-live-observations.v1',
               'scope': 'asynchronously observed existing task dumps; observed_vi is not a GPU-frame synchronization claim',
               'resource_sha256': hashlib.sha256(resource).hexdigest(), 'resource_bytes': len(resource),
               'samples': len(records), 'samples_with_verified_root_call': len(calls),
               'resource_addresses': sorted({r['resource_address'] for r in calls}),
               'distinct_modelview_matrices': len(matrices), 'saved_task_samples': saved,
               'first_call_observed_vi': calls[0]['observed_vi'] if calls else None,
               'last_call_observed_vi': calls[-1]['observed_vi'] if calls else None}
    root.mkdir(parents=True, exist_ok=True)
    (root/'model-5600-runtime.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
