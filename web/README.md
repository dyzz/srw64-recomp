# srw64.dreamquest.club

The MARCHWIND 64 website: an Astro static site in three languages (`/zh/`, `/en/`, `/ja/`) plus a small API (`api/server.mjs`) for story search and translation suggestions.
The design notes stay outside the repository, since they describe the server.

```sh
npm --prefix web install
npm --prefix web run dev      # http://localhost:4321
npm --prefix web run build    # writes web/dist/
```

## Layout

- `src/i18n.ts`: languages and shared strings. Page copy lives in `src/copy/` (home, install) and in Markdown under `src/content/` (FAQ, blog posts), one file per language.
- `src/data/release.json`: the current release (version, date, files, sizes, SHA-256, links). Regenerate it from a `build_release.py` output with `node web/scripts/sync-release.mjs build/release/<version>-<commit> [--quark <share link>]`. The download page and `/latest.json` (read by the game's update check) both come from it.
- Guide pages (`/guide/`) render `guide/data/<language>/` the way `tools/content/build_guide.py` renders the offline guide; the offline file is served as `/downloads/marchwind64-guide.html`.
- `public/brand/`: the M64 mark (`m64-icon.png`, favicons `icon-*.png`) and the title logo for each language (`title-<lang>.webp`, `-small` for the footer), trimmed from the logo set.
- `public/media/`: screenshots, as WebP (1280 wide, hero 1600), most in three languages (`*-zh`, `*-en`, `*-ja`). Taken 2026-10-05 from debug runs of 0.3.5 at a 1280×720 window with the HD pack: the Battle Viewer (hero, finishing moves), the `battle-ui` mini stage (pre-battle page, map, settings, Library) and the `scene8` mini stage (dialogue, history, original/HD pair). In mini stages the protagonist has no portrait (no protagonist was chosen), so avoid lines spoken by the protagonist or partner.
- Visual style: the palette and slanted shapes of the game's modern UI (`src/native/ui/frontend.cpp`); dark only. The Latin display font is Chakra Petch (OFL, from `@fontsource`); CJK text uses system fonts.

## Game data

The story, Library and image pages need data derived from the ROM, which is not in git:

```sh
.venv/bin/python web/scripts/export_story.py      # web/.data/story, web/public/gen/portraits
.venv/bin/python web/scripts/export_library.py --check   # web/.data/library.json
.venv/bin/python web/scripts/export_images.py     # web/public/gen/units and the Library portraits
```

In development run the API beside the dev server (`.claude/launch.json` has `web` and `web-api`); the dev server proxies `/api`.

## Deploy

`web/deploy/deploy.sh` builds, uploads a release and switches to it (nginx config, systemd units and the daily database backup are in `web/deploy/`). Maintainers answer suggestions with `node web/api/admin.mjs`.


The origin is a server behind the CDN; Nginx serves `/srv/srw64/public` for `srw64.dreamquest.club`. `deploy.sh` reaches it by an ssh host name from `SRW64_DEPLOY_HOST` or `~/.config/srw64/site-deploy-host`, so the repository never names it.
