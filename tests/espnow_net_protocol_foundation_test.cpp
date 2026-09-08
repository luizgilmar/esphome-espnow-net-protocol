#include <cassert>
#include <cstring>

#include "components/espnow_net_protocol/protocol_identity.h"

using namespace esphome::espnow_net_protocol;

int main() {
  BoundedText<5> text{};
  assert(text.assign("peer1"));
  assert(!text.assign("peer12"));
  assert(text.empty());

  const uint8_t source[]{1, 2, 3};
  BoundedBytes<3> bytes{};
  assert(bytes.assign(source, sizeof(source)));
  assert(bytes.size() == sizeof(source));
  assert(std::memcmp(bytes.data(), source, sizeof(source)) == 0);

  const DeliveryIdentity first{10, 20, 30};
  const DeliveryIdentity same{10, 20, 30};
  const DeliveryIdentity other_boot{11, 20, 30};
  assert(first.valid());
  assert(first == same);
  assert(!(first == other_boot));
  assert(!DeliveryIdentity{}.valid());
  return 0;
}
