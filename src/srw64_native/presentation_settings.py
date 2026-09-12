"""Persistent display preferences, separate from game identity and save data."""
from __future__ import annotations

import json
from pathlib import Path


def selected_locale(path: Path, registered: dict, default: str, override: str | None = None) -> str:
    if override is not None:
        if override not in registered:
            raise ValueError(f'Locale is not registered: {override}')
        return override
    if not path.exists():
        return default
    data = json.loads(path.read_text())
    if (not isinstance(data, dict) or data.get('schema') != 'srw64.presentation-settings.v1'
            or not isinstance(data.get('locale'), str) or data['locale'] not in registered):
        raise ValueError(f'Invalid or unavailable saved locale in {path}; use --language to select a registered locale')
    return data['locale']
