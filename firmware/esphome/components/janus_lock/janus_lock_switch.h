#pragma once
#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "janus_lock.h"
#ifdef USE_ESP32
#include <string>
namespace esphome {
namespace janus_lock {

// A lock setting exposed as a switch: writing sends `opcode`+01/00, and the state is
// refreshed from the handshake status flag bit `flag_bit` (-1 = no status feedback).
class JanusSettingSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(JanusLock *p) { this->parent_ = p; }
  void set_opcode(const std::string &o) { this->opcode_ = o; }
  void set_flag_bit(int b) { this->flag_bit_ = b; }
  void update_from_flags(uint8_t flags) {
    if (this->flag_bit_ >= 0) this->publish_state((flags >> this->flag_bit_) & 0x01);
  }
  void dump_config() override {}

 protected:
  void write_state(bool state) override;
  JanusLock *parent_{nullptr};
  std::string opcode_;
  int flag_bit_{-1};
};

}  // namespace janus_lock
}  // namespace esphome
#endif
