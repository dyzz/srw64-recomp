#!/usr/bin/env bash
# Make the origin answer only the CDN (web/README.md, Security).
#
#   web/deploy/origin-secret.sh             create the secret if missing, set the CDN's
#                                          back-to-origin header, then write the Nginx check
#   web/deploy/origin-secret.sh --cdn-done  the header was set in the CDN console: only the
#                                          Nginx check (read the value on the server:
#                                          ssh <host> cat /etc/srw64-origin-secret)
#
# The order matters: the CDN must send the header before Nginx refuses requests without it.
# Runs over ssh on the origin (host from SRW64_DEPLOY_HOST or ~/.config/srw64/site-deploy-host).
# The secret is made there (/etc/srw64-origin-secret, root only) and goes from that file to
# /etc/nginx/srw64-cdn.conf and to the CDN through the origin's own aliyun CLI (its RAM
# role): it is never printed, never leaves the server otherwise, never enters the repository.
set -euo pipefail
HOST=${SRW64_DEPLOY_HOST:-$(cat ~/.config/srw64/site-deploy-host 2>/dev/null || true)}
[ -n "$HOST" ] || { echo "set SRW64_DEPLOY_HOST or write ~/.config/srw64/site-deploy-host" >&2; exit 1; }
DOMAIN=${SRW64_DOMAIN:-srw64.dreamquest.club}
CDN_DONE=0
[ "${1:-}" = --cdn-done ] && CDN_DONE=1

ssh -o ControlMaster=no -o ControlPath=none "$HOST" DOMAIN="$DOMAIN" CDN_DONE="$CDN_DONE" bash -s <<'REMOTE'
set -euo pipefail
umask 077
[ -s /etc/srw64-origin-secret ] || openssl rand -hex 32 > /etc/srw64-origin-secret
SECRET=$(cat /etc/srw64-origin-secret)
# The CDN adds the header to every request it sends here.
if [ "$CDN_DONE" != 1 ]; then
  if ! aliyun cdn BatchSetCdnDomainConfig --DomainNames "$DOMAIN" \
      --Functions "[{\"functionName\":\"set_req_header\",\"functionArgs\":[{\"argName\":\"key\",\"argValue\":\"X-Srw64-Origin\"},{\"argName\":\"value\",\"argValue\":\"$SECRET\"}]}]" >/dev/null 2>&1; then
    echo "the CDN could not be set from here (the RAM role may lack cdn:BatchSetCdnDomainConfig)." >&2
    echo "Add it in the CDN console instead: $DOMAIN > Back-to-origin > custom request header" >&2
    echo "X-Srw64-Origin = the value of /etc/srw64-origin-secret on this server; then run with --cdn-done." >&2
    echo "Nginx is unchanged." >&2
    exit 2
  fi
  echo "CDN back-to-origin header set"
fi
# Requests without it are refused, but this machine's own (deploy.sh checks the site at
# 127.0.0.1). $realip_remote_addr is the peer itself: Ali-Cdn-Real-Ip cannot fake it.
cat > /etc/nginx/srw64-cdn.conf.new <<CONF
# Written by web/deploy/origin-secret.sh. Holds the CDN's back-to-origin secret: root only.
set_real_ip_from 0.0.0.0/0;
set_real_ip_from ::/0;
set \$srw64_from_cdn 0;
if (\$http_x_srw64_origin = "$SECRET") { set \$srw64_from_cdn 1; }
if (\$realip_remote_addr = 127.0.0.1) { set \$srw64_from_cdn 1; }
if (\$srw64_from_cdn = 0) { return 403; }
CONF
chmod 600 /etc/nginx/srw64-cdn.conf.new
mv /etc/nginx/srw64-cdn.conf.new /etc/nginx/srw64-cdn.conf
if [ -e /etc/nginx/sites-enabled/srw64 ]; then nginx -t 2>&1 | tail -1 && systemctl reload nginx; fi
echo "origin check written"
REMOTE
