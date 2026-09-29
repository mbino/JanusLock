#include "janus_lock_switch.h"
#ifdef USE_ESP32
namespace esphome {
namespace janus_lock {

void JanusPassageSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) this->parent_->set_passage(state);
  this->publish_state(state);
}

}  // namespace janus_lock
}  // namespace esphome
#endif
