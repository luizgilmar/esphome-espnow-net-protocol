#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "espnow_net_protocol.h"

namespace esphome {
namespace espnow_net_protocol {

template<typename... Ts>
class EspNowSendCommandAction final : public Action<Ts...> {
 public:
  explicit EspNowSendCommandAction(EspNowNetProtocolComponent *parent)
      : parent_(parent) {}

  void set_peer(const char *value) { peer_ = value; }
  void set_device(const char *value) { device_ = value; }
  void set_resource(const char *value) { resource_ = value; }
  void set_command(const char *value) { command_ = value; }
  void set_payload(const char *value) { payload_ = value; }
  void set_timeout(uint32_t value) { timeout_ms_ = value; }

  void play(const Ts &...args) override {
    (void) sizeof...(args);
    if (!parent_->send_command(peer_, device_, resource_, command_, payload_,
                               timeout_ms_, millis()))
      ESP_LOGW("espnow_net_protocol.action",
               "Command not started peer=%s device=%s resource=%s command=%s",
               peer_, device_, resource_, command_);
  }

 protected:
  EspNowNetProtocolComponent *parent_;
  const char *peer_{nullptr};
  const char *device_{nullptr};
  const char *resource_{nullptr};
  const char *command_{nullptr};
  const char *payload_{""};
  uint32_t timeout_ms_{5000};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
