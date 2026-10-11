# Brand masters

The project's marks at full size with transparent backgrounds, the sources the
packaging scripts draw from:

- `m64.webp` (1254×1254): the M64 badge. `tools/release/app_icons.py` makes the
  Windows, macOS and Android icons from it; `tools/release/linux/steam_art.py` the
  Steam icon.
- `marchwind64-<locale>.webp` (2000×667): the wordmark in each game language (en,
  ja, zh-Hans). `steam_art.py` puts it on the Steam capsules and uses it as the logo
  Steam draws over the hero.

The website's copies in `web/public/brand` are smaller exports of the same marks.
