#include "janus_lock_switch.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

void JanusSettingSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) this->parent_->queue_setting(this->opcode_, state);
  this->publish_state(state);  // optimistic; corrected on the next status read
}

}  // namespace janus_lock
}  // namespace esphome
#endif
