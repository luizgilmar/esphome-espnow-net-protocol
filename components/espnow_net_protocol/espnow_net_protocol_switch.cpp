#include "espnow_net_protocol_switch.h"

#include "esphome/core/log.h"

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol.switch";

void EspNowNetProtocolSwitch::setup() {
  if (parent_ == nullptr) {
    mark_failed();
    return;
  }
  const bool enabled = get_initial_state_with_restore_mode().value_or(true);
  parent_->set_runtime_enabled(enabled);
  publish_state(enabled);
}

void EspNowNetProtocolSwitch::dump_config() {
  LOG_SWITCH("", "ESP-NOW NetProtocol runtime", this);
}

void EspNowNetProtocolSwitch::write_state(bool state) {
  if (parent_ == nullptr) return;
  parent_->set_runtime_enabled(state);
  publish_state(state);
  ESP_LOGI(TAG, "Runtime %s", state ? "enabled" : "disabled");
}

}  // namespace espnow_net_protocol
}  // namespace esphome
