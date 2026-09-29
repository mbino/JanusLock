#pragma once
#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "janus_lock.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

class JanusPassageSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(JanusLock *p) { this->parent_ = p; }
  void dump_config() override {}

 protected:
  void write_state(bool state) override;
  JanusLock *parent_{nullptr};
};

}  // namespace janus_lock
}  // namespace esphome
#endif
