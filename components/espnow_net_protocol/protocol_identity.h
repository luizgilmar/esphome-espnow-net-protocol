#pragma once

#include <cstdint>

#include "bounded_value.h"

namespace esphome {
namespace espnow_net_protocol {

using TransactionId = uint64_t;
using PeerIndex = uint8_t;
static constexpr PeerIndex INVALID_PEER_INDEX = UINT8_MAX;

struct DeliveryIdentity {
  uint64_t source_boot_id{0};
  TransactionId transaction_id{0};
  uint32_t sequence{0};

  bool valid() const {
    return source_boot_id != 0 && transaction_id != 0 && sequence != 0;
  }

  bool operator==(const DeliveryIdentity &other) const {
    return source_boot_id == other.source_boot_id &&
           transaction_id == other.transaction_id && sequence == other.sequence;
  }
};

struct PeerIdentity {
  static constexpr size_t MAX_ID_SIZE = 63;
  static constexpr size_t MAC_SIZE = 6;
  static constexpr size_t KEY_SIZE = 16;

  BoundedText<MAX_ID_SIZE> id{};
  uint8_t address[MAC_SIZE]{};
  uint8_t lmk[KEY_SIZE]{};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
