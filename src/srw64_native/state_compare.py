"""Compare matching semantic observations without ignoring unexplained bytes."""
from __future__ import annotations


def compare_observations(left: dict, right: dict) -> dict:
    if left.get('schema') != 'srw64.game-state-observation.v1' or right.get('schema') != left['schema']:
        raise ValueError('Unsupported state observation')
    if (left.get('boundary'), left.get('argument')) != (right.get('boundary'), right.get('argument')):
        raise ValueError('Observations are not at the same semantic boundary')
    if not left.get('regions') or set(left['regions']) != set(right.get('regions', {})):
        raise ValueError('State coverage differs or is empty')
    differences = []
    for name, a in left['regions'].items():
        b = right['regions'][name]
        if (a['address'], a['size']) != (b['address'], b['size']):
            raise ValueError(f'State region layout differs: {name}')
        old, new = bytes.fromhex(a['bytes']), bytes.fromhex(b['bytes'])
        if len(old) != a['size'] or len(new) != a['size']:
            raise ValueError(f'Truncated state region: {name}')
        changed = [i for i, (x, y) in enumerate(zip(old, new)) if x != y]
        if changed:
            differences.append({'region': name, 'changed_bytes': len(changed),
                                'first_differences': [{'address': a['address'] + i, 'left': old[i], 'right': new[i]} for i in changed[:32]]})
    complete = left.get('coverage_complete') is True and right.get('coverage_complete') is True
    return {'schema': 'srw64.state-comparison.v1', 'boundary': left['boundary'], 'argument': left['argument'],
            'left_vi': left['vi'], 'right_vi': right['vi'], 'differences': differences,
            'observed_regions_equal': not differences, 'coverage_complete': complete,
            'equivalence_verified': complete and not differences,
            'status': 'different' if differences else 'equivalent' if complete else 'equal-observed-regions-only'}
