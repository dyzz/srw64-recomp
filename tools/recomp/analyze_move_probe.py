#!/usr/bin/env python3
"""Accept only the fixed scene-0 3D3C 1912 -> 1911 experiment against a fresh baseline."""
import argparse
import json
from pathlib import Path

from analyze_script_trace import ROOT, analyze, digest


def checkpoint(rows, phase):
    matches = [r for r in rows if r['phase'] == phase]
    if len(matches) != 1:
        raise ValueError(f'Expected one {phase} checkpoint, found {len(matches)}')
    return matches[0]


def actor(row, identity):
    matches = [u for u in row['units'] if u['actor'] == identity]
    if len(matches) != 1:
        raise ValueError(f'Expected one actor {identity}, found {len(matches)}')
    return matches[0]


def occupancy(row, x, y):
    # Derived from active roster slots, not a call into the game's collision code.
    return [(u['side'], u['slot'], u['actor']) for u in row['units'] if (u['x'], u['y']) == (x, y)]


def compare(baseline, changed, catalog):
    reports, traces, polls = [], [], []
    for run in (baseline, changed):
        reports.append(json.loads((run / 'report.json').read_text()))
        trace = analyze(run, catalog)
        (run / 'script-observation.json').write_text(json.dumps(trace, ensure_ascii=False, indent=2) + '\n')
        traces.append(trace)
        prefix = 'SRW64_MOVE_PROBE '
        polls.append([json.loads(line[len(prefix):]) for line in run.with_suffix('.native.log').read_text().splitlines()
                      if line.startswith(prefix)])
    b, c = reports
    bt, ct = traces
    bp, cp = polls
    bs, cs = checkpoint(bp, 'before'), checkpoint(cp, 'before')
    be, ce = checkpoint(bp, 'complete'), checkpoint(cp, 'complete')
    bf, cf = checkpoint(bp, 'opening-end'), checkpoint(cp, 'opening-end')
    ca, cr = checkpoint(cp, 'armed'), checkpoint(cp, 'restored')
    bm = next(s for s in bt['commands'] if s['offset'] == 0xC8)
    cm = next(s for s in ct['commands'] if s['offset'] == 0xC8)
    checks = {
        'same_binary': b['binary_sha256'] == c['binary_sha256'],
        'same_sources': b['source_sha256'] == c['source_sha256'],
        'same_presentation': b['profile']['profile'] == c['profile']['profile'] and
                             b['profile']['profile']['presentation']['images'] == 'original' and
                             b['profile']['profile']['presentation']['locale'] == 'ja' and
                             not b['profile']['profile']['gameplay_mods'],
        'same_original_rom': b['rom_sha256'] == c['rom_sha256'] == 'ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e',
        'same_input': b['input']['script_sha256'] == c['input']['script_sha256'],
        'muted_empty_sram': all(r['audio_output_enabled'] is False and r['initial_save'] is None for r in reports),
        'normal_exit': all(r['exit_code'] == 0 for r in reports),
        'expected_modes': b['script_move_probe'] == 'baseline' and c['script_move_probe'] == 'target17',
        'valid_logs_and_pc': all(not t['malformed_log_lines'] and not t['unmatched_polls'] for t in traces),
        'baseline_matches_rom': not bt['byte_or_pc_mismatches'] and all(s['operand_bytes_verified'] for s in bt['commands']),
        'one_expected_parameter_difference': ct['byte_or_pc_mismatches'] == [
            {'vi': cm['start_vi'], 'expected': [0x3D3C, 27, 0x1912], 'observed': [0x3D3C, 27, 0x1911]}],
        'all_other_parameters_verified': all(s['operand_bytes_verified'] for s in ct['commands'] if s is not cm),
        'same_56_commands': len(bt['commands']) == len(ct['commands']) == 56 and
            [(s['event'], s['offset'], s['opcode']) for s in bt['commands']] ==
            [(s['event'], s['offset'], s['opcode']) for s in ct['commands']],
        'all_completed_scene0': all(s['end_vi'] is not None and s['scene'] == 0 and
                                   s['event'] == 'base:stage_events:0019bf10' for t in traces for s in t['commands']),
        'no_probe_rejection': not any(r['phase'] in ('identity-rejected', 'restore-conflict') for rows in polls for r in rows),
        'same_initial_roster_and_sprites': bs['units'] == cs['units'],
        'parameter_restored': bs['parameter'] == cs['parameter'] == cr['parameter'] == cf['parameter'] == 0x1912 and
                              ca['parameter'] == ce['parameter'] == 0x1911,
    }
    for label, left, right in [('completion', be, ce), ('opening_end', bf, cf)]:
        expected = [dict(u) for u in left['units']]
        unit = next(u for u in expected if u['actor'] == 27)
        unit['y'] -= 1
        unit['sprite_y'] -= 16
        checks[label + '_only_actor27_moves_one_cell'] = right['units'] == expected
        checks[label + '_baseline_destination'] = (actor(left, 27)['x'], actor(left, 27)['y']) == (25, 18)
        checks[label + '_changed_destination'] = (actor(right, 27)['x'], actor(right, 27)['y']) == (25, 17)
        checks[label + '_roster_occupancy'] = (not occupancy(left, 25, 28) and not occupancy(right, 25, 28) and
                                             not occupancy(right, 25, 18) and len(occupancy(right, 25, 17)) == 1)
    return {
        'schema': 'srw64.move-comparison.v1', 'passed': all(checks.values()), 'checks': checks,
        'runs': [{'path': str(run), 'report_sha256': digest(run / 'report.json'),
                  'log_sha256': digest(run.with_suffix('.native.log'))} for run in (baseline, changed)],
        'baseline': {'move': bm, 'before': actor(bs, 27), 'complete': actor(be, 27), 'opening_end': actor(bf, 27)},
        'changed': {'move': cm, 'observed_operands': [27, 0x1911], 'before': actor(cs, 27),
                    'complete': actor(ce, 27), 'opening_end': actor(cf, 27)},
        'scope': 'One isolated absolute-coordinate movement. Occupancy is derived from active roster coordinates; no collision API, battle, save/reload or relative-position acceptance.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('changed', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = compare(args.baseline.resolve(), args.changed.resolve(), ROOT / 'build/original-data')
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'checks': report['checks']}, indent=2))
    return not report['passed']


if __name__ == '__main__':
    raise SystemExit(main())
