# signal

A lightweight, peer-to-peer voice/video/text messenger. Built as an
alternative to Telegram for low-end Windows hardware (2 GB RAM, Intel
Celeron) where Electron-based messengers crawl.

- **Native client** in Qt 6 / C++. No Electron, no embedded browser.
- **WebRTC media** via GStreamer `webrtcbin`. Audio + video + text over the
  same DataChannel.
- **Tiny signaling server** in Go. Helps two peers find each other; never
  sees or stores message content.
- **Optional TURN relay** (`coturn`) for the ~10 % of NAT setups where
  direct P2P fails.
- **Local-only history** in SQLite. The server has no chat DB.

Expected resource footprint on the target Celeron / 2 GB box:
| State | RAM | CPU |
|---|---|---|
| Idle, in tray | 20–40 MB | ~0 % |
| Active chat | 40–90 MB | <1 % |
| 480p video call | 150–280 MB | 25–50 % |

For comparison, Telegram Desktop idles around 200 MB and consumes
400–700 MB during a video call on the same hardware.

---

## Repository layout

```
signal/
├── server/
│   ├── signaling/           Go WebSocket signaling server
│   └── turn/                coturn config + deployment notes
└── client/                  Qt6 + GStreamer client
    ├── CMakeLists.txt
    ├── resources/
    └── src/
```

The server and client are completely separate builds with no shared
sources. Either can be replaced independently.

---

## Building the signaling server

Requires Go 1.22+.

```sh
cd server/signaling
go build -o signaling .
./signaling -addr :8080         # SIGNAL_TOKEN env var must be set
```

The binary is statically linked, ~9 MB, and runs on any Linux/macOS/Windows
amd64/arm64 host without runtime dependencies.

---

## Installing the client (prebuilt)

The easiest path — download the artifact for your platform from the
[Releases page](https://github.com/g17orange-blip/family-signal/releases),
no toolchain required:

| Platform | Asset | How to run |
|---|---|---|
| Windows | `signal-setup.exe` | Run the installer, follow the prompts. |
| macOS (Apple Silicon) | `signal-macos-arm64.dmg` | Open the dmg, drag Signal to Applications. |
| macOS (Intel) | `signal-macos-x86_64.dmg` | Same. |
| Linux (any distro) | `signal-x86_64.AppImage` | `chmod +x signal-x86_64.AppImage && ./signal-x86_64.AppImage` |

All bundles are self-contained (Qt + GStreamer included). On first launch the
[setup wizard](#configuring-the-client) asks for your invite code.

macOS and Linux builds are **unsigned**, so the OS may warn on first launch:
- macOS: right-click the app → **Open** (once), or `xattr -dr com.apple.quarantine /Applications/signal-client.app`.
- Linux: the AppImage needs FUSE (`sudo apt install libfuse2` on older setups).

To build from source instead, read on.

## Building the client

### Linux (Arch)

```sh
sudo pacman -S qt6-base qt6-websockets qt6-tools \
               cmake pkgconf \
               gstreamer gst-plugins-base gst-plugins-good \
               gst-plugins-bad gst-plugins-ugly gst-libav

cmake -B build -S client -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/signal-client
```

### macOS

```sh
brew install cmake qt gstreamer
cmake -B build -S client -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j
./build/signal-client.app/Contents/MacOS/signal-client
```

If GStreamer was installed from the official `.pkg` instead of Homebrew,
prepend its pkg-config path:

```sh
export PKG_CONFIG_PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig
```

### Windows

Install dependencies via [MSYS2](https://www.msys2.org/):

```sh
pacman -S mingw-w64-x86_64-{qt6-base,qt6-websockets,cmake,gcc,pkgconf,\
gstreamer,gst-plugins-base,gst-plugins-good,gst-plugins-bad,gst-plugins-ugly,gst-libav}

cmake -B build -S client -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`windeployqt` packages a runnable folder for distribution:

```sh
windeployqt --release build/signal-client.exe
```

---

## Deploying the server

A cheap 1 vCPU / 1 GB VPS with a domain name is sufficient. One command sets
up everything — coturn (STUN + TURN), the signaling server, nginx + Let's
Encrypt TLS, systemd units and the firewall:

```sh
curl -fsSL https://raw.githubusercontent.com/g17orange-blip/family-signal/main/server/install.sh \
  | sudo DOMAIN=signal.example.com EMAIL=you@example.com bash
```

It then prints, for each family member, a ready `config.json` and a one-line
**invite code** to paste into the client's first-run wizard. See
[`server/turn/README.deploy.md`](server/turn/README.deploy.md) for details,
verification, and the manual fallback procedure.

Once running, the server hosts:
- `wss://your-domain/ws` — signaling WebSocket (TLS via nginx)
- `udp/tcp 3478` — STUN, plain TURN
- `tcp 5349` — TURNS (TURN over TLS)
- `udp 49152–65535` — TURN media relay (only used when direct P2P fails)

---

## Configuring the client

On first launch the client shows a **setup wizard**: paste the one-line invite
code that `server/install.sh` printed for this member and it writes the config
automatically. (There's an "advanced" section for entering the fields by hand.)

The wizard saves to:

- Linux:   `~/.config/signal/config.json`
- macOS:   `~/Library/Application Support/signal/config.json`
- Windows: `%APPDATA%\signal\config.json`

The invite code is just `base64(config.json)`. The file it produces looks like:

```json
{
  "user_id": "alice",
  "display_name": "Alice",
  "signaling_url": "wss://your-vps.example.com/ws",
  "signaling_token": "LONG_RANDOM_SHARED_SECRET",
  "stun_url": "stun:your-vps.example.com:3478",
  "peers": [
    { "id": "grandpa", "name": "Дедушка" }
  ],
  "turn": {
    "url": "turn:your-vps.example.com:3478",
    "username": "signaluser",
    "password": "LONG_RANDOM_PASSWORD"
  }
}
```

Both clients share the same `signaling_token` and TURN credentials. Each
client's `user_id` matches the other's `peers[].id`.

---

## Architecture in one diagram

```
   ┌──────────────┐                            ┌──────────────┐
   │   Client A   │                            │   Client B   │
   │ (Arch Linux) │                            │ (Windows 10) │
   │              │                            │              │
   │ Qt6 UI       │                            │ Qt6 UI       │
   │ GStreamer    │                            │ GStreamer    │
   │ webrtcbin    │                            │ webrtcbin    │
   └──────┬───────┘                            └──────┬───────┘
          │  (1) WebSocket: hello, ICE, SDP            │
          │      "Where is B?"  "Here is my SDP"       │
          └──────────────┬─────────────────────────────┘
                         ▼
                ┌────────────────────┐
                │  Signaling (Go)    │  ← stateless relay, ~30 MB RAM
                │     + coturn       │  ← TURN only when NAT pinhole fails
                └────────────────────┘

   (2) After handshake, audio/video/text flow DIRECTLY between A and B
       over UDP, end-to-end encrypted via DTLS-SRTP.
       The server sees zero media bytes.
```

---

## Status — v0.3.0

Tested end-to-end (macOS ↔ Windows) against a live server:

- ✅ Text messaging without calls (background DataChannel sessions),
  offline queue with delivery acks and read receipts, history encrypted
  at rest, paginated conversations.
- ✅ Voice and video calls with accept/decline, ringtone, self-view, echo
  cancellation (Windows/Linux), busy handling, call log pills in the chat
  with unread badges for missed/declined calls.
- ✅ Unstable-network resilience: Opus inband FEC + video NACK/RTX,
  lip-sync (RTCP latency handling), "connection lost" status with
  automatic redial and silent re-accept after a dropped call.
- ✅ One-command server install, invite-code onboarding, prebuilt
  installers for Windows/macOS/Linux (macOS bundle fixed in 0.3.1 —
  earlier dmgs shipped a broken GStreamer relocation).

Planned next (v0.4.0): photo/video sending over the same encrypted
DataChannel; macOS echo cancellation; remote peer public-key pinning.
