# JanusLock

Open, self-hosted control for **Exitec / Janus** BLE smart locks (the "Smart Handle"
family, app package `com.exitec.januslock`). Reverse-engineered for interoperability so
you can drive a lock **you own** without the vendor app — e.g. remotely, or from
Home Assistant.

> This project talks to a lock you own, using your own Janus account credentials, exactly
> the way the official app does. It contains **no** vendor code and does not redistribute the app.

## What it can do

The BLE protocol is fully documented in [`PROTOCOL.md`](PROTOCOL.md). Supported operations:

- **Unlock / lock**
- **Passage mode** (free-handle / "normale deurkruk" — disables outside locking)
- **Passcode (PIN) management** — add/remove, one-time or permanent
- **Battery + status** (lock state flags, firmware version)
- **Access / unlock history**
- **Fingerprint** enrollment & removal, keypad pairing, calibration, DFU

## How it works

- The lock speaks a **Nordic UART Service** protocol (20-byte frames). Session auth is an
  **AES-128 challenge/response** keyed on the lock's `hardwareId` (which the lock reveals
  during the handshake). See `PROTOCOL.md`.
- Per-lock secrets (`masterToken`, passcode tokens) come from the Janus cloud profile
  (`GET /api/v1/user/profile`) after an OAuth login — the same call the app makes.

## Architecture / roadmap

| Phase | What | Status |
|---|---|---|
| 1. Protocol RE | Decompile app, document protocol | ✅ done (`PROTOCOL.md`) |
| 2. PoC firmware | ESP32 (NimBLE) proves handshake→unlock, tiny web UI | 🔧 in progress (`firmware/poc`) |
| 3. Final firmware | **ESPHome external component** on the Olimex ESP32-PoE2 by the door: native HA `lock`/`switch`/`sensor` entities | ⏳ planned |
| 4. (optional) HACS | Python HA integration + ESPHome BLE proxy, with per-user Janus login config-flow | ⏳ optional |

For a dedicated PoE device at the door, the **ESPHome external component** (phase 3) is the
target: always-on, no separate proxy, native Home Assistant integration. The PoC's C++
protocol code ports directly into it.

## Home Assistant integration (cloud side)

Two parts work together:

- **`firmware/esphome/`** — the ESPHome external component that drives the lock over BLE
  (unlock, passage, settings, battery/status). See below.
- **`custom_components/januslock/`** — a Home Assistant integration for the Janus **cloud**:
  sign in with your Janus account to get a per-lock **"Day code"** sensor (the offline 1-day
  passcode that works today) and a `januslock.get_day_code` service for any date.

Install the integration via HACS → *Custom repositories* → add `https://github.com/mbino/JanusLock`
as an **Integration**, install "Janus Lock", restart, then add it from *Settings → Devices &
Services* and sign in. (Provisioning custom PIN codes to the lock is planned next.)

## Repository layout

```
PROTOCOL.md              Full BLE + cloud-API protocol spec
firmware/esphome/        ESPHome external component (BLE) + device YAMLs
firmware/poc/            PlatformIO proof-of-concept firmware (protocol validation)
custom_components/januslock/   Home Assistant integration (cloud: 1-day codes)
private/                 Your fetched profile/credentials (gitignored)
```

`apk/` and `decompiled/` (the vendor app) are **gitignored** and never published.

## Building the PoC firmware

Requires [PlatformIO](https://platformio.org/).

```sh
cd firmware/poc
# create include/secrets.h (see below), then:
pio run -t upload      # flash over USB
pio device monitor     # watch the handshake trace
```

`include/secrets.h`:
```c
#define WIFI_SSID        "your-2.4GHz-ssid"
#define WIFI_PASS        "your-wifi-pass"
#define LOCK_SERIAL      "<serial from profile>"   // also the BLE adv name
#define HANDSHAKE_OPCODE "aa03"                     // H03; see PROTOCOL.md for other models
#define MASTER_TOKEN     "<tokenRaw of tokenId==1>"
```

Get `LOCK_SERIAL` / `MASTER_TOKEN` from your profile:
```sh
TOKEN=$(curl -s -X POST https://api.janus-lock.com/api/v1/oauth/token \
  -d username=YOU -d password=PASS -d grant_type=password \
  -d client_id=SmartLockV2 \
  -d client_secret=de0a5c792d1dbec62b9d9dd44a4096d1616c1d820c9735aecb8b9fed85ad00d1 \
  | python -c "import sys,json;print(json.load(sys.stdin)['access_token'])")
curl -s https://api.janus-lock.com/api/v1/user/profile -H "Authorization: Bearer $TOKEN"
```

## Legal / ethics

For use with locks you own or are authorized to manage. Reverse engineering for
interoperability. No vendor binaries are included or redistributed.
