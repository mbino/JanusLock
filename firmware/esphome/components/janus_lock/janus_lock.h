#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_ESP32
#include <esp_gattc_api.h>
#include <string>

namespace esphome {
namespace janus_lock {

namespace espbt = esphome::esp32_ble_tracker;

class JanusLockLock;       // fwd
class JanusPassageSwitch;  // fwd

// Nordic UART Service
static const char *const NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

class JanusLock : public PollingComponent, public ble_client::BLEClientNode {
  enum Pending { P_NONE, P_UNLOCK, P_LOCK, P_PASS_ON, P_PASS_OFF };

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
  void set_bs_normal_lock(binary_sensor::BinarySensor *s) { this->bs_normal_lock_ = s; }
  void set_bs_auto_lock(binary_sensor::BinarySensor *s) { this->bs_auto_lock_ = s; }
  void set_bs_lock_sound(binary_sensor::BinarySensor *s) { this->bs_lock_sound_ = s; }
  void set_bs_calibrated(binary_sensor::BinarySensor *s) { this->bs_calibrated_ = s; }
  void set_lock(JanusLockLock *l) { this->lock_ = l; }
  void set_passage_switch(JanusPassageSwitch *s) { this->passage_switch_ = s; }

  // actions
  void unlock();
  void lock_it();
  void set_passage(bool on);
  bool is_ready() const { return this->ready_; }

 protected:
  bool find_handles_();
  void send_frame_(const std::string &hex, const char *label);
  void send_handshake_();
  void on_notify_(const uint8_t *data, uint16_t len);
  void publish_status_(const uint8_t *r, uint16_t len);
  void flush_pending_();

  std::string master_token_;
  sensor::Sensor *battery_sensor_{nullptr};
  binary_sensor::BinarySensor *bs_normal_lock_{nullptr};
  binary_sensor::BinarySensor *bs_auto_lock_{nullptr};
  binary_sensor::BinarySensor *bs_lock_sound_{nullptr};
  binary_sensor::BinarySensor *bs_calibrated_{nullptr};
  JanusLockLock *lock_{nullptr};
  JanusPassageSwitch *passage_switch_{nullptr};

  uint16_t rx_handle_{0};
  uint16_t tx_handle_{0};
  bool ready_{false};
  bool hs_sent_{false};       // handshake sent this connection (dedupe)
  Pending pending_{P_NONE};   // queued command to run once connected
};

}  // namespace janus_lock
}  // namespace esphome
#endif
