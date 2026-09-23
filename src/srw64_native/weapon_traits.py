"""Separate printed weapon markers from names, without guessing numeric flags."""

MARKERS = {"格": "格斗武器", "射": "射击武器", "P": "可移动后使用",
           "B": "光束武器", "MAP": "地图武器"}
SUFFIXES = (("", []), ("P", ["P"]), ("B", ["B"]), ("PB", ["P", "B"]),
            ("MAP", ["MAP"]), ("BMAP", ["B", "MAP"]), ("PBMAP", ["P", "B", "MAP"]))


def split_menu(name: str, menu: str) -> tuple[str, str, str, bool] | None:
    """(prefix, display, suffix, map_moved) with menu == prefix + display + suffix.

    Some base-name entries already end in MAP; the menu inserts B before MAP, so
    display is the name without MAP (map_moved). Only strip that suffix when both
    original strings agree on this grammar."""
    candidates = [(name, False)]
    if name.endswith("MAP"):
        candidates.insert(0, (name[:-3], True))
    for display, requires_map in candidates:
        for prefix in ("格", "射", ""):
            for suffix, tokens in SUFFIXES:
                if requires_map and "MAP" not in tokens:
                    continue
                if display and menu == prefix + display + suffix:
                    return prefix, display, suffix, requires_map
    return None


def weapon_traits(name: str, menu: str) -> dict:
    split = split_menu(name, menu)
    if split is None:
        return {"display_name": name, "markers": [], "source": "original-menu-text", "parsed": False}
    prefix, display, suffix, _ = split
    badges = ([prefix] if prefix else []) + dict(SUFFIXES)[suffix]
    return {"display_name": display,
            "markers": [{"token": t, "label": MARKERS[t]} for t in badges],
            "source": "original-menu-text", "parsed": True}
