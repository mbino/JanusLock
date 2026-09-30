## v0.9.1
- Made the custom card load more reliably: it no longer errors if its script is loaded twice, and the integration now cache-busts the card URL with its version so an updated card isn't served stale from the browser cache. (Fixes an occasional "Configuration error" in place of the card until a hard refresh.)

## v0.9.0
- Added a **custom Lovelace card** for managing the lock from a normal dashboard — no more Developer Tools. It has a form to add a code (with the one-time / time-window / date-range / weekday options), a list of existing PIN codes each with a Remove button, the enrolled fingerprints, and the recent unlocks with a "Sync now" button. The integration serves and auto-loads the card itself, so just add a card of type `custom:janus-lock-card` to a dashboard (the entity is auto-detected).

## v0.8.0
- Unlock history & self-cleaning one-time codes. A new `januslock.sync_history` action reads the lock's unlock log over BLE and uploads it to the Janus cloud, exactly like the app does. The cloud then reconciles access — so **used one-time codes get cleaned up automatically** and the counts stay correct. The lock's on-board log is cleared after a successful upload, and the recent unlocks appear as a `recent_unlocks` attribute on the Day code sensor. Tip: run `sync_history` once a day with an automation.
- Requires the ESPHome device's "Allow the device to perform Home Assistant actions" option to be enabled (so the lock can hand its history to Home Assistant).

## v0.7.1
- Removing a PIN is now reliable: if the code is taken off the lock but the Janus cloud removal fails, the action retries and then reports an error instead of silently leaving the code in the cloud (which previously let the lock and cloud drift out of sync). It also returns whether the lock and cloud parts each succeeded.
- The "Day code" sensor now lists the lock's access more clearly: a **pins** list (with each code's type — permanent / one-time — and any time, date or weekday limits) and a separate **fingerprints** list, instead of one mixed "passcodes" attribute.

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
