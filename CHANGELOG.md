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
