#!/usr/bin/env python3
"""Join opt-in native script polling observations to the extracted ROM catalog.

Only uniquely matched ordinary commands are reported. Internal condition scans
are deliberately not reconstructed as executed instructions.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def analyze(run, catalog):
    layout = json.loads((ROOT / 'config/data/original-jp-v1.json').read_text())['stage_scripts']
    records = {}
    for path in (catalog / 'details/stage_events').glob('*.json'):
        records.update(json.loads(path.read_text()))
    instructions = {}
    for path in (catalog / 'details/scenarios').glob('*.json'):
        for record in json.loads(path.read_text()).values():
            scene = record['scenario']['scene_index']
            instructions[scene] = {}
            for event in record['scenario']['events']:
                base = int(event['runtime_entry_vram'], 16)
                for command in records[event['key']]['script']['instructions']:
                    if command['kind'] not in ('command', 'dialogue'):
                        continue
                    instructions[scene][base + command['offset']] = {
                        **command, 'event': event['key'], 'pc': base + command['offset'],
                        'handler': int(layout['commands'][f"{command['opcode']:04x}"]['handler_vram'] or '0', 16)}
    log = run.with_suffix('.native.log')
    rows, errors = [], []
    prefix = 'SRW64_SCRIPT_TRACE '
    for number, line in enumerate(log.read_text(errors='replace').splitlines(), 1):
        if prefix not in line:
            continue
        try:
            rows.append(json.loads(line.split(prefix, 1)[1]))
        except json.JSONDecodeError:
            errors.append(number)
    spans, pending, mismatches, unmatched = [], {}, [], []
    for row in rows:
        before, after = row['before'], row['after']
        owner = row['owner']
        if before['state'] == 0:
            candidates = []
            for pc, cmd in instructions.get(before['scene'], {}).items():
                if not before['pc'] <= pc < after['pc'] or cmd['handler'] != after['handler']:
                    continue
                expected = pc + (len(cmd['operands']) + 1) * 2 if after['state'] == 0 else pc + 2
                if after['pc'] == expected:
                    candidates.append(cmd)
            if len(candidates) != 1:
                if after['handler'] and after['state'] != 128:
                    unmatched.append({'vi': row['vi'], 'before_pc': before['pc'], 'after_pc': after['pc'], 'candidates': len(candidates)})
                continue
            cmd = candidates[0]
            offset = (cmd['pc'] - before['pc']) // 2
            expected = [cmd['opcode'], *cmd['operands']]
            observed = before['words'][offset:offset + len(expected)]
            # Eight-word windows can be exhausted by a condition scan. Do not
            # call truncated observations a complete operand verification.
            verified = len(observed) == len(expected) and observed == expected
            if observed and observed != expected[:len(observed)]:
                mismatches.append({'vi': row['vi'], 'expected': expected, 'observed': observed})
            span = {'scene': before['scene'], 'event': cmd['event'], 'offset': cmd['offset'],
                    'opcode': f"{cmd['opcode']:04x}", 'name': cmd['name'], 'operands': cmd['operands'],
                    'start_vi': row['vi'], 'end_vi': None, 'operand_bytes_verified': verified,
                    'semantic_confidence': layout['commands'][f"{cmd['opcode']:04x}"]['semantic_confidence'],
                    'pc': cmd['pc'], 'end_pc': cmd['pc'] + len(expected) * 2}
            spans.append(span)
            pending[owner] = span
        if after['state'] == 0 and owner in pending:
            span = pending.pop(owner)
            if after['pc'] == span['end_pc']:
                span['end_vi'] = row['vi']
                span['elapsed_vis'] = row['vi'] - span['start_vi']
            else:
                mismatches.append({'vi': row['vi'], 'expected_end_pc': span['end_pc'], 'actual': after['pc']})
    report = {'schema': 'srw64.script-observation.v1', 'log': str(log), 'log_sha256': digest(log),
              'catalog_manifest_sha256': digest(catalog / 'manifest.json'),
              'poll_transitions': len(rows), 'malformed_log_lines': errors, 'unmatched_polls': unmatched,
              'byte_or_pc_mismatches': mismatches, 'command_executions': len(spans),
              'completed_commands': sum(s['end_vi'] is not None for s in spans),
              'opcode_counts': dict(sorted(Counter(s['opcode'] for s in spans).items())),
              'scope': 'Observed ordinary command polling boundaries; internal conditions, unit effects and causality are not proven by this join.',
              'commands': spans}
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--catalog', type=Path, default=ROOT / 'assets/original-data')
    args = parser.parse_args()
    report = analyze(args.run.resolve(), args.catalog.resolve())
    target = args.run / 'script-observation.json'
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'commands'}, ensure_ascii=False, indent=2))
    return bool(report['malformed_log_lines'] or report['byte_or_pc_mismatches'] or report['unmatched_polls'])

if __name__ == '__main__':
    raise SystemExit(main())
