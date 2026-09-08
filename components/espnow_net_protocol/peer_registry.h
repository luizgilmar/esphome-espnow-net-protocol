#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol_identity.h"

namespace esphome {
namespace espnow_net_protocol {

class PeerRegistry {
 public:
  static constexpr size_t CAPACITY = 16;

  bool add(const PeerIdentity &peer) {
    if (peer.id.empty() || size_ >= CAPACITY ||
        this->index_for_id(peer.id.c_str()) != INVALID_PEER_INDEX ||
        this->index_for_address(peer.address) != INVALID_PEER_INDEX)
      return false;
    peers_[size_++] = peer;
    return true;
  }

  const PeerIdentity *peer(PeerIndex index) const {
    return index < size_ ? &peers_[index] : nullptr;
  }

  PeerIndex index_for_id(const char *id) const {
    if (id == nullptr) return INVALID_PEER_INDEX;
    for (size_t index = 0; index < size_; index++)
      if (std::strcmp(peers_[index].id.c_str(), id) == 0)
        return static_cast<PeerIndex>(index);
    return INVALID_PEER_INDEX;
  }

  PeerIndex index_for_address(const uint8_t *address) const {
    if (address == nullptr) return INVALID_PEER_INDEX;
    for (size_t index = 0; index < size_; index++)
      if (std::memcmp(peers_[index].address, address,
                      PeerIdentity::MAC_SIZE) == 0)
        return static_cast<PeerIndex>(index);
    return INVALID_PEER_INDEX;
  }

  size_t size() const { return size_; }
  constexpr size_t capacity() const { return CAPACITY; }

 private:
  PeerIdentity peers_[CAPACITY]{};
  size_t size_{0};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
