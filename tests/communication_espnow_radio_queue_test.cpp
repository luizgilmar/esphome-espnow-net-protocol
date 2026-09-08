#include <cassert>

#include "components/espnow_net_protocol/radio_queue.h"

using namespace esphome::espnow_net_protocol;

int main() {
  EspNowSpscQueue<EspNowSendCompletion, 3> queue{};
  assert(queue.capacity() == 3);
  assert(queue.size() == 0);
  assert(queue.push({0, true}));
  assert(queue.push({1, false}));
  assert(queue.push({2, true}));
  assert(!queue.push({3, true}));
  assert(queue.size() == 3);

  EspNowSendCompletion value{};
  assert(queue.pop(value) && value.peer_index == 0 && value.succeeded);
  assert(queue.push({3, false}));
  assert(queue.pop(value) && value.peer_index == 1 && !value.succeeded);
  assert(queue.pop(value) && value.peer_index == 2 && value.succeeded);
  assert(queue.pop(value) && value.peer_index == 3 && !value.succeeded);
  assert(!queue.pop(value));
  return 0;
}
