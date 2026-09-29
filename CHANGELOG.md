## v0.7.0
- PIN codes can now be time-limited, matching the last two options in the vendor app. `januslock.add_pin` gained a daily time window (`time_from`/`time_to`, e.g. 09:00–17:00) and a valid-from/until date range (`date_from`/`date_to`). Combine them freely with the existing one-time and weekday options; leave them empty for a code with no time restriction.

## v0.6.0
- PIN-code management, end to end. The ESPHome firmware gained BLE services to provision and remove passcodes (updatePasscode1/2, removePasscode), and the Home Assistant integration gained `januslock.add_pin` (create a code — permanent or one-time, optional weekdays — on the cloud and push it to the lock) and `januslock.remove_pin`. The lock's current passcodes are listed as an attribute on the Day code sensor.

## v0.5.0
- Added a Home Assistant custom integration (HACS-installable) for the Janus cloud side. Sign in with your Janus app account to get a per-lock "Day code" sensor — the offline 1-day passcode that works on the current day — plus a `januslock.get_day_code` service to fetch the code for any date. (Provisioning custom PIN codes to the lock is the next step.)

## v0.4.0
- Replaced the lock entity with an "Ontgrendelen" (unlock) button, matching the app and how the H03 "Smart Handle" actually works: there is no remote lock command — unlocking opens the handle and auto-lock (or closing the door) re-locks it. Unlocking only works when the door is currently locked.

## v0.3.0
- ESPHome integration now runs on real hardware in Home Assistant: unlock, passage mode, battery and status all working, built from the public repo via the ESPHome add-on.
- Connect-on-demand: the ESP32 only connects to the lock for a command or a periodic status check, then disconnects — so it does not drain the lock's batteries.
- Added switches for the lock's settings — auto-lock, lock sound, break-in alarm, button lock, unlatch, and passage mode — each reflecting the lock's current state and toggling it.

## v0.2.0
- Proved the protocol end-to-end on real hardware: the ESP32 by the door unlocks the lock and toggles passage (free-handle) mode, driven entirely over WiFi.
- Confirmed the handshake is always sent as opcode `aabb` + "BIGTEARICE"; the lock answers with its model opcode (H03 → `aa03`).
- Confirmed the AES challenge/response is only used on firmware major version ≥ 3; this lock (firmware 0.213) needs no challenge, and `hardwareId` equals the serial.
- Decoded the full handshake status response (firmware, battery, clock-valid flag, state bitmask, history id range).
- Added wireless (OTA) firmware updates so the module never needs unplugging again.

## v0.1.0
- Initial reverse-engineering of the Janus / Exitec smart-lock BLE protocol, documented in `PROTOCOL.md`.
- Decoded: Nordic UART transport, 20-byte frame format, AES-128 challenge/response session auth, and the full command opcode table (unlock, lock, passage mode, PIN management, fingerprints, history, calibration, DFU).
- Documented the cloud API route (OAuth login + profile) to obtain per-lock secrets (masterToken, passcode tokens) without root or packet capture.
- Added proof-of-concept ESP32 (Olimex ESP32-PoE2) firmware that connects over BLE, performs the handshake and challenge, and can unlock / toggle passage mode, with a small web UI.
