#include "janus_lock.h"
#ifdef USE_ESP32
#include "esphome/components/lock/lock.h"
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
  this->pending_cmd_ = hex;
  this->parent()->set_enabled(true);  // trigger a connection (or use the current one)
  this->flush_pending_();
}

void JanusLock::flush_pending_() {
  if (this->rx_handle_ == 0 || this->pending_cmd_.empty()) return;
  this->send_frame_(this->pending_cmd_, "cmd");
  this->pending_cmd_.clear();
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
    this->flush_pending_();
    this->set_timeout("disc", 2500, [this]() { this->parent()->set_enabled(false); });
    return;
  }
  if (data[0] == 0xaa && data[1] == 0x55) return;  // heartbeat ack
  bool ok = (data[2] == 0x00);
  if (data[0] == 0x04 && data[1] == 0x01) {
    ESP_LOGI(TAG, "unlock %s", ok ? "OK" : "FAIL");
    if (ok && this->lock_ != nullptr) ((lock::Lock *) this->lock_)->publish_state(lock::LOCK_STATE_UNLOCKED);
  } else if (data[0] == 0x04 && data[1] == 0x02) {
    ESP_LOGI(TAG, "lock %s", ok ? "OK" : "FAIL");
    if (ok && this->lock_ != nullptr) ((lock::Lock *) this->lock_)->publish_state(lock::LOCK_STATE_LOCKED);
  } else if (data[0] == 0x09) {
    ESP_LOGI(TAG, "setting 09%02x %s", data[1], ok ? "OK" : "FAIL");
  }
}

void JanusLock::publish_status_(const uint8_t *r, uint16_t len) {
  if (len < 20) return;
  uint8_t fw_major = r[10], fw_minor = r[11];
  uint16_t volt = (r[13] << 8) | r[12];
  bool timer_valid = r[14] == 1;
  uint8_t flags = r[15];
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

}  // namespace janus_lock
}  // namespace esphome
#endif
