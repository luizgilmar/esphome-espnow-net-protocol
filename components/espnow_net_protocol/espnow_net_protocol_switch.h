#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

#include "espnow_net_protocol.h"

namespace esphome {
namespace espnow_net_protocol {

class EspNowNetProtocolSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(EspNowNetProtocolComponent *parent) { parent_ = parent; }
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  void write_state(bool state) override;
  EspNowNetProtocolComponent *parent_{nullptr};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
