#include "janus_lock.h"
#ifdef USE_ESP32
#include "esphome/components/lock/lock.h"
#include "esphome/core/application.h"
#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#include <map>
#endif
#include "janus_lock_lock.h"
#include "janus_lock_switch.h"

namespace esphome {
namespace janus_lock {

static const char *const TAG = "janus_lock";

// Voltage -> % table (from the app's Utils.getBatteryLevel, default/H0x model).
static const uint16_t BATT_TABLE[] = {
  853,851,850,849,847,846,845,844,842,841,840,839,837,836,835,834,832,831,830,829,
  827,826,825,824,822,821,820,819,817,816,815,814,812,811,810,809,807,806,805,804,
  802,801,800,799,797,796,795,794,792,791,790,789,787,786,785,784,782,781,780,779,
  777,776,775,774,772,771,770,769,767,766,765,764,762,761,760,759,757,756,755,754,
  752,751,750,749,747,746,745,744,742,741,739,730,722,713,705,696,688,679,671,662};

static std::string ascii_to_hex(const char *s) {
  static const char *H = "0123456789abcdef";
  std::string o;
  for (const char *p = s; *p; ++p) { o += H[(*p) >> 4]; o += H[(*p) & 0xF]; }
  return o;
}

void JanusLock::dump_config() {
  ESP_LOGCONFIG(TAG, "Janus Lock (masterToken %d chars, %d switches)", (int) this->master_token_.size(),
                (int) this->switches_.size());
}

void JanusLock::setup() {
  // On-demand connection to save the lock's batteries. The ble_client auto-connects once at
  // boot for an initial status read; after every handshake we disconnect (see on_notify_),
  // and only reconnect for a command or the periodic status refresh.
}

void JanusLock::queue_cmd_(const std::string &hex) {
  this->queue_.push_back(hex);
  this->parent()->set_enabled(true);  // trigger a connection (or use the current one)
  if (this->ready_)
    this->pump_();
}

// Send the next queued command (one at a time, waiting for each reply), or if the queue is
// empty, disconnect after a short delay (connect-on-demand).
void JanusLock::pump_() {
  if (!this->ready_ || this->rx_handle_ == 0 || this->awaiting_response_) return;
  if (this->queue_.empty()) {
    this->set_timeout("disc", 800, [this]() {
      if (this->queue_.empty()) this->parent()->set_enabled(false);
    });
    return;
  }
  this->cancel_timeout("disc");
  std::string cmd = this->queue_.front();
  this->queue_.erase(this->queue_.begin());
  this->awaiting_response_ = true;
  this->send_frame_(cmd, "cmd");
}

bool JanusLock::find_handles_() {
  auto *rx = this->parent()->get_characteristic(espbt::ESPBTUUID::from_raw(NUS_SVC),
                                                espbt::ESPBTUUID::from_raw(NUS_RX));
  auto *tx = this->parent()->get_characteristic(espbt::ESPBTUUID::from_raw(NUS_SVC),
                                                espbt::ESPBTUUID::from_raw(NUS_TX));
  if (rx == nullptr || tx == nullptr) {
    ESP_LOGW(TAG, "NUS characteristics not found");
    return false;
  }
  this->rx_handle_ = rx->handle;
  this->tx_handle_ = tx->handle;
  return true;
}

void JanusLock::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                    esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT:
      this->ready_ = false;
      this->hs_sent_ = false;
      this->awaiting_response_ = false;
      this->rx_handle_ = this->tx_handle_ = 0;
      this->node_state = espbt::ClientState::IDLE;
      ESP_LOGI(TAG, "disconnected");
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (!this->find_handles_()) break;
      esp_ble_gattc_register_for_notify(gattc_if, this->parent()->get_remote_bda(), this->tx_handle_);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->node_state = espbt::ClientState::ESTABLISHED;
      uint8_t v[2] = {0x01, 0x00};
      esp_gattc_descr_elem_t descr;
      uint16_t count = 1;
      auto s = esp_ble_gattc_get_descr_by_char_handle(gattc_if, this->parent()->get_conn_id(),
                                                      this->tx_handle_,
                                                      espbt::ESPBTUUID::from_uint16(0x2902).get_uuid(),
                                                      &descr, &count);
      if (s == ESP_OK && count > 0) {
        esp_ble_gattc_write_char_descr(gattc_if, this->parent()->get_conn_id(), descr.handle, sizeof(v),
                                       v, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
      } else if (!this->hs_sent_) {
        this->hs_sent_ = true;
        this->send_handshake_();
      }
      break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT:
      if (!this->hs_sent_) {
        this->hs_sent_ = true;
        this->send_handshake_();
      }
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == this->tx_handle_)
        this->on_notify_(param->notify.value, param->notify.value_len);
      break;

    default:
      break;
  }
}

void JanusLock::send_frame_(const std::string &hex_in, const char *label) {
  if (this->rx_handle_ == 0) {
    ESP_LOGW(TAG, "%s: not connected", label);
    return;
  }
  std::string hex = hex_in;
  while (hex.size() < 40) hex += "00";  // pad to 20 bytes
  uint8_t frame[20];
  for (int i = 0; i < 20; i++) frame[i] = (uint8_t) strtol(hex.substr(i * 2, 2).c_str(), nullptr, 16);
  ESP_LOGD(TAG, "-> %s %s", label, hex.c_str());
  esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                           this->rx_handle_, sizeof(frame), frame, ESP_GATT_WRITE_TYPE_NO_RSP,
                           ESP_GATT_AUTH_REQ_NONE);
}

void JanusLock::send_handshake_() { this->send_frame_("aabb" + ascii_to_hex("BIGTEARICE"), "handshake"); }

void JanusLock::update() {
  ESP_LOGD(TAG, "periodic wake for status refresh");
  this->parent()->set_enabled(true);
}

void JanusLock::on_notify_(const uint8_t *data, uint16_t len) {
  if (len < 3) return;
  char buf[8];
  std::string hx;
  size_t n = len < 20 ? len : 20;
  for (size_t i = 0; i < n; i++) { snprintf(buf, sizeof(buf), "%02x", data[i]); hx += buf; }
  ESP_LOGD(TAG, "<- %s", hx.c_str());

  if (data[0] == 0xaa && data[1] != 0x55) {  // handshake / status response (aa03/aabb/aa1x)
    this->publish_status_(data, len);
    this->ready_ = true;
    this->awaiting_response_ = false;
    this->pump_();  // run any queued commands, then disconnect
    return;
  }
  if (data[0] == 0xaa && data[1] == 0x55) {  // heartbeat ack
    this->awaiting_response_ = false;
    this->pump_();
    return;
  }
  // unlock-history read loop: getUnlockHistory1 (0501) -> 0502 x N -> getUnlockHistory3 (0503)
  if (this->reading_history_ && data[0] == 0x05) {
    if (data[1] == 0x01) {  // count at [3..4] LE
      uint16_t count = (data[2] == 0) ? ((data[4] << 8) | data[3]) : 0;
      ESP_LOGI(TAG, "history: %u entr(ies) to read", count);
      if (count > 0) this->send_frame_("0502", "cmd");
      else this->finish_history_();
      return;
    }
    if (data[1] == 0x02) {  // one entry, or status != 0 = no more
      if (data[2] == 0x00 && len >= 18) {
        uint16_t token_id = (data[4] << 8) | data[3];
        uint16_t token_ver = (data[15] << 8) | data[14];
        uint16_t hid = (data[17] << 8) | data[16];
        char e[176];
        snprintf(e, sizeof(e),
                 "%s{\"historyId\":%u,\"tokenId\":%u,\"result\":%u,\"tokenVersion\":%u,"
                 "\"date\":\"%02d%02d-%02d-%02d %02d:%02d:%02d\"}",
                 this->hist_count_ ? "," : "", hid, token_id, (unsigned) data[5], token_ver,
                 data[6], data[7], data[8], data[9], data[10], data[11], data[12]);
        this->history_json_ += e;
        this->hist_count_++;
        if (hid > this->hist_max_id_) this->hist_max_id_ = hid;
        this->send_frame_("0502", "cmd");  // next entry
      } else {
        this->send_frame_("0503", "cmd");  // no more -> finalize
      }
      return;
    }
    if (data[1] == 0x03) {  // end of history
      this->finish_history_();
      return;
    }
  }
  // a command response
  bool ok = (data[2] == 0x00);
  if (data[0] == 0x04 && data[1] == 0x01) {
    ESP_LOGI(TAG, "unlock %s", ok ? "OK" : "FAIL");
    if (ok && this->lock_ != nullptr) ((lock::Lock *) this->lock_)->publish_state(lock::LOCK_STATE_UNLOCKED);
  } else if (data[0] == 0x04 && data[1] == 0x02) {
    ESP_LOGI(TAG, "lock %s", ok ? "OK" : "FAIL");
    if (ok && this->lock_ != nullptr) ((lock::Lock *) this->lock_)->publish_state(lock::LOCK_STATE_LOCKED);
  } else if (data[0] == 0x09) {
    ESP_LOGI(TAG, "setting 09%02x %s", data[1], ok ? "OK" : "FAIL");
  } else if (data[0] == 0x08 || (data[0] == 0x0a && data[1] == 0x01)) {
    ESP_LOGI(TAG, "passcode %02x%02x %s", data[0], data[1], ok ? "OK" : "FAIL");
  }
  this->awaiting_response_ = false;
  this->pump_();  // next queued command, or disconnect
}

void JanusLock::publish_status_(const uint8_t *r, uint16_t len) {
  if (len < 20) return;
  uint8_t fw_major = r[10], fw_minor = r[11];
  uint16_t volt = (r[13] << 8) | r[12];
  bool timer_valid = r[14] == 1;
  uint8_t flags = r[15];
  this->hist_start_ = (r[17] << 8) | r[16];
  this->hist_end_ = (r[19] << 8) | r[18];
  int pct = 0;
  for (size_t i = 0; i < sizeof(BATT_TABLE) / sizeof(BATT_TABLE[0]); i++)
    if (volt >= BATT_TABLE[i]) { pct = 100 - (int) i; break; }
  ESP_LOGI(TAG, "status fw=%u.%u batt=%d%% (raw %u) timerValid=%d flags=0x%02x", fw_major, fw_minor, pct,
           volt, timer_valid, flags);
  if (this->battery_sensor_ != nullptr) this->battery_sensor_->publish_state(pct);
  if (this->bs_calibrated_ != nullptr) this->bs_calibrated_->publish_state(flags & 0x80);
  for (auto *sw : this->switches_) sw->update_from_flags(flags);
}

void JanusLock::unlock() { this->queue_cmd_("0401" + this->master_token_); }
void JanusLock::lock_it() { this->queue_cmd_("0402" + this->master_token_); }
void JanusLock::queue_setting(const std::string &opcode, bool on) {
  this->queue_cmd_(opcode + (on ? "01" : "00"));
}

// Provision a passcode: updatePasscode1 (0801 + tokenRaw), then updatePasscode2
// (0802 + tokenId(2,LE) + len(1) + each digit as %02x). tokenRaw comes from /api/v1/token/add.
void JanusLock::provision_passcode(std::string token_raw, int token_id, std::string passcode) {
  char b[8];
  this->queue_cmd_("0801" + token_raw);
  std::string f2 = "0802";
  snprintf(b, sizeof(b), "%02x%02x", token_id & 0xff, (token_id >> 8) & 0xff);
  f2 += b;
  snprintf(b, sizeof(b), "%02x", (int) passcode.size());
  f2 += b;
  for (char c : passcode) {
    snprintf(b, sizeof(b), "%02x", (uint8_t) c);
    f2 += b;
  }
  this->queue_cmd_(f2);
  ESP_LOGI(TAG, "provisioning passcode tokenId=%d (%d digits)", token_id, (int) passcode.size());
}

// Remove a passcode: removePasscode (0a01 + tokenId(2,LE)).
void JanusLock::remove_passcode(int token_id) {
  char b[8];
  std::string f = "0a01";
  snprintf(b, sizeof(b), "%02x%02x", token_id & 0xff, (token_id >> 8) & 0xff);
  f += b;
  this->queue_cmd_(f);
  ESP_LOGI(TAG, "removing passcode tokenId=%d", token_id);
}

// Read the lock's unlock history. Kicks off the 0501 -> 0502.. -> 0503 loop; the responses are
// handled in on_notify_, and finish_history_() hands the collected entries to Home Assistant.
void JanusLock::read_history() {
  this->history_json_ = "[";
  this->hist_count_ = 0;
  this->hist_max_id_ = 0;
  this->reading_history_ = true;
  this->queue_cmd_("0501");
  ESP_LOGI(TAG, "reading unlock history");
}

// Clear the lock's stored history up to (and including) history id `id` (flushUnlockHistoryUntil).
void JanusLock::flush_history(int id) {
  char b[8];
  std::string f = "0b01";
  snprintf(b, sizeof(b), "%02x%02x", id & 0xff, (id >> 8) & 0xff);
  f += b;
  this->queue_cmd_(f);
  ESP_LOGI(TAG, "flushing history up to id=%d", id);
}

// Close the JSON array and push the history to Home Assistant via a service call (which, unlike an
// entity state, has no length limit). The integration uploads it to the Janus cloud and flushes.
void JanusLock::finish_history_() {
  this->history_json_ += "]";
#ifdef USE_API_HOMEASSISTANT_SERVICES
  api::CustomAPIDevice dev;
  std::map<std::string, std::string> data;
  data["node"] = App.get_name();
  data["entries"] = this->history_json_;
  data["max_id"] = std::to_string(this->hist_max_id_);
  data["count"] = std::to_string(this->hist_count_);
  data["id_start"] = std::to_string(this->hist_start_);
  data["id_end"] = std::to_string(this->hist_end_);
  dev.call_homeassistant_service("januslock.ingest_history", data);
#endif
  ESP_LOGI(TAG, "history: collected %d entr(ies), handed to HA", this->hist_count_);
  this->reading_history_ = false;
  this->awaiting_response_ = false;
  this->pump_();
}

}  // namespace janus_lock
}  // namespace esphome
#endif
