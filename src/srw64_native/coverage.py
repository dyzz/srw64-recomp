"""Count validated translation records without equating fallback with coverage."""
from __future__ import annotations

from collections import defaultdict


def locale_coverage(document: dict, sources: dict, compiled: dict, ui_keys: set[str]) -> dict:
    rows = {row['key']: row for row in document['entries']}
    tables: dict[str, dict] = defaultdict(lambda: {'source_records': 0, 'translated': 0, 'reviewed': 0, 'draft': 0, 'fallback': 0})
    missing = []
    for key in sources:
        table = tables[key.split('_')[0]]
        table['source_records'] += 1
        if key in compiled:
            table['translated'] += 1
            table['reviewed' if rows[key].get('review_status', 'draft') == 'reviewed' else 'draft'] += 1
        elif document['locale'] != 'ja':
            table['fallback'] += 1
            missing.append(key)
    return {'schema': 'srw64.locale-coverage.v1', 'locale': document['locale'],
            'source_records': len(sources), 'translated_records': len(compiled),
            'reviewed_records': sum(row['reviewed'] for row in tables.values()),
            'draft_records': sum(row['draft'] for row in tables.values()),
            'fallback_records': len(missing), 'fallback_keys': missing, 'tables': dict(sorted(tables.items())),
            'ui': {'required': len(ui_keys), 'provided': len(ui_keys & document['ui'].keys()),
                   'fallback_keys': sorted(ui_keys - document['ui'].keys())},
            'consumers': {'standard_script_dialogue': 'adapted; runtime requires matching standard box and speaker',
                          'native_reading_ui': 'adapted', 'native_name_ui_labels': 'adapted',
                          'native_language_settings': 'adapted', 'original_menus': 'not-adapted',
                          'battle_labels': 'not-adapted', 'baked_intro_text': 'not-adapted',
                          'default_character_names': 'original-game-values'},
            'denominator': 'All extracted text records; does not prove every record has an adapted rendering path.'}
