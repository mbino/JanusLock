#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>
#include <string>
#include <vector>

namespace esphome {
namespace janus_lock {

namespace espbt = esphome::esp32_ble_tracker;

class JanusLockLock;       // fwd
class JanusSettingSwitch;  // fwd

// Nordic UART Service
static const char *const NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

class JanusLock : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void update() override;  // periodic status refresh (re-handshake)
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // config setters
  void set_master_token(const std::string &t) { this->master_token_ = t; }
  void set_battery_sensor(sensor::Sensor *s) { this->battery_sensor_ = s; }
  void set_bs_calibrated(binary_sensor::BinarySensor *s) { this->bs_calibrated_ = s; }
  void set_lock(JanusLockLock *l) { this->lock_ = l; }
  void add_setting_switch(JanusSettingSwitch *s) { this->switches_.push_back(s); }

  // actions
  void unlock();
  void lock_it();
  void queue_setting(const std::string &opcode, bool on);  // e.g. "0903" + on
  void provision_passcode(std::string token_raw, int token_id, std::string passcode);
  void remove_passcode(int token_id);
  void read_history();          // read the lock's unlock history, then hand it to HA
  void flush_history(int id);   // clear the lock's history up to (and incl.) history id
  bool is_ready() const { return this->ready_; }

 protected:
  bool find_handles_();
  void send_frame_(const std::string &hex, const char *label);
  void send_handshake_();
  void on_notify_(const uint8_t *data, uint16_t len);
  void publish_status_(const uint8_t *r, uint16_t len);
  void queue_cmd_(const std::string &hex);
  void pump_();  // send the next queued command, or disconnect when done
  void finish_history_();  // close the JSON, push it to HA, resume normal flow

  std::string master_token_;
  sensor::Sensor *battery_sensor_{nullptr};
  binary_sensor::BinarySensor *bs_calibrated_{nullptr};
  JanusLockLock *lock_{nullptr};
  std::vector<JanusSettingSwitch *> switches_;

  uint16_t rx_handle_{0};
  uint16_t tx_handle_{0};
  bool ready_{false};
  bool hs_sent_{false};              // handshake sent this connection (dedupe)
  bool awaiting_response_{false};    // a command was written, waiting for its reply
  std::vector<std::string> queue_;   // pending command hex frames (no padding), sent in order

  bool reading_history_{false};      // mid unlock-history read loop (0501/0502/0503)
  std::string history_json_;         // accumulated entries as a JSON array
  uint16_t hist_max_id_{0};          // highest history id seen (for flush)
  int hist_count_{0};                // number of entries collected
  uint16_t hist_start_{0};           // lock's history id range (from handshake status)
  uint16_t hist_end_{0};
};

}  // namespace janus_lock
}  // namespace esphome
#endif
