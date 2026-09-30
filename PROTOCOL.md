# Janus Lock — BLE Protocol (reverse-engineered)

Reverse-engineered from `com.exitec.januslock` v1.0.35 (versionCode 116), package name
Exitec "SmartLockV2". Source of truth: decompiled `MainActivity.java`, `LockSecurity.java`,
`model/Lock.java`, `utilities/*`.

This documents the protocol for **interoperating with a lock you own** (building an ESP32
bridge to replace the phone). Everything here is derived from the official app's own logic.

---

## 1. Transport — Nordic UART Service (NUS)

The lock is a Nordic (nRF) device. Communication is over a standard Nordic UART Service.

| Role | UUID |
|------|------|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| **RX** — write, phone → lock | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| **TX** — notify, lock → phone | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

- The app scans and matches the device by its **serial** (advertised name).
- Firmware updates use the Nordic DFU service (`DfuService`), opcode `enterDfu` = `f001`.

### Frame format

Every command is built as a **hex string** then converted 1:1 to bytes (`toByteArrayFromHexString`,
no checksum, no wrapping):

```
[ opcode : 2 bytes ] [ payload : N bytes ] [ 0x00 padding ... ]
```

The hex string is right-padded with `"00"` **until it is ≥ 40 hex chars = 20 bytes**.
So **every frame written to RX is exactly 20 bytes** (fits one BLE notification at MTU 23).

### Response frames (TX notifications)

```
[ opcode : 2 bytes ] [ status : 1 byte ] [ data ... ]
```

- `status` (byte index **[2]**): `0x00` = success, non-zero = failure.
- Responses echo the command opcode.

---

## 2. Opcode table (HEADER map)

| Command | Opcode | Notes |
|---|---|---|
| heartbeat | `aa55` | |
| handshake | `aabb` | base / H02 |
| handshakeH03 | `aa03` | model H03 |
| handshakeL100/200/300/700 | `aa1a`/`aa1b`/`aa1c`/`aa1d` | model variants |
| challenge1 | `aac1` | request nonce |
| challenge2 | `aac2` | send AES response |
| pair1 / pair2 | `0101` / `0102` | initial pairing |
| getTokenVersion | `0201` | |
| **unlock** | `0401` | + token |
| **lock** | `0402` | + token |
| getUnlockHistory1/2/3 | `0501`/`0502`/`0503` | |
| flushUnlockHistoryUntil | `0b01` | |
| updateAdminPasscode | `0601` | |
| updateClock | `0701` | |
| updatePasscode1 / updatePasscode2 | `0801` / `0802` | two-step add/update PIN |
| removePasscode | `0a01` | |
| **setNormalLock** (passage / free-handle) | `0901` | `01`=on, `00`=off |
| setLockSound | `0902` | |
| setAutoLock | `0903` | |
| setBreakInAlarm | `0904` | |
| setLockDirection | `0905` | |
| setUnlatch | `0906` | |
| setButtonEnabled | `0907` | |
| unbind | `1001` | |
| addFingerprint | `1101` | |
| updateFingerprint1/2 | `1201`/`1202` | |
| removeFingerprint | `1301` | |
| calibrationDoor* | `1401`–`1407` | mechanical calibration |
| enterDfu | `f001` | firmware update mode |
| sync | `ffff` | client-side marker (not sent) |
| pairKeypad / unpairKeypad | `bb04` / `bb05` | external keypad |

---

## 3. Session handshake (must run in this order after connecting)

> **Verified live** against an H03 lock, firmware 0.213, via an ESP32 (NimBLE).

1. **Connect** and enable **notifications** on TX (`6e400003`). RX (`6e400002`) is
   Write / Write-No-Response; TX is Notify (no indicate). Default MTU 23 is fine —
   every frame and response is 20 bytes.

2. **handshake** — always write opcode **`aabb`** + ASCII `"BIGTEARICE"` (regardless of
   model). The lock replies with **its own model opcode** (H03 → `aa03`, H02 → `aabb`, L* → `aa1a`…).
   ```
   send: aabb 42494754454152494345  (+ 00 pad to 20)
   recv: aa03 <hardwareId: 8 bytes> 00 <fwMin> <batt: 2B LE> <timerValid> <flags> <histStart:2 LE> <histEnd:2 LE>
   ```
   Response layout (20 bytes):
   | idx | field |
   |---|---|
   | [0..1] | response opcode (model: `aa03`=H03, `aabb`=H02, …) |
   | [2..9] | **`hardwareId`** (8 bytes) — equals the `serial`; session key seed |
   | [10] | firmware **major** |
   | [11] | firmware **minor** (→ "major.minor", e.g. `00 d5` = "0.213") |
   | [12..13] | battery raw, **LE** (`(resp[13]<<8)|resp[12]`) → `Utils.getBatteryLevel(raw, model)` |
   | [14] | **isTimerValid** (1 = lock clock already set) |
   | [15] | status flags bitmask (see below) |
   | [16..17] | unlockHistoryIdStart, **LE** |
   | [18..19] | unlockHistoryIdEnd, **LE** |

   Flags byte [15]: bit0 `isNormalLock` (passage/free-handle), bit1 `isLockSoundOn`,
   bit2 `isAutoLock`, bit3 `isBreakInAlarmOn`, bit4 `isUnlatchOn`, bit5 `isButtonEnabled`,
   bit7 `isCalibrated`.

3. **Challenge/response is gated on firmware major version** (`b = resp[10]`):
   - **`major < 3`: NO challenge.** After the handshake you send functional commands
     directly. (Our firmware 0.213 → no challenge. Sending `aac1` gets no reply.)
   - **`major >= 3`:** send **challenge1** (`aac1`) → reply `aac1 00 <nonce:4 @ [3..6]>`;
     compute the hash (see §4) and send **challenge2** (`aac2` + hash(4)); reply `aac2 00`
     ⇒ authenticated.

4. **updateClock** — only sent when `isTimerValid == 0`. `0701` + 8 time bytes, each `%02x`:
   `[ year/100, year%100, month(1-12), day, hour, minute, second, weekday ]`
   weekday = `Calendar.DAY_OF_WEEK - 1` (Sunday=0), value 7 mapped to 0.

After this, functional commands (unlock, setNormalLock, passcodes, …) are accepted.
Command responses echo the opcode with a status byte at **[2]** (`00` = success), e.g.
unlock reply `0401 00 …`, setNormalLock reply `0901 00 …`.

---

## 4. Challenge-response crypto (`LockSecurity`)

```
key       = hardwareId (8 bytes, from handshake response[2..9])  ++  0x00 * 8   # 16 bytes
block     = nonce (4 bytes, from challenge1 response[3..6])       ++  0x00 * 12  # 16 bytes
cipher    = AES-128-ECB, NoPadding, encrypt(block, key)
response  = cipher[0..3]        # first 4 bytes → sent in challenge2
```

- Note: `aesKey` exists on the Lock model but is **not used** for BLE crypto in this version.
  The entire session key is derived from `hardwareId`, which the lock transmits in the clear
  during the handshake. The session auth is therefore reproducible from the BLE session alone.

---

## 5. Command payloads

Payload is appended to the opcode, then the whole thing is zero-padded to 20 bytes.

- **unlock** `0401` + `masterToken` (hex string, appended verbatim)
  - admin: `lock.masterToken`; non-admin: `tokens[0].tokenRaw`
  - **Verified on H03**: reply `0401 00 …` = opened. Only works when the door is currently
    locked; unlocking an already-unlocked door replies `0401 ff …` (rejected).
- **lock** `0402` + `masterToken` (same rule)
  - **Verified on H03: not supported** — the lock sends no reply and does not act. This
    "Smart Handle" has no motorised remote lock; locking is done by **auto-lock** (setting the
    `setAutoLock` flag re-locks an open door) or by closing the door. Other hardware models may
    honour `0402`.
- **setNormalLock** `0901` + (`01` on / `00` off) — passage / free-handle mode
- **setUnlatch** `0906`, **setAutoLock** `0903`, **setLockSound** `0902`,
  **setBreakInAlarm** `0904`, **setLockDirection** `0905`, **setButtonEnabled** `0907`
  — each + `01`/`00`
- **updateAdminPasscode** `0601` + `len(%02x)` + each char `%02x` + raw passcode string
- **updatePasscode1** `0801` + `tokenRaw` (server-issued token blob)
- **updatePasscode2** `0802` + `tokenId`(2 bytes LE, `%02x%02x`) + `len(%02x)` + each digit `%02x`
- **removePasscode** `0a01` + `tokenId`(2 bytes LE)
- **removeFingerprint** `1301` + `tokenId`(2 bytes LE)
- **flushUnlockHistoryUntil** `0b01` + `id`(2 bytes LE)
- **updateClock** — see §3 step 5

---

## 6. Credentials needed to drive the lock from the ESP32

| Credential | Needed for | Where it comes from |
|---|---|---|
| `hardwareId` (8 bytes) | session handshake | **Free** — lock sends it in the handshake response |
| `masterToken` (hex) | unlock / lock | The `tokenRaw` of the token with **`tokenId == 1`** in the server profile (`MainActivity.setLock`). Also sent in cleartext in the `0401` frame on every real unlock. |
| `tokenRaw` (per PIN) | adding/updating a passcode (`updatePasscode1`) | Server-issued (`/api/v1/token/add`), or synthesizable — see layout below |

**Unlock, lock, and passage mode (`setNormalLock`) need only `hardwareId` + `masterToken`.**
Because `masterToken` (tokenId 1) is 18 bytes, the unlock frame is
`0401` + `masterToken` = **exactly 20 bytes** (no padding needed).

### How to get the credentials (server API — no root/snoop needed)

1. `POST https://api.janus-lock.com/api/v1/oauth/token` (form-urlencoded):
   `username`, `password`, `grant_type=password`, `client_id=SmartLockV2`,
   `client_secret=de0a5c792d1dbec62b9d9dd44a4096d1616c1d820c9735aecb8b9fed85ad00d1`
   → `access_token`.
2. `GET /api/v1/user/profile` with header `Authorization: Bearer <access_token>`
   → `locks[]`, each with `info.serial`, `info.hardwareModel`, and `tokens[]`
   (each token has `tokenId`, `tokenRaw`, `passcode`, `info.remainingUnlockCount`, `info.version`, `info.weekday`).
   - `masterToken` = the `tokenRaw` where `tokenId == 1`.
   - `hardwareModel` selects the handshake opcode (H03 → `aa03`).
   - `hardwareId` is **not** in the profile; the lock returns it during the handshake
     (response bytes [2..9]). It appears to equal the 8-byte `serial`, but read it live to be safe.

### `tokenRaw` layout (18 bytes / 36 hex) — decoded

```
| tokenId (2, LE) | count (1) | schedule / validity (13) | version (2, LE) |
```
Schematic (permanent master, tokenId 1): `01 00  FF  <13-byte schedule block>  01 00`
Schematic (one-time PIN,  tokenId 10):    `0A 00  01  <13-byte schedule block>  02 00`
(Concrete `tokenRaw` values are per-lock credentials — fetch your own from the profile API.)

- `count` = `remainingUnlockCount` clamped to a byte: `0xff` (255) ≈ permanent, `0x01` = one-time.
- The 13-byte schedule block encodes the validity window. The **server** fills it in from the
  `token/add` fields, so you push `tokenRaw` verbatim — you don't build the schedule yourself.
  Decoded by diffing tokens (verified against a real lock): a **weekday bitmask** byte
  (`0x7f` = whole week, e.g. `0x22` = Mon+Fri), a **daily time window** (start/end as `HH MM`;
  a permanent token is `00:00`–`23:59` → `00 00 … 17 3b`), and a **date range** (`yy mm dd`
  start/end, e.g. `1a 0a 01` = 2026-10-01). All limits off ⇒ the "always valid" pattern.
- `version` matches `token.info.version` and increments on edit.

**Adding / editing a PIN** the official way: `POST /api/v1/token/add` (`GrantAccessRightRequest`)
returns the new token incl. `tokenRaw`; then push over BLE with
`updatePasscode1` (`0801` + tokenRaw) followed by `updatePasscode2`
(`0802` + tokenId(2,LE) + len(1) + passcode digits each `%02x`).
**Removing a PIN**: `removePasscode` (`0a01` + tokenId(2,LE)) over BLE + `/api/v1/token/remove` on the server.

---

## 6b. Access rights — cloud + BLE (verified)

Two kinds of access:

**One-day code (offline TOTP).** `GET /api/v1/lock/{lockId}/totp?date=YYYYMMDD` (Bearer)
→ `{"totp":"NNNNN"}`. A 5-digit code the lock validates internally for that calendar day —
**no BLE needed** to create it. (Verified: distinct code per date.) The seed is server/lock side
(likely the unused `aesKey`); the app never computes it locally.

**Provisioned passcodes / fingerprints.** Created via `POST /api/v1/token/add`
(`GrantAccessRightRequest`), then pushed to the lock over BLE. Request fields:

| Field | Meaning / app option |
|---|---|
| `lockId` | target lock |
| `passcode` | PIN digits (e.g. "1234"); omit for fingerprint |
| `remainingUnlockCount` | `1` = one-time ("eenmalig"); large/255 = unlimited (permanent) |
| `timeValidFrom` / `timeValidTo` | daily time-of-day window ("beperkte toegangstijd"), `"HH:mm"` (e.g. `"09:00"`); empty = no limit |
| `dateValidFrom` / `dateValidTo` | date range ("beperkte toegangsdatum"), `"yyyy-MM-dd"` (e.g. `"2026-10-01"`); empty = no limit |
| `weekday` | `{monday..sunday}` booleans ("beperkte dagtoegang") |
| `fingerprintId` | set for a fingerprint token |
| `recipientUsername` | to share to another Janus account (not needed for own use) |

Permanent code = all limits off (`remainingUnlockCount=255`, full week, no date/time).
The response is a token incl. `tokenId` + `tokenRaw` (the 18-byte blob; see §6 layout — the
13-byte schedule block encodes weekday/time/date, and `count` byte = remaining uses).

Provisioning sequence (add/update a passcode):
1. `POST /api/v1/token/add` → get `tokenId`, `tokenRaw`, `passcode`.
2. BLE (after handshake): `updatePasscode1` (`0801` + tokenRaw) → then
   `updatePasscode2` (`0802` + tokenId(2,LE) + len(1) + each digit `%02x`).
3. `POST /api/v1/token/confirm-passcode-synced` (`lockId`, `tokenId`).

Remove: BLE `removePasscode` (`0a01` + tokenId(2,LE)) + `POST /api/v1/token/remove`.
Edit: `POST /api/v1/token/edit` (`EditAccessRightRequest`: same schedule fields + `tokenId`).
Fingerprint enrol: `token/add` with `fingerprintId`, then BLE `addFingerprint` (`1101`) +
`updateFingerprint1/2` (`1201`/`1202`) — enrolment is lock-driven (user touches sensor).

## 6c. Unlock history & one-time-code cleanup (verified)

The lock records every unlock in an on-board log with a rolling id range (reported in the handshake
status: `histStart`/`histEnd` at bytes [16..17]/[18..19]). One-time codes are enforced **by the lock**
(the `count` byte decrements; a used one-time code stops opening), but the **cloud** only learns of
usage — and thus removes/decrements used one-time tokens — when the history is uploaded to it. The
official app does this on every connection; an ESP32 bridge must do the same to keep cloud and lock
in sync.

**Read the history (BLE):**
1. `getUnlockHistory1` (`0501`) → reply `0501 <status> <count:2 LE @ [3]>`. `status@[2]==0`; `count` =
   number of entries waiting. If 0, done.
2. `getUnlockHistory2` (`0502`), repeated — each reply is **one entry** (or `status@[2]!=0` = no more):
   | idx | field |
   |---|---|
   | [2] | status (0 = an entry follows) |
   | [3..4] | **tokenId**, LE (which credential unlocked) |
   | [5] | result / type |
   | [6..12] | timestamp bytes → `"%02d%02d-%02d-%02d %02d:%02d:%02d"` = `YYYY-MM-DD HH:MM:SS` |
   | [13] | weekday (value **+1**) |
   | [14..15] | tokenVersion, LE |
   | [16..17] | **historyId**, LE |

   Keep sending `0502` until `status!=0`. (The app batches 10, then does step 3, uploads, repeats.)
3. `getUnlockHistory3` (`0503`) → end marker.
4. `flushUnlockHistoryUntil` (`0b01` + `id:2 LE`) → clears the lock's log up to `id`. Only flush
   **after** the entries are safely uploaded to the cloud.

**Upload to the cloud:** `POST /api/v1/lock/{lockId}/unlock-history`
```json
{ "histories": [ { "historyId": N, "date": "YYYY-MM-DD HH:MM:SS", "result": R,
                   "tokenId": T, "tokenVersion": V } ],
  "unlockHistoryIdStart": S, "unlockHistoryIdEnd": E }
```
The server reconciles access rights from this (decrements `remainingUnlockCount`, drops used one-time
tokens). The already-uploaded log can be read back with `GET /api/v1/lock/{lockId}/unlock-history`.

## 7. Backend (for reference)

- `SERVER_URL = https://api.janus-lock.com/`
- `CLIENT_ID = SmartLockV2`, `CLIENT_SECRET = de0a5c79…00d1` (OAuth-style; Retrofit)
- Local storage: Room + SQLCipher (`janus-lock-encrypted`) — stores the profile incl. masterToken/tokens.
