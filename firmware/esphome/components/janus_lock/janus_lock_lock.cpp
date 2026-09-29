#include "janus_lock_lock.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

void JanusLockLock::control(const lock::LockCall &call) {
  auto state = call.get_state();
  if (!state.has_value() || this->parent_ == nullptr) return;
  if (*state == lock::LOCK_STATE_UNLOCKED)
    this->parent_->unlock();
  else if (*state == lock::LOCK_STATE_LOCKED)
    this->parent_->lock_it();
}

}  // namespace janus_lock
}  // namespace esphome
#endif
