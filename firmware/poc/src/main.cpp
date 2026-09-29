// ===========================================================================
// JanusLock PoC firmware
// Proves the reverse-engineered BLE protocol on real hardware:
//   scan -> connect -> handshake -> challenge/response (AES) -> unlock / etc.
// Exposes a tiny web UI over WiFi. This code is the seed of the final
// ESPHome external component.
//
// Target: Olimex ESP32-PoE2 (WiFi phase). Board env: esp32-poe.
// ===========================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <NimBLEDevice.h>
#include "mbedtls/aes.h"
#include "secrets.h"
#include "time.h"

// --- Nordic UART Service UUIDs -------------------------------------------
static const char* NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* NUS_RX       = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"; // write (phone->lock)
static const char* NUS_TX       = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"; // notify (lock->phone)

// Set true to send updateClock after auth (needed for time-limited tokens/history).
static const bool DO_UPDATE_CLOCK = true;

WebServer server(80);

// --- Notification plumbing ------------------------------------------------
static volatile bool     gNotifyReady = false;
static uint8_t           gNotifyBuf[64];
static volatile size_t   gNotifyLen = 0;
static String            gLog;  // accumulates a human-readable trace per action

static void logln(const String& s) { Serial.println(s); gLog += s; gLog += "\n"; }

static String toHex(const uint8_t* d, size_t n) {
  static const char* H = "0123456789abcdef";
  String s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += H[d[i] >> 4]; s += H[d[i] & 0xF]; }
  return s;
}

// Full advertisement payload as hex (name, mfg data, service UUIDs all in here).
static String advHex(NimBLEAdvertisedDevice& d) {
  return toHex(d.getPayload(), d.getPayloadLength());
}
// A device is our lock if its advertisement carries the Nordic (0x0059) manufacturer
// data with the lock's 4-byte serial prefix, e.g. "5900" + "d8a6f806".
static bool isTargetLock(NimBLEAdvertisedDevice& d) {
  String h = advHex(d);
  String pfx = String(LOCK_SERIAL).substring(0, 8); pfx.toLowerCase(); // first 4 bytes
  return h.indexOf("5900" + pfx) >= 0 || h.indexOf(pfx) >= 0;
}

static void notifyCB(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  size_t n = len > sizeof(gNotifyBuf) ? sizeof(gNotifyBuf) : len;
  memcpy(gNotifyBuf, data, n);
  gNotifyLen = n;
  gNotifyReady = true;
}

// Wait for a fresh notification; returns length (0 = timeout).
static size_t waitNotify(uint8_t* out, size_t outMax, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (!gNotifyReady) {
    if (millis() - t0 > timeoutMs) return 0;
    delay(5);
  }
  size_t n = gNotifyLen > outMax ? outMax : gNotifyLen;
  memcpy(out, gNotifyBuf, n);
  gNotifyReady = false;
  return n;
}

// --- Frame builder: opcode+payload hex -> 20-byte frame -------------------
static size_t buildFrame(const String& hexIn, uint8_t* out) {
  String hex = hexIn;
  while (hex.length() < 40) hex += "00";      // pad to 20 bytes
  size_t n = hex.length() / 2;
  for (size_t i = 0; i < n; i++)
    out[i] = (uint8_t) strtol(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return n;
}

static String asciiToHex(const char* s) {
  String h; for (const char* p = s; *p; ++p) { char b[3]; sprintf(b, "%02x", (uint8_t)*p); h += b; }
  return h;
}

// --- AES-128-ECB challenge response (LockSecurity) ------------------------
// key = hardwareId(8) || 00*8 ; block = nonce(4) || 00*12 ; out = enc[0..3]
static void calcHandshakeHash(const uint8_t hwId[8], const uint8_t nonce[4], uint8_t out4[4]) {
  uint8_t key[16] = {0}; memcpy(key, hwId, 8);
  uint8_t in[16]  = {0}; memcpy(in, nonce, 4);
  uint8_t enc[16];
  mbedtls_aes_context aes; mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, enc);
  mbedtls_aes_free(&aes);
  memcpy(out4, enc, 4);
}

// --- 8 clock bytes (admin format): century, yy, mon, day, hh, mm, ss, wday
static String clockPayloadHex() {
  struct tm t; if (!getLocalTime(&t, 500)) return ""; // no time yet
  int year = t.tm_year + 1900;
  int wday = t.tm_wday; // 0=Sun .. matches Calendar.get(DAY_OF_WEEK)-1
  char b[24];
  sprintf(b, "%02x%02x%02x%02x%02x%02x%02x%02x",
          year / 100, year % 100, t.tm_mon + 1, t.tm_mday,
          t.tm_hour, t.tm_min, t.tm_sec, wday);
  return String(b);
}

// --- One BLE session: connect, authenticate, run one command --------------
// Returns true on command success (response byte[2]==0). cmdHex without padding.
static bool bleSession(const String& cmdHex, const String& label) {
  gLog = "";
  logln("== session: " + label + " ==");

  // 1. Scan for the lock by advertised name (== serial).
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(45); scan->setWindow(45);
  logln("scanning...");
  NimBLEScanResults res = scan->start(6, false);
  NimBLEAdvertisedDevice targetDev;
  bool found = false;
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    String nm = d.getName().c_str();
    bool hit = isTargetLock(d);
    logln("  found: " + String(d.getAddress().toString().c_str()) + " name='" + nm + "' rssi=" + d.getRSSI() + (hit ? "  <== LOCK" : ""));
    if (hit) { targetDev = d; found = true; }
  }
  if (!found) { logln("LOCK NOT FOUND (name " LOCK_SERIAL "). Is the phone app disconnected?"); scan->clearResults(); return false; }
  logln("target: " + String(targetDev.getAddress().toString().c_str()));

  // 2. Connect.
  NimBLEClient* cli = NimBLEDevice::createClient();
  if (!cli->connect(&targetDev)) { logln("connect failed"); NimBLEDevice::deleteClient(cli); scan->clearResults(); return false; }
  logln("connected, mtu=" + String(cli->getMTU()));

  bool ok = false;
  do {
    NimBLERemoteService* svc = cli->getService(NUS_SERVICE);
    if (!svc) { logln("NUS service missing"); break; }
    NimBLERemoteCharacteristic* rx = svc->getCharacteristic(NUS_RX);
    NimBLERemoteCharacteristic* tx = svc->getCharacteristic(NUS_TX);
    if (!rx || !tx) { logln("RX/TX characteristic missing"); break; }
    bool noResp = rx->canWriteNoResponse();
    gNotifyReady = false;
    if (!tx->subscribe(true, notifyCB)) { logln("subscribe failed"); break; }
    delay(50);

    uint8_t frame[20]; uint8_t resp[64]; size_t rn;

    // --- handshake ---  (always send opcode aabb + "BIGTEARICE"; lock replies with
    // its model opcode, e.g. aa03 for H03. hardwareId = response bytes [2..9] = serial.)
    buildFrame(String("aabb") + asciiToHex("BIGTEARICE"), frame);
    logln("-> handshake " + toHex(frame, 20));
    rx->writeValue(frame, 20, !noResp);
    rn = waitNotify(resp, sizeof(resp), 4000);
    if (rn < 10) { logln("no/short handshake resp (" + String(rn) + ")"); break; }
    logln("<- " + toHex(resp, rn));
    uint8_t hwId[8]; memcpy(hwId, resp + 2, 8);
    uint8_t fwMajor    = (rn > 10) ? resp[10] : 0;
    bool    timerValid = (rn > 14) && (resp[14] == 1);
    logln("hardwareId=" + toHex(hwId, 8) + " fwMajor=" + fwMajor + " timerValid=" + timerValid);

    // --- challenge/response only on firmware major >= 3 ---
    if (fwMajor >= 3) {
      buildFrame("aac1", frame);
      logln("-> challenge1");
      rx->writeValue(frame, 20, !noResp);
      rn = waitNotify(resp, sizeof(resp), 4000);
      if (rn < 7 || resp[2] != 0) { logln("challenge1 failed resp=" + toHex(resp, rn)); break; }
      uint8_t nonce[4]; memcpy(nonce, resp + 3, 4);
      logln("nonce = " + toHex(nonce, 4));
      uint8_t hash[4]; calcHandshakeHash(hwId, nonce, hash);
      logln("hash  = " + toHex(hash, 4));
      buildFrame("aac2" + toHex(hash, 4), frame);
      logln("-> challenge2");
      rx->writeValue(frame, 20, !noResp);
      rn = waitNotify(resp, sizeof(resp), 4000);
      if (rn < 3 || resp[2] != 0) { logln("challenge2 failed resp=" + toHex(resp, rn)); break; }
      logln("AUTHENTICATED (challenge)");
    } else {
      logln("no challenge needed (fwMajor<3)");
    }

    // --- updateClock only if the lock's clock is not valid ---
    if (DO_UPDATE_CLOCK && !timerValid) {
      String cp = clockPayloadHex();
      if (cp.length()) {
        buildFrame("0701" + cp, frame);
        logln("-> updateClock " + cp);
        rx->writeValue(frame, 20, !noResp);
        rn = waitNotify(resp, sizeof(resp), 3000);
        logln("<- updateClock resp=" + toHex(resp, rn));
      }
    }

    // --- the actual command ---
    buildFrame(cmdHex, frame);
    logln("-> cmd " + toHex(frame, 20));
    rx->writeValue(frame, 20, !noResp);
    rn = waitNotify(resp, sizeof(resp), 5000);
    logln("<- " + toHex(resp, rn));
    ok = (rn >= 3 && resp[2] == 0);
    logln(ok ? "COMMAND OK" : "COMMAND FAILED");
  } while (false);

  cli->disconnect();
  NimBLEDevice::deleteClient(cli);
  scan->clearResults();
  return ok;
}

// --- Web UI ---------------------------------------------------------------
static const char* PAGE =
  "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>JanusLock</title><style>body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:0 12px}"
  "button{font-size:18px;padding:14px 18px;margin:6px 0;width:100%;border:0;border-radius:10px;color:#fff}"
  ".u{background:#1a7f37}.p1{background:#0969da}.p0{background:#6e7781}.s{background:#8250df}"
  "pre{background:#f6f8fa;padding:10px;border-radius:8px;white-space:pre-wrap;font-size:12px}</style>"
  "<h2>JanusLock &mdash; Kantoor</h2>"
  "<button class=u onclick=go('/unlock')>Ontgrendelen</button>"
  "<button class=p1 onclick=go('/passage/on')>Deurkruk-modus AAN</button>"
  "<button class=p0 onclick=go('/passage/off')>Deurkruk-modus UIT</button>"
  "<button class=s onclick=go('/status')>Status / handshake</button>"
  "<button onclick=go('/scan')>BLE scan</button>"
  "<pre id=o>klaar.</pre>"
  "<script>function go(u){document.getElementById('o').textContent='bezig...';"
  "fetch(u).then(r=>r.text()).then(t=>document.getElementById('o').textContent=t)"
  ".catch(e=>document.getElementById('o').textContent='err '+e)}</script>";

void handleRoot()      { server.send(200, "text/html", PAGE); }
void handleUnlock()    { bool ok = bleSession("0401" + String(MASTER_TOKEN), "unlock");      server.send(200, "text/plain; charset=utf-8", gLog); }
void handlePassageOn() { bool ok = bleSession("090101", "passage on");                        server.send(200, "text/plain; charset=utf-8", gLog); }
void handlePassageOff(){ bool ok = bleSession("090100", "passage off");                       server.send(200, "text/plain; charset=utf-8", gLog); }
// status = run handshake+challenge, then a harmless re-handshake as the "command" (shows raw frames)
void handleStatus()    { bool ok = bleSession(String("aabb") + asciiToHex("BIGTEARICE"), "status"); server.send(200, "text/plain; charset=utf-8", gLog); }

// Diagnostic: connect, dump characteristic properties, wait for unsolicited
// notifications, then try several handshake variants and report what answers.
void handleProbe() {
  gLog = "";
  logln("== probe ==");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults res = scan->start(6, false);
  NimBLEAdvertisedDevice targetDev; bool found = false;
  for (int i = 0; i < res.getCount(); i++) { NimBLEAdvertisedDevice d = res.getDevice(i); if (isTargetLock(d)) { targetDev = d; found = true; } }
  if (!found) { logln("lock not found"); scan->clearResults(); server.send(200, "text/plain; charset=utf-8", gLog); return; }

  NimBLEClient* cli = NimBLEDevice::createClient();
  cli->setConnectionParams(12, 12, 0, 200);
  if (!cli->connect(&targetDev)) { logln("connect failed"); NimBLEDevice::deleteClient(cli); scan->clearResults(); server.send(200,"text/plain; charset=utf-8",gLog); return; }
  logln("connected mtu=" + String(cli->getMTU()));

  NimBLERemoteService* svc = cli->getService(NUS_SERVICE);
  if (!svc) { logln("no NUS service; listing services:");
    // list all services/characteristics
    std::vector<NimBLERemoteService*>* svcs = cli->getServices(true);
    for (auto s : *svcs) logln("  svc " + String(s->getUUID().toString().c_str()));
    cli->disconnect(); NimBLEDevice::deleteClient(cli); scan->clearResults(); server.send(200,"text/plain; charset=utf-8",gLog); return; }

  NimBLERemoteCharacteristic* rx = svc->getCharacteristic(NUS_RX);
  NimBLERemoteCharacteristic* tx = svc->getCharacteristic(NUS_TX);
  if (!rx || !tx) { logln("rx/tx missing"); cli->disconnect(); NimBLEDevice::deleteClient(cli); scan->clearResults(); server.send(200,"text/plain; charset=utf-8",gLog); return; }
  logln(String("RX props: write=") + rx->canWrite() + " writeNR=" + rx->canWriteNoResponse());
  logln(String("TX props: notify=") + tx->canNotify() + " indicate=" + tx->canIndicate());

  gNotifyReady = false;
  bool sub;
  if (tx->canNotify()) sub = tx->subscribe(true, notifyCB, true);
  else                 sub = tx->subscribe(false, notifyCB, true);
  logln(String("subscribe=") + sub);
  delay(400);

  uint8_t resp[64]; size_t rn;
  logln("waiting 1500ms for unsolicited notify...");
  rn = waitNotify(resp, sizeof(resp), 1500);
  if (rn) logln("UNSOLICITED <- " + toHex(resp, rn)); else logln("(none)");

  const char* cands[] = {
    "aabb",  // base handshake opcode
    "aa03",  // H03 opcode
  };
  bool bigVariants[] = { true, false };
  bool noResp = rx->canWriteNoResponse();
  for (const char* op : cands) {
    for (bool big : bigVariants) {
      String cmd = String(op) + (big ? asciiToHex("BIGTEARICE") : "");
      uint8_t frame[20]; buildFrame(cmd, frame);
      gNotifyReady = false;
      bool w = rx->writeValue(frame, 20, !noResp);
      logln(String("-> ") + op + (big ? "+BIG " : "     ") + "write=" + w + " " + toHex(frame, 20));
      rn = waitNotify(resp, sizeof(resp), 2500);
      if (rn) { logln("   <- " + toHex(resp, rn) + "  *** ANSWER ***"); }
      else    { logln("   (no response)"); }
    }
  }
  cli->disconnect();
  NimBLEDevice::deleteClient(cli);
  scan->clearResults();
  server.send(200, "text/plain; charset=utf-8", gLog);
}

void handleScan() {
  gLog = "";
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults res = scan->start(6, false);
  logln("scan: " + String(res.getCount()) + " devices");
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    bool hit = isTargetLock(d);
    logln(String(d.getAddress().toString().c_str()) + "  '" + d.getName().c_str() + "'  rssi=" + d.getRSSI() +
          (hit ? "  <== LOCK" : "") + "\n    adv=" + advHex(d));
  }
  scan->clearResults();
  server.send(200, "text/plain; charset=utf-8", gLog);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\nJanusLock PoC booting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK  IP=" + WiFi.localIP().toString());
    configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC; lock uses this in admin path
  } else {
    Serial.println("\nWiFi FAILED");
  }

  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(200);

  server.on("/", handleRoot);
  server.on("/unlock", handleUnlock);
  server.on("/passage/on", handlePassageOn);
  server.on("/passage/off", handlePassageOff);
  server.on("/status", handleStatus);
  server.on("/probe", handleProbe);
  server.on("/scan", handleScan);
  server.begin();
  Serial.println("HTTP server up. Open http://" + WiFi.localIP().toString());

  ArduinoOTA.setHostname("januslock-poc");
  ArduinoOTA.begin();
  Serial.println("OTA ready (hostname januslock-poc).");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  delay(2);
}
