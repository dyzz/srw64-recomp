#!/usr/bin/env bash
# Deploy the MARCHWIND 64 site to the origin server: one command for every change.
#
#   web/deploy/deploy.sh            dependencies, exports, build, upload, switch, CDN
#
# Before building it brings everything up to date by itself:
#   - npm ci when web/package-lock.json changed since the last install;
#   - the story, Library and history exports (web/scripts) when the committed
#     translations or the release tag in web/src/data/release.json moved on since the
#     last export, and the Library images every time (they only redo changed ones).
#     The site publishes committed translations: when an export is due and content/ has
#     uncommitted text, the deploy stops (SRW64_DEPLOY_DIRTY=1 exports it anyway).
# The exports read the ROM-derived assets, so this runs on the machine that has them.
# Visitor statistics (Umami, set up apart, docs in web/README.md): the public website
# ID comes from PUBLIC_UMAMI_WEBSITE_ID or ~/.config/srw64/site-analytics-id; without
# one the pages send nothing.
#
# The origin's host name comes from SRW64_DEPLOY_HOST or ~/.config/srw64/site-deploy-host
# (kept out of the repository). Each deploy is /srv/srw64/releases/<id> (unchanged files
# hard-linked to the previous one); /srv/srw64/current points at the live one; the five
# newest are kept. The suggestions database stays in /srv/srw64/data. The admin token is
# created on the server the first time and copied to ~/.config/srw64/site-admin-token
# without being printed. After the switch the origin refreshes the CDN's cache through
# its own RAM role (cdn:RefreshObjectCaches on the domain) and this checks that the CDN
# serves the new release; SRW64_DEPLOY_NO_CDN=1 skips that.
set -euo pipefail
HOST=${SRW64_DEPLOY_HOST:-$(cat ~/.config/srw64/site-deploy-host 2>/dev/null || true)}
[ -n "$HOST" ] || { echo "set SRW64_DEPLOY_HOST or write ~/.config/srw64/site-deploy-host" >&2; exit 1; }
DOMAIN=srw64.dreamquest.club
WEB=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$WEB/.." && pwd)
PY="$ROOT/.venv/bin/python"

# Dependencies, only when the lock file changed.
LOCK=$(shasum -a 256 "$WEB/package-lock.json" | cut -c1-64)
if [ "$(cat "$WEB/node_modules/.srw64-lock" 2>/dev/null)" != "$LOCK" ]; then
  echo "npm ci (package-lock.json changed)"
  (cd "$WEB" && npm ci --silent)
  echo "$LOCK" > "$WEB/node_modules/.srw64-lock"
fi

# Exports. The story export records what it read: the commit of the translations, whether
# content/ had uncommitted edits, and the release tag it compared against.
LATEST=$(git -C "$ROOT" log -1 --format=%h -- content/dialogue content/locales)
TAG=$(node -e 'console.log(require(process.argv[1]).tag)' "$WEB/src/data/release.json")
# The story and Library exports read content/ as it is on disk; the history export reads
# commits only, so someone's uncommitted text in the shared worktree matters only for the
# first two.
story_stale() {
  for f in .data/story/index.json .data/story/search.json .data/library.json; do
    [ -e "$WEB/$f" ] || { echo "missing web/$f"; return 0; }
  done
  read -r EXPORTED BASE DIRTY <<<"$(node -e 'const s=require(process.argv[1]).source??{};console.log((s.commit||"-")+" "+(s.baseline||"-")+" "+(s.dirty?1:0))' "$WEB/.data/story/index.json")"
  [ "$EXPORTED" = "$LATEST" ] || { echo "translations at $LATEST, story exported at $EXPORTED"; return 0; }
  [ "$BASE" = "$TAG" ] || { echo "release $TAG, story compared with $BASE"; return 0; }
  [ "$DIRTY" = 1 ] && [ "${SRW64_DEPLOY_DIRTY:-}" != 1 ] && { echo "the story export read uncommitted text"; return 0; }
  return 1
}
history_stale() {
  [ -e "$WEB/.data/history/index.json" ] || { echo "missing web/.data/history/index.json"; return 0; }
  local base; base=$(node -e 'console.log(require(process.argv[1]).baseline||"-")' "$WEB/.data/history/index.json")
  [ "$base" = "$TAG" ] || { echo "release $TAG, history from $base"; return 0; }
  return 1
}
if WHY=$(story_stale); then
  if ! git -C "$ROOT" diff --quiet HEAD -- content/dialogue content/locales && [ "${SRW64_DEPLOY_DIRTY:-}" != 1 ]; then
    echo "the site data needs exporting ($WHY) but content/ has uncommitted translations: commit them first (or set SRW64_DEPLOY_DIRTY=1)" >&2; exit 1
  fi
  echo "exporting the story, Library and history ($WHY)"
  (cd "$ROOT" && "$PY" web/scripts/export_story.py >/dev/null && "$PY" web/scripts/export_library.py --check >/dev/null \
    && "$PY" web/scripts/export_history.py >/dev/null) || { echo "an export failed: run the scripts in web/scripts by hand to see why" >&2; exit 1; }
elif WHY=$(history_stale); then
  echo "exporting the history ($WHY)"
  (cd "$ROOT" && "$PY" web/scripts/export_history.py >/dev/null) || { echo "export_history.py failed" >&2; exit 1; }
fi
(cd "$ROOT" && "$PY" web/scripts/export_images.py >/dev/null)
[ -e "$WEB/public/gen/portraits" ] || { echo "missing web/public/gen/portraits: web/scripts/export_images.py made none" >&2; exit 1; }

if [ -z "${PUBLIC_UMAMI_WEBSITE_ID:-}" ] && [ -s ~/.config/srw64/site-analytics-id ]; then
  export PUBLIC_UMAMI_WEBSITE_ID=$(cat ~/.config/srw64/site-analytics-id)
fi
(cd "$WEB" && npm run build --silent)
ID=$(date +%Y%m%d-%H%M%S)-$(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet -- web content || echo -dirty)
# What the CDN check below reads back.
echo "$ID" > "$WEB/dist/deploy-id.txt"
REL=/srv/srw64/releases/$ID
echo "release $ID"

ssh "$HOST" "mkdir -p $REL /srv/srw64/data /srv/srw64/backups && chown www-data:www-data /srv/srw64/data /srv/srw64/backups && chmod 750 /srv/srw64/data /srv/srw64/backups"
LINK=$(ssh "$HOST" 'readlink -f /srv/srw64/current 2>/dev/null || true')
rsync -a --delete ${LINK:+--link-dest=$LINK/public} "$WEB/dist/" "$HOST:$REL/public/"
rsync -a ${LINK:+--link-dest=$LINK/api} "$WEB/api/" "$HOST:$REL/api/"
rsync -a ${LINK:+--link-dest=$LINK/data} "$WEB/.data/library.json" "$HOST:$REL/data/"
rsync -a ${LINK:+--link-dest=$LINK/data/story} "$WEB/.data/story/index.json" "$WEB/.data/story/search.json" "$HOST:$REL/data/story/"
scp -q "$WEB/deploy/nginx-srw64.conf" "$WEB/deploy/nginx-srw64-headers.conf" "$WEB/deploy/srw64-api.service" "$WEB/deploy/srw64-api-backup.service" "$WEB/deploy/srw64-api-backup.timer" "$HOST:$REL/"

ssh "$HOST" bash -s -- "$REL" <<'REMOTE'
set -euo pipefail
REL=$1
if [ ! -f /etc/srw64-api.env ]; then
  umask 077
  echo "SRW64_ADMIN_TOKEN=$(openssl rand -hex 24)" > /etc/srw64-api.env
fi
install -m 644 "$REL/srw64-api.service" "$REL/srw64-api-backup.service" "$REL/srw64-api-backup.timer" /etc/systemd/system/
install -m 644 "$REL/nginx-srw64.conf" /etc/nginx/sites-available/srw64
install -m 644 "$REL/nginx-srw64-headers.conf" /etc/nginx/srw64-headers.conf
[ -f /etc/nginx/srw64-cdn.conf ] || echo "warning: /etc/nginx/srw64-cdn.conf is missing: visitors' addresses are the CDN's (README)" >&2
ln -sfn /etc/nginx/sites-available/srw64 /etc/nginx/sites-enabled/srw64
ln -sfn "$REL" /srv/srw64/current.new && mv -T /srv/srw64/current.new /srv/srw64/current
systemctl daemon-reload
systemctl enable --now srw64-api-backup.timer >/dev/null
systemctl restart srw64-api
nginx -t 2>&1 | tail -1
systemctl reload nginx
for i in $(seq 1 20); do curl -fsS http://127.0.0.1:3064/api/health >/dev/null 2>&1 && break; sleep 0.5; done
curl -fsS http://127.0.0.1:3064/api/health; echo
curl -fsS -o /dev/null -w "site %{http_code}\n" -H "Host: srw64.dreamquest.club" http://127.0.0.1/zh/
ls -1dt /srv/srw64/releases/* | tail -n +6 | xargs -r rm -rf
REMOTE

mkdir -p ~/.config/srw64
if [ ! -s ~/.config/srw64/site-admin-token ]; then
  ssh "$HOST" "sed -n 's/^SRW64_ADMIN_TOKEN=//p' /etc/srw64-api.env" > ~/.config/srw64/site-admin-token
  chmod 600 ~/.config/srw64/site-admin-token
fi

# The CDN keeps pages up to 10 minutes: the origin refreshes the whole site and waits for
# the task (at most 5 minutes), then the CDN must hand back this release's deploy-id.txt.
if [ "${SRW64_DEPLOY_NO_CDN:-}" != 1 ]; then
  if ssh -o ControlMaster=no -o ControlPath=none "$HOST" DOMAIN="$DOMAIN" bash -s <<'REMOTE'
set -euo pipefail
OUT=$(aliyun cdn RefreshObjectCaches --ObjectType Directory --ObjectPath "https://$DOMAIN/" 2>&1) || { echo "$OUT" | tail -2 >&2; exit 1; }
TASK=$(echo "$OUT" | grep -o '"RefreshTaskId": *"[0-9]*"' | grep -o '[0-9]*' | head -1)
for i in $(seq 1 60); do
  aliyun cdn DescribeRefreshTaskById --TaskId "$TASK" 2>/dev/null | grep -q '"Status": *"Complete"' && { echo "cdn refreshed"; exit 0; }
  sleep 5
done
echo "the CDN refresh task $TASK is still running" >&2
REMOTE
  then
    for i in $(seq 1 12); do
      [ "$(curl -fsS "https://$DOMAIN/deploy-id.txt" 2>/dev/null)" = "$ID" ] && { echo "cdn serves $ID"; break; }
      [ "$i" = 12 ] && echo "warning: the CDN still serves an older release; it catches up within 10 minutes" >&2
      sleep 5
    done
  else
    echo "warning: the CDN was not refreshed; refresh https://$DOMAIN/ in the CDN console, or wait up to 10 minutes" >&2
  fi
fi
echo "deployed $ID"
