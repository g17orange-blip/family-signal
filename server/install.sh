#!/usr/bin/env bash
#
# One-shot installer for the family-signal server: coturn (STUN + TURN),
# the Go signaling server, and an nginx + Let's Encrypt TLS front end —
# all on a single VPS.
#
# Usage (as root, on a fresh Ubuntu 22.04/24.04 box pointed at by a domain):
#
#   curl -fsSL https://raw.githubusercontent.com/g17orange-blip/family-signal/main/server/install.sh \
#     | sudo DOMAIN=signal.example.com EMAIL=you@example.com bash
#
# Or clone the repo and run:  sudo DOMAIN=... EMAIL=... bash server/install.sh
#
# DOMAIN/EMAIL may be left unset — the script will prompt. It also prompts
# for the family members (id + display name) and prints, for each one, a
# ready-to-paste config.json plus a single-line invite code for the client's
# first-run wizard.
#
# Re-running is safe: generated secrets are reused from /etc/signal/secrets.env.

set -euo pipefail

REPO_SLUG="${REPO_SLUG:-g17orange-blip/family-signal}"
SECRETS_FILE="/etc/signal/secrets.env"
TURN_USER="signaluser"
# Where `go install` should fetch the signaling package from when building
# from source (fallback when no release asset is available yet).
SIGNALING_REF="${SIGNALING_REF:-main}"
BUILD_SIGNALING=0
[[ "${1:-}" == "--build-signaling" ]] && BUILD_SIGNALING=1

# --- pretty output --------------------------------------------------------
c_blue=$'\033[1;34m'; c_green=$'\033[1;32m'; c_yellow=$'\033[1;33m'
c_red=$'\033[1;31m'; c_reset=$'\033[0m'
step() { printf '%s==>%s %s\n' "$c_blue" "$c_reset" "$*"; }
ok()   { printf '%s ✓ %s%s\n' "$c_green" "$*" "$c_reset"; }
warn() { printf '%s ! %s%s\n' "$c_yellow" "$*" "$c_reset" >&2; }
die()  { printf '%s ✗ %s%s\n' "$c_red" "$*" "$c_reset" >&2; exit 1; }

# --- preflight ------------------------------------------------------------
[[ $EUID -eq 0 ]] || die "run as root (use sudo)"
command -v apt-get >/dev/null || die "this installer targets Debian/Ubuntu (apt)"

prompt_if_empty() {  # name, prompt-text
  local cur="${!1:-}"
  if [[ -z "$cur" ]]; then
    read -rp "$2: " cur </dev/tty
    printf -v "$1" '%s' "$cur"
  fi
}

DOMAIN="${DOMAIN:-}"; EMAIL="${EMAIL:-}"
prompt_if_empty DOMAIN  "Domain pointing at this VPS (e.g. signal.example.com)"
prompt_if_empty EMAIL   "Email for Let's Encrypt notices"
[[ -n "$DOMAIN" && -n "$EMAIL" ]] || die "DOMAIN and EMAIL are required"

# Collect family members: id + display name, two or more.
# Non-interactive: MEMBERS="id:Name,id2:Name2,..." skips the prompts.
declare -a MEMBER_IDS=() MEMBER_NAMES=()
if [[ -n "${MEMBERS:-}" ]]; then
  IFS=',' read -ra _pairs <<< "$MEMBERS"
  for p in "${_pairs[@]}"; do
    p="${p#"${p%%[![:space:]]*}"}"; p="${p%"${p##*[![:space:]]}"}"  # trim
    [[ -z "$p" ]] && continue
    mid="${p%%:*}"
    if [[ "$p" == *:* ]]; then mname="${p#*:}"; else mname="$mid"; fi
    MEMBER_IDS+=("$mid"); MEMBER_NAMES+=("$mname")
  done
  (( ${#MEMBER_IDS[@]} >= 2 )) || die "MEMBERS needs at least 2 entries (format: id:Name,id2:Name2)"
  ok "members from \$MEMBERS: ${MEMBER_IDS[*]}"
else
step "Family members (blank id when done; at least 2):"
while true; do
  mid=""; mname=""
  read -rp "  member ${#MEMBER_IDS[@]} id (latin, e.g. grandpa): " mid </dev/tty || true
  if [[ -z "$mid" ]]; then
    if (( ${#MEMBER_IDS[@]} >= 2 )); then break; fi
    warn "need at least 2"; continue
  fi
  read -rp "  member display name (e.g. Дедушка): " mname </dev/tty || true
  [[ -z "$mname" ]] && mname="$mid"
  MEMBER_IDS+=("$mid"); MEMBER_NAMES+=("$mname")
done
fi

# --- public IP & DNS sanity ----------------------------------------------
step "Detecting public IP"
PUBLIC_IP="$(curl -fsS https://api.ipify.org 2>/dev/null || true)"
[[ -z "$PUBLIC_IP" ]] && PUBLIC_IP="$(ip route get 1.1.1.1 2>/dev/null | awk '{print $7; exit}')"
[[ -n "$PUBLIC_IP" ]] || die "could not determine public IP"
ok "public IP: $PUBLIC_IP"

RESOLVED="$(getent ahostsv4 "$DOMAIN" 2>/dev/null | awk '{print $1; exit}' || true)"
if [[ -n "$RESOLVED" && "$RESOLVED" != "$PUBLIC_IP" ]]; then
  warn "$DOMAIN resolves to $RESOLVED, not $PUBLIC_IP — certbot will fail until DNS is correct"
elif [[ -z "$RESOLVED" ]]; then
  warn "$DOMAIN does not resolve yet — make sure its A record points to $PUBLIC_IP"
fi

# --- packages -------------------------------------------------------------
step "Installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq coturn nginx certbot python3-certbot-nginx curl ufw openssl python3 ca-certificates
ok "packages installed"

# --- secrets (generate once, reuse on re-run) -----------------------------
step "Provisioning secrets ($SECRETS_FILE)"
install -d -m 700 /etc/signal
if [[ -f "$SECRETS_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$SECRETS_FILE"
  ok "reusing existing secrets"
fi
SIGNAL_TOKEN="${SIGNAL_TOKEN:-$(openssl rand -hex 32)}"
TURN_PASSWORD="${TURN_PASSWORD:-$(openssl rand -hex 24)}"
umask 077
cat > "$SECRETS_FILE" <<EOF
SIGNAL_TOKEN=$SIGNAL_TOKEN
TURN_PASSWORD=$TURN_PASSWORD
EOF
chmod 600 "$SECRETS_FILE"
ok "secrets ready"

# --- signaling binary -----------------------------------------------------
step "Installing signaling binary"
ARCH="$(dpkg --print-architecture)"   # amd64 | arm64
ASSET_URL="https://github.com/${REPO_SLUG}/releases/latest/download/signaling-linux-${ARCH}"
install_from_source() {
  warn "building signaling from source via go install (${SIGNALING_REF})"
  command -v go >/dev/null || apt-get install -y -qq golang-go
  GOBIN=/usr/local/bin GOFLAGS=-trimpath \
    go install "github.com/${REPO_SLUG}/server/signaling@${SIGNALING_REF}"
}
if [[ "$BUILD_SIGNALING" -eq 1 ]]; then
  install_from_source
elif curl -fsSL -o /usr/local/bin/signaling.download "$ASSET_URL"; then
  install -m 755 /usr/local/bin/signaling.download /usr/local/bin/signaling
  rm -f /usr/local/bin/signaling.download
  ok "downloaded prebuilt binary ($ARCH)"
else
  rm -f /usr/local/bin/signaling.download
  warn "no release asset at $ASSET_URL"
  if [[ -x /usr/local/bin/signaling ]]; then
    warn "reusing existing /usr/local/bin/signaling"
  else
    install_from_source
  fi
fi
[[ -x /usr/local/bin/signaling ]] || die "signaling binary missing"
ok "signaling installed"

# --- coturn ---------------------------------------------------------------
# Base config: STUN + plain TURN on 3478 (always works). TURNS on 5349 is
# added by enable_coturn_tls() only after a certificate exists, so a failed
# certbot run never prevents coturn from starting.
step "Configuring coturn (STUN + TURN)"
cat > /etc/turnserver.conf <<EOF
# Managed by family-signal install.sh — re-running the installer rewrites this.
# TLS lines (tls-listening-port/cert/pkey) are appended by the installer once
# a Let's Encrypt certificate is available; do not edit them by hand.
listening-port=3478
external-ip=$PUBLIC_IP
min-port=49152
max-port=65535

lt-cred-mech
realm=$DOMAIN
user=$TURN_USER:$TURN_PASSWORD

no-loopback-peers
no-multicast-peers
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
denied-peer-ip=192.168.0.0-192.168.255.255
denied-peer-ip=169.254.0.0-169.254.255.255
denied-peer-ip=127.0.0.0-127.255.255.255

# Quotas cap concurrent TURN allocations. The whole family shares ONE turn
# user (signaluser), and relay-only ICE (client default since v0.3.4) makes
# every call leg allocate a relay — failed/redialed calls leave allocations
# pinned until their ~10-min lifetime expires. The old user-quota=12 was
# reached after a handful of dropped calls and then refused every new
# allocation ("486 Allocation Quota Reached") → calls stuck in ICE checking
# forever ("worked first, then stopped connecting"). user-quota=0 lifts the
# per-user cap; total-quota stays as the real ceiling, raised for headroom.
total-quota=200
user-quota=0
max-bps=2000000

log-file=stdout
no-stdout-log
simple-log
EOF
# Enable the daemon (Debian gates it behind this flag).
if grep -q '^#\?TURNSERVER_ENABLED' /etc/default/coturn 2>/dev/null; then
  sed -i 's/^#\?TURNSERVER_ENABLED=.*/TURNSERVER_ENABLED=1/' /etc/default/coturn
else
  echo 'TURNSERVER_ENABLED=1' >> /etc/default/coturn
fi
ok "coturn configured"

# --- nginx (HTTP first, certbot adds TLS) ---------------------------------
step "Configuring nginx reverse proxy"
cat > "/etc/nginx/sites-available/$DOMAIN" <<EOF
server {
    listen 80;
    listen [::]:80;
    server_name $DOMAIN;

    location /ws {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_read_timeout 90s;
    }

    location /healthz { proxy_pass http://127.0.0.1:8080; }
    location / { return 200 "family-signal\n"; add_header Content-Type text/plain; }
}
EOF
ln -sf "/etc/nginx/sites-available/$DOMAIN" "/etc/nginx/sites-enabled/$DOMAIN"
rm -f /etc/nginx/sites-enabled/default
nginx -t
systemctl reload nginx
ok "nginx serving http://$DOMAIN"

# --- firewall (before certbot so HTTP-01 can reach :80) -------------------
step "Configuring firewall (ufw)"
ufw allow OpenSSH    >/dev/null 2>&1 || ufw allow 22/tcp >/dev/null
ufw allow 80/tcp     >/dev/null
ufw allow 443/tcp    >/dev/null
ufw allow 3478/udp   >/dev/null
ufw allow 3478/tcp   >/dev/null
ufw allow 5349/tcp   >/dev/null
ufw allow 49152:65535/udp >/dev/null
ufw --force enable   >/dev/null
ok "firewall rules applied"

# Copy the LE cert into a coturn-readable dir and enable TURNS on 5349.
# coturn runs as the 'turnserver' user (Debian) and cannot read root-only
# /etc/letsencrypt; the copy is owned by that user. Safe to call repeatedly.
COTURN_CERT_DIR="/etc/coturn/certs"
enable_coturn_tls() {
  local live="/etc/letsencrypt/live/$DOMAIN"
  [[ -r "$live/privkey.pem" ]] || { warn "no cert at $live — skipping TURNS"; return 1; }
  local owner="root"
  getent passwd turnserver >/dev/null && owner="turnserver"
  install -d -m 750 "$COTURN_CERT_DIR"
  install -m 644 "$live/fullchain.pem" "$COTURN_CERT_DIR/fullchain.pem"
  install -m 600 "$live/privkey.pem"   "$COTURN_CERT_DIR/privkey.pem"
  chown -R "$owner" "$COTURN_CERT_DIR"
  if ! grep -q '^tls-listening-port' /etc/turnserver.conf; then
    cat >> /etc/turnserver.conf <<EOF

# --- TLS (added by install.sh once a certificate was available) ----------
tls-listening-port=5349
cert=$COTURN_CERT_DIR/fullchain.pem
pkey=$COTURN_CERT_DIR/privkey.pem
EOF
  fi
}

# --- TLS via certbot ------------------------------------------------------
step "Obtaining TLS certificate (Let's Encrypt)"
if certbot --nginx -d "$DOMAIN" -m "$EMAIL" --agree-tos -n --redirect; then
  ok "certificate installed; wss://$DOMAIN/ws is live"
  enable_coturn_tls && ok "TURNS enabled on 5349"
else
  warn "certbot failed — fix DNS/port 80 and re-run. Signaling falls back to ws:// and TURN to plain 3478 until then."
fi

# Refresh coturn's cert copy and reload it whenever the cert renews.
install -d /etc/letsencrypt/renewal-hooks/deploy
cat > /etc/letsencrypt/renewal-hooks/deploy/coturn-reload.sh <<EOF
#!/bin/sh
live="/etc/letsencrypt/live/$DOMAIN"
owner=root; getent passwd turnserver >/dev/null && owner=turnserver
install -d -m 750 "$COTURN_CERT_DIR"
install -m 644 "\$live/fullchain.pem" "$COTURN_CERT_DIR/fullchain.pem"
install -m 600 "\$live/privkey.pem"   "$COTURN_CERT_DIR/privkey.pem"
chown -R "\$owner" "$COTURN_CERT_DIR"
systemctl reload coturn 2>/dev/null || systemctl restart coturn
EOF
chmod 755 /etc/letsencrypt/renewal-hooks/deploy/coturn-reload.sh

# --- signaling systemd unit ----------------------------------------------
step "Installing signaling systemd service"
cat > /etc/systemd/system/signaling.service <<EOF
[Unit]
Description=family-signal signaling server
After=network.target

[Service]
ExecStart=/usr/local/bin/signaling -addr 127.0.0.1:8080
EnvironmentFile=$SECRETS_FILE
Restart=on-failure
RestartSec=2
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now signaling
systemctl enable --now coturn
systemctl restart coturn   # pick up cert if certbot just ran
ok "services started"

# --- per-member config + invite codes -------------------------------------
step "Generating client configs and invite codes"
SIGNALING_URL="wss://$DOMAIN/ws"
STUN_URL="stun:$DOMAIN:3478"
TURN_URL="turn:$DOMAIN:3478"

# Hand off to python3 for safe JSON encoding (names may be non-ASCII).
MEMBER_IDS_JOINED="$(IFS=$'\n'; echo "${MEMBER_IDS[*]}")"
MEMBER_NAMES_JOINED="$(IFS=$'\n'; echo "${MEMBER_NAMES[*]}")"
export MEMBER_IDS_JOINED MEMBER_NAMES_JOINED \
       SIGNALING_URL SIGNAL_TOKEN STUN_URL TURN_URL TURN_USER TURN_PASSWORD

python3 - <<'PY'
import base64, json, os

ids   = os.environ["MEMBER_IDS_JOINED"].splitlines()
names = os.environ["MEMBER_NAMES_JOINED"].splitlines()
GREEN, YEL, RST = "\033[1;32m", "\033[1;33m", "\033[0m"

for i, (uid, uname) in enumerate(zip(ids, names)):
    peers = [{"id": pid, "name": pn}
             for j, (pid, pn) in enumerate(zip(ids, names)) if j != i]
    cfg = {
        "user_id": uid,
        "display_name": uname,
        "signaling_url": os.environ["SIGNALING_URL"],
        "signaling_token": os.environ["SIGNAL_TOKEN"],
        "stun_url": os.environ["STUN_URL"],
        "peers": peers,
        "turn": {
            "url": os.environ["TURN_URL"],
            "username": os.environ["TURN_USER"],
            "password": os.environ["TURN_PASSWORD"],
        },
    }
    pretty = json.dumps(cfg, ensure_ascii=False, indent=2)
    invite = base64.b64encode(
        json.dumps(cfg, ensure_ascii=False).encode("utf-8")).decode("ascii")
    print(f"\n{GREEN}━━━ {uname} ({uid}) ━━━{RST}")
    print(f"{YEL}config.json:{RST}\n{pretty}")
    print(f"\n{YEL}invite code (paste into the client's first-run wizard):{RST}")
    print(invite)
PY

cat <<EOF

$c_green━━━ Done ━━━$c_reset
Signaling : $SIGNALING_URL   (health: https://$DOMAIN/healthz)
STUN      : $STUN_URL
TURN      : $TURN_URL  (user $TURN_USER) / turns:$DOMAIN:5349
Secrets   : $SECRETS_FILE

Verify from a client machine:
  turnutils_stunclient $DOMAIN
  turnutils_uclient -u $TURN_USER -w $TURN_PASSWORD -y $DOMAIN
  curl -I https://$DOMAIN/healthz

Service status:  systemctl status signaling coturn
EOF
