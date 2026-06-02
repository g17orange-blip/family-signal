# Deploying coturn + signaling on a single VPS

Tested target: Ubuntu 24.04 LTS, 1 vCPU / 1 GB RAM (e.g. Hetzner CX11, ~€4/mo).

## 1. Install packages

```sh
sudo apt update
sudo apt install -y coturn
```

Edit `/etc/default/coturn` and uncomment `TURNSERVER_ENABLED=1`.

## 2. Configure

```sh
sudo cp turnserver.conf.example /etc/turnserver.conf
sudo $EDITOR /etc/turnserver.conf   # fill the REPLACE_WITH_* placeholders
sudo systemctl enable --now coturn
```

## 3. Firewall

```sh
sudo ufw allow 3478/udp
sudo ufw allow 3478/tcp
sudo ufw allow 5349/tcp        # only if TLS is enabled
sudo ufw allow 49152:65535/udp # relay range, must match turnserver.conf
sudo ufw allow 8080/tcp        # signaling websocket
```

For production, put the signaling server behind nginx with a Let's Encrypt
certificate and serve it over `wss://` instead of plain `ws://`.

## 4. Signaling server

Copy the `signaling` binary (built from `server/signaling/`) to `/usr/local/bin/`
and create `/etc/systemd/system/signaling.service`:

```ini
[Unit]
Description=Signal messenger signaling server
After=network.target

[Service]
ExecStart=/usr/local/bin/signaling -addr :8080
Environment=SIGNAL_TOKEN=REPLACE_WITH_LONG_RANDOM_TOKEN
Restart=on-failure
User=nobody
DynamicUser=yes

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now signaling
```

## 5. Verify

From a client machine:

```sh
# STUN
turnutils_stunclient YOUR_VPS_IP

# TURN (replace user/password)
turnutils_uclient -u signaluser -w PASSWORD -v YOUR_VPS_IP
```

Both should report success without errors.
