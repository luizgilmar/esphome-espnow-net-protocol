#include <cassert>

#include "components/espnow_net_protocol/peer_application_identity.h"

using namespace esphome::espnow_net_protocol;

int main() {
  PeerApplicationIdentity map;
  assert(map.set(0, "tx-quartogian-integration"));
  assert(!map.set(0, "other"));
  assert(!map.set(INVALID_PEER_INDEX, "other"));
  assert(map.check(0, "tx-quartogian-integration", 42) ==
         PeerApplicationIdentityMatch::MATCH);
  assert(map.check(0, "other", 42) ==
         PeerApplicationIdentityMatch::MISMATCH);
  assert(map.check(0, "tx-quartogian-integration", 0) ==
         PeerApplicationIdentityMatch::LEGACY);
  assert(map.check(1, "tx-quartogian-integration", 42) ==
         PeerApplicationIdentityMatch::UNCONFIGURED);
}
