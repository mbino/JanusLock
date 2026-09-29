#include "janus_lock_button.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

void JanusButton::press_action() {
  if (this->parent_ == nullptr) return;
  if (this->action_ == "lock")
    this->parent_->lock_it();
  else
    this->parent_->unlock();
}

}  // namespace janus_lock
}  // namespace esphome
#endif
