#include <cassert>

#include "components/espnow_net_protocol/peer_registry.h"

using namespace esphome::espnow_net_protocol;

static PeerIdentity make_peer(const char *id, uint8_t suffix) {
  PeerIdentity peer{};
  assert(peer.id.assign(id));
  peer.address[5] = suffix;
  peer.lmk[0] = suffix;
  return peer;
}

int main() {
  PeerRegistry registry;
  assert(registry.add(make_peer("hub_quarto", 1)));
  assert(registry.add(make_peer("rele_persiana", 2)));
  assert(!registry.add(make_peer("hub_quarto", 3)));
  assert(!registry.add(make_peer("outro_id", 2)));
  assert(registry.index_for_id("hub_quarto") == 0);
  assert(registry.index_for_id("rele_persiana") == 1);
  assert(registry.index_for_id("ausente") == INVALID_PEER_INDEX);
  assert(registry.peer(1)->id.size() == 13);
  assert(registry.peer(INVALID_PEER_INDEX) == nullptr);
  for (uint8_t index = 3; index <= PeerRegistry::CAPACITY; index++) {
    char id[12]{};
    id[0] = 'p';
    id[1] = static_cast<char>('0' + index / 10);
    id[2] = static_cast<char>('0' + index % 10);
    assert(registry.add(make_peer(id, index)));
  }
  assert(registry.size() == 16);
  assert(!registry.add(make_peer("overflow", 17)));
  return 0;
}
