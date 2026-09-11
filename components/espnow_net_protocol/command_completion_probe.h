#pragma once

#include <cstdint>

#include "message_model.h"

namespace esphome {
namespace espnow_net_protocol {

class CommandCompletionProbe {
 public:
  virtual ~CommandCompletionProbe() = default;
  virtual void begin() = 0;
  virtual bool completed() const = 0;
  virtual bool expected_snapshot(NetStateSnapshot &snapshot) const = 0;
  virtual bool observed_snapshot(NetStateSnapshot &snapshot) const = 0;
};

}  // namespace espnow_net_protocol
}  // namespace esphome
