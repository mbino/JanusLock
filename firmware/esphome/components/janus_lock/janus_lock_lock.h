#pragma once
#include "esphome/core/component.h"
#include "esphome/components/lock/lock.h"
#include "janus_lock.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

class JanusLockLock : public lock::Lock, public Component {
 public:
  void set_parent(JanusLock *p) { this->parent_ = p; }
  void dump_config() override {}

 protected:
  void control(const lock::LockCall &call) override;
  JanusLock *parent_{nullptr};
};

}  // namespace janus_lock
}  // namespace esphome
#endif
