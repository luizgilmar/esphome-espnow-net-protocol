#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol_identity.h"

namespace esphome {
namespace espnow_net_protocol {

enum class PeerApplicationIdentityMatch : uint8_t {
  MATCH,
  UNCONFIGURED,
  LEGACY,
  MISMATCH,
};

// YAML strings have static lifetime in generated ESPHome code. Store pointers
// instead of duplicating up to 16 bounded identifiers in device RAM.
class PeerApplicationIdentity {
 public:
  static constexpr size_t MAX_PEERS = 16;

  bool set(PeerIndex peer, const char *source_id) {
    if (peer >= MAX_PEERS || source_id == nullptr || source_id[0] == '\0' ||
        expected_[peer] != nullptr) return false;
    size_t length = 0;
    while (length <= 63 && source_id[length] != '\0') ++length;
    if (length > 63) return false;
    expected_[peer] = source_id;
    return true;
  }

  PeerApplicationIdentityMatch check(PeerIndex peer, const char *source_id,
                                     uint64_t boot_id) const {
    if (peer >= MAX_PEERS || expected_[peer] == nullptr)
      return PeerApplicationIdentityMatch::UNCONFIGURED;
    if (source_id == nullptr || source_id[0] == '\0' || boot_id == 0)
      return PeerApplicationIdentityMatch::LEGACY;
    return std::strcmp(expected_[peer], source_id) == 0
               ? PeerApplicationIdentityMatch::MATCH
               : PeerApplicationIdentityMatch::MISMATCH;
  }

 private:
  const char *expected_[MAX_PEERS]{};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
