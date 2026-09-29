#pragma once
#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "janus_lock.h"
#ifdef USE_ESP32
#include <string>
namespace esphome {
namespace janus_lock {

// A momentary action button. On this hardware (H03 "Smart Handle") there is no remote
// lock command; unlocking opens the handle and auto-lock re-locks it, so an unlock button
// matches the app. `action` is "unlock" (default) or "lock" (only useful on models that
// support the 0402 command).
class JanusButton : public button::Button, public Component {
 public:
  void set_parent(JanusLock *p) { this->parent_ = p; }
  void set_action(const std::string &a) { this->action_ = a; }
  void dump_config() override {}

 protected:
  void press_action() override;
  JanusLock *parent_{nullptr};
  std::string action_;
};

}  // namespace janus_lock
}  // namespace esphome
#endif
