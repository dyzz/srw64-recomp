#!/usr/bin/env python3
"""Compare two game-thread state observations at the same semantic boundary."""
import argparse
import hashlib
import json
from pathlib import Path

from srw64_native.state_compare import compare_observations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('left', type=Path)
    parser.add_argument('right', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = compare_observations(json.loads(args.left.read_text()), json.loads(args.right.read_text()))
    result['sources'] = [{'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in (args.left, args.right)]
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(result['status'])
    return 0 if result['equivalence_verified'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
