# Deploying the family-signal server (coturn + signaling) on one VPS

Tested target: Ubuntu 24.04 LTS, 1 vCPU / 1 GB RAM (e.g. Hetzner CX11, ~€4/mo).

You need a **domain name** with an `A` record pointing at the VPS public IP —
TLS (Let's Encrypt) is issued for it, giving `wss://` signaling and `turns://`
relay.

## Quick install (recommended)

One command sets up coturn (STUN + TURN), the Go signaling server, nginx +
Let's Encrypt TLS, systemd units, and the firewall:

```sh
curl -fsSL https://raw.githubusercontent.com/g17orange-blip/family-signal/main/server/install.sh \
  | sudo DOMAIN=signal.example.com EMAIL=you@example.com bash
```

(Leave `DOMAIN`/`EMAIL` off to be prompted.) The script then asks for the family
members (id + display name) and prints, for each one, a ready `config.json` and a
single-line **invite code** to paste into the client's first-run wizard.

Re-running is safe — generated secrets are reused from `/etc/signal/secrets.env`.

### What it installs

| Component | Where |
|---|---|
| Secrets (token, TURN password) | `/etc/signal/secrets.env` (0600) |
| Signaling binary | `/usr/local/bin/signaling`, bound to `127.0.0.1:8080` |
| Signaling service | `systemd` unit `signaling.service` |
| STUN / TURN | `coturn`, `/etc/turnserver.conf` |
| TLS front end | `nginx` proxies `wss://DOMAIN/ws` → signaling |
| Certificates | Let's Encrypt; coturn copy in `/etc/coturn/certs` |

Ports opened: 80, 443, 3478/udp+tcp (STUN/TURN), 5349/tcp (TURNS),
49152–65535/udp (relay).

If no published release asset exists yet for your CPU arch, the script falls
back to building the signaling binary from source (`go install`); you can force
that with `--build-signaling`.

## Verify

```sh
curl -I https://signal.example.com/healthz          # 200 OK
turnutils_stunclient signal.example.com             # STUN
turnutils_uclient -u signaluser -w PASSWORD -y signal.example.com  # TURN
systemctl status signaling coturn
```

`PASSWORD` is the `TURN_PASSWORD` value from `/etc/signal/secrets.env`.

---

## Manual procedure (fallback / reference)

Only needed if you can't run `install.sh` (e.g. non-Ubuntu, or you want to wire
things up by hand). The steps mirror what the script automates.

### 1. Packages

```sh
sudo apt update
sudo apt install -y coturn nginx certbot python3-certbot-nginx
```

Edit `/etc/default/coturn` and set `TURNSERVER_ENABLED=1`.

### 2. coturn

```sh
sudo cp turnserver.conf.example /etc/turnserver.conf
sudo $EDITOR /etc/turnserver.conf   # fill the REPLACE_WITH_* placeholders
sudo systemctl enable --now coturn
```

### 3. Signaling binary + service

Build (`cd server/signaling && go build -o signaling .`) or download the release
asset, copy to `/usr/local/bin/`, and create
`/etc/systemd/system/signaling.service`:

```ini
[Unit]
Description=family-signal signaling server
After=network.target

[Service]
ExecStart=/usr/local/bin/signaling -addr 127.0.0.1:8080
Environment=SIGNAL_TOKEN=REPLACE_WITH_LONG_RANDOM_TOKEN
Restart=on-failure
DynamicUser=yes

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now signaling
```

### 4. nginx + TLS

Reverse-proxy `wss://DOMAIN/ws` → `127.0.0.1:8080` with WebSocket upgrade
headers, then `sudo certbot --nginx -d DOMAIN`. Point coturn's `cert`/`pkey` at a
coturn-readable copy of the issued certificate and add a renewal deploy hook so
it reloads on renewal.

### 5. Firewall

```sh
sudo ufw allow 80,443/tcp
sudo ufw allow 3478/udp && sudo ufw allow 3478/tcp
sudo ufw allow 5349/tcp            # TURNS
sudo ufw allow 49152:65535/udp     # relay range
```
