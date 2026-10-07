#!/usr/bin/env bash
# Deploy the MARCHWIND 64 site to the origin server.
#
#   web/deploy/deploy.sh            build, upload a new release, switch to it
#
# Needs: web/.data from export_story.py / export_library.py and web/public/gen from
# export_images.py (they come from the ROM-derived assets, so run them here first);
# ssh access to the origin, whose host name comes from SRW64_DEPLOY_HOST or
# ~/.config/srw64/site-deploy-host (kept out of the repository). Each deploy is /srv/srw64/releases/<id>
# (unchanged files hard-linked to the previous one); /srv/srw64/current points at
# the live one; the five newest are kept. The suggestions database stays in
# /srv/srw64/data. The admin token is created on the server the first time and
# copied to ~/.config/srw64/site-admin-token without being printed.
set -euo pipefail
HOST=${SRW64_DEPLOY_HOST:-$(cat ~/.config/srw64/site-deploy-host 2>/dev/null || true)}
[ -n "$HOST" ] || { echo "set SRW64_DEPLOY_HOST or write ~/.config/srw64/site-deploy-host" >&2; exit 1; }
WEB=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$WEB/.." && pwd)
for f in .data/story/index.json .data/story/search.json .data/library.json .data/history/index.json public/gen/portraits; do
  [ -e "$WEB/$f" ] || { echo "missing web/$f: run the exporters in web/scripts first" >&2; exit 1; }
done

# The site publishes committed translations: after handling suggestions, commit the text,
# export again, then deploy. The story export records what it read (export_story.py).
read -r EXPORTED DIRTY <<<"$(node -e 'const s=require(process.argv[1]).source??{};console.log((s.commit||"-")+" "+(s.dirty?1:0))' "$WEB/.data/story/index.json")"
LATEST=$(git -C "$ROOT" log -1 --format=%h -- content/dialogue content/locales)
if [ "$DIRTY" = 1 ] && [ "${SRW64_DEPLOY_DIRTY:-}" != 1 ]; then
  echo "the story export read uncommitted edits in content/: commit them (or set SRW64_DEPLOY_DIRTY=1), export again" >&2; exit 1
fi
if [ "$EXPORTED" != "$LATEST" ]; then
  echo "the story export is of $EXPORTED but the translations are at $LATEST: run web/scripts/export_story.py again" >&2; exit 1
fi

(cd "$WEB" && npm run build --silent)
ID=$(date +%Y%m%d-%H%M%S)-$(git -C "$ROOT" rev-parse --short HEAD)$(git -C "$ROOT" diff --quiet -- web content || echo -dirty)
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
echo "deployed $ID"
