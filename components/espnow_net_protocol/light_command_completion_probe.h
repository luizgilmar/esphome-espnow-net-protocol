#pragma once

#include "esphome/components/light/light_state.h"

#include "command_completion_probe.h"

namespace esphome {
namespace espnow_net_protocol {

enum class LightExpectedState : uint8_t { ON, OFF, TOGGLED };

class LightCommandCompletionProbe final : public CommandCompletionProbe {
 public:
  void set_light(light::LightState *state) { state_ = state; }
  void set_expected(LightExpectedState expected) { expected_ = expected; }

  void begin() override {
    const bool current = state_ != nullptr && state_->current_values.is_on();
    expected_on_ = expected_ == LightExpectedState::ON ||
                   (expected_ == LightExpectedState::TOGGLED && !current);
    armed_ = state_ != nullptr;
  }

  bool completed() const override {
    return armed_ && state_->current_values.is_on() == expected_on_;
  }

  bool expected_snapshot(NetStateSnapshot &snapshot) const override {
    return this->fill_snapshot_(expected_on_, snapshot);
  }

  bool observed_snapshot(NetStateSnapshot &snapshot) const override {
    return state_ != nullptr &&
           this->fill_snapshot_(state_->current_values.is_on(), snapshot);
  }

 private:
  bool fill_snapshot_(bool state, NetStateSnapshot &snapshot) const {
    snapshot = {};
    snapshot.completeness = NetStateCompleteness::COMPLETE;
    const uint8_t value = state ? 1 : 0;
    return snapshot.schema.assign("binary-state/v1") &&
           snapshot.data.assign(&value, 1);
  }

  light::LightState *state_{nullptr};
  LightExpectedState expected_{LightExpectedState::TOGGLED};
  bool expected_on_{false};
  bool armed_{false};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
