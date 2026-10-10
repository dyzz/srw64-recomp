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
- Technical docs (`/docs/`): Markdown in `src/content/docs/<lang>/<slug>.md`, the same slug in each language. `script-commands.md` is generated from the layout lock's `stage_scripts` table by `python3 web/scripts/build_script_reference.py` (names and operands translated in `web/scripts/script-reference-i18n.json`; `--check` fails when a page is stale or a string untranslated). The same script writes `public/docs/mini-stage/llms.txt`, the mini stage usage and full reference in one file for AI agents, linked from the mini stage page. Every page is also served as Markdown at `/<lang>/docs/<slug>.md`.
- Guide pages (`/guide/`) render `guide/data/<language>/` the way `tools/content/build_guide.py` renders the offline guide; the offline file is served as `/downloads/marchwind64-guide.html`.
- `public/brand/`: the M64 mark (`m64-icon.png`, favicons `icon-*.png`) and the title logo for each language (`title-<lang>.webp`, `-small` for the footer), trimmed from the logo set.
- `public/media/`: screenshots, as WebP (1280 wide, hero 1600), most in three languages (`*-zh`, `*-en`, `*-ja`). Taken 2026-10-05 from debug runs of 0.3.5 at a 1280×720 window with the HD pack: the Battle Viewer (hero, finishing moves), the `battle-ui` mini stage (pre-battle page, map, settings, Library) and the `scene8` mini stage (dialogue, history, original/HD pair). In mini stages the protagonist has no portrait (no protagonist was chosen), so avoid lines spoken by the protagonist or partner.
- Visual style: the palette and slanted shapes of the game's modern UI (`src/native/ui/frontend.cpp`); dark only. The Latin display font is Chakra Petch (OFL, from `@fontsource`); CJK text uses system fonts.

## Game data

The story, Library and image pages need data derived from the ROM, which is not in git:

```sh
.venv/bin/python web/scripts/export_story.py      # web/.data/story, web/public/gen/portraits
.venv/bin/python web/scripts/export_history.py    # web/.data/history (after export_story.py)
.venv/bin/python web/scripts/export_library.py --check   # web/.data/library.json
.venv/bin/python web/scripts/export_images.py     # web/public/gen/units and the Library portraits
```

The story export also compares every line with the latest game release (the tag in `src/data/release.json`, or `--baseline TAG`): the stage list counts the lines changed since then, and each stage marks them, with "only changed" comparing old and new text. It records the last commit that changed the translations and whether `content/` had uncommitted edits. The Library export does the same for every name and term of each entry. `export_history.py` lists every commit since that release that changed a translation, with its changed lines (story, battle quotes, terms), for the history pages under `/<lang>/story/history/`; it reads committed files only.

In development run the API beside the dev server (`.claude/launch.json` has `web` and `web-api`); the dev server proxies `/api`.

## Deploy

`bash web/deploy/deploy.sh` is the one command for every site change: it runs `npm ci` when `package-lock.json` changed, exports the story, Library and history data again when the committed translations or the release tag in `src/data/release.json` moved on (and the Library images every time; they only redo changed ones), builds, uploads a release, switches to it (nginx config, systemd units and the daily database backup are in `web/deploy/`), has the origin refresh the CDN through its RAM role (`cdn:RefreshObjectCaches` on the domain) and checks that the CDN serves the new `deploy-id.txt` (`SRW64_DEPLOY_NO_CDN=1` skips that). Visitor statistics: Nginx forwards `/analytics/script.js` and `/analytics/api/send` to Umami on the server (set up apart); the build takes the public website ID from `PUBLIC_UMAMI_WEBSITE_ID` or `~/.config/srw64/site-analytics-id`, and without one the pages send nothing. A read-only share link made in Umami (website > share, views chosen there) is served at `/share/<code>` on the site: Nginx forwards only what that page reads, GET only, and keeps logins, visitor sessions, exports and writes closed; the link itself is kept out of the repository. Maintainers answer suggestions with `node web/api/admin.mjs`.

A site release after handling suggestions: change the translations, commit them, then `deploy.sh` (it exports again by itself). The suggestions answered by the new text show as accepted or as the final wording once the site has it. `deploy.sh` stops when an export is due while `content/` has uncommitted translations (`SRW64_DEPLOY_DIRTY=1` exports them anyway).


Visibility: a suggestion is seen only by its author and the maintainers until it is handled; others see the processed and declined ones (never a hidden one), so nothing unreviewed reaches the public pages. Moderation: `admin.mjs hide <id…>` keeps a suggestion's words off every list but its author's; `ban <id…>` stops those suggestions' authors from writing, and since an identity is only a cookie, also the address each came from for 30 days (a salted hash kept on a suggestion for 30 days, the salt only in the database); `unhide` and `unban` undo them. Nicknames that read as the site's own voice (管理, 维护, admin, MARCHWIND…) are refused.

Security, as deployed: pages carry a Content-Security-Policy `<meta>` with hashes (`security.csp` in `astro.config.mjs`: scripts only from the site or hashed inline ones; `is:inline` scripts would be blocked, so none are used), and Nginx adds `deploy/nginx-srw64-headers.conf` (no framing, nosniff, referrer policy). A suggestion's link back is kept only as a path on the site. Only the CDN reaches the origin: `web/deploy/origin-secret.sh` makes a secret on the server (`/etc/srw64-origin-secret`, root only), has the CDN send it as the back-to-origin header `X-Srw64-Origin`, and writes `/etc/nginx/srw64-cdn.conf`, which answers 403 to requests without it (but the server's own) and then trusts the visitor's address the CDN passes in `Ali-Cdn-Real-Ip`; the API rate-limits and bans by that (`X-Real-IP`). If the server's RAM role may not change the CDN, the script stops before touching Nginx: add the header in the CDN console (read the value on the server yourself) and run it again with `--cdn-done`. The secret is never printed and never enters the repository. HSTS is set on the CDN.

The maintainers' API is not public (`/api/admin/` answers 404): `admin.mjs` opens an ssh tunnel to the API on the server for each command and closes it after.

The origin is a server behind the CDN; Nginx serves `/srv/srw64/public` for `srw64.dreamquest.club`. `deploy.sh` reaches it by an ssh host name from `SRW64_DEPLOY_HOST` or `~/.config/srw64/site-deploy-host`, so the repository never names it.
