#!/usr/bin/env python3
"""The ICU items the Vita build leaves out of its data (build_icu.sh): reads `icupkg -l`
output on stdin, writes the items to remove. Kept: the break rules and the dictionaries
they name, the character properties, normalization, aliases and metadata, and root,
English, Japanese and Chinese for what is locale-specific (33 MB down to 5.3 MB)."""
import re
import sys

LOCALES = re.compile(r"^(root|en|ja|zh)(_[A-Za-z0-9_]+)?\.res$")
DROP_TREES = ("coll/", "curr/", "lang/", "region/", "rbnf/", "translit/", "unit/", "zone/")
DROP_FILES = re.compile(r"(\.cnv$|^confusables|^unames\.icu$|\.spp$)")

for line in sys.stdin:
    item = line.strip()
    if not item:
        continue
    name = item.split("/")[-1]
    if item.startswith(DROP_TREES) or DROP_FILES.search(name):
        print(item)
    elif "/" not in item and name.endswith(".res") and not LOCALES.match(name) and not name.startswith(("pool", "res_index", "supplementalData", "metadata", "likelySubtags", "keyTypeData", "langInfo", "icuver", "icustd", "numberingSystems", "plurals", "dayPeriods", "grammaticalFeatures", "genderList", "units", "timezoneTypes", "windowsZones", "zoneinfo64", "metaZones", "tzdbNames", "cnvalias", "currencyNumericCodes")):
        print(item)
    elif item.startswith("brkitr/") and name.endswith(".res") and not LOCALES.match(name):
        print(item)
