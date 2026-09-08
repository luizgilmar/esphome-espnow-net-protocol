#include <cassert>

#include "components/espnow_net_protocol/reliability.h"

using namespace esphome::espnow_net_protocol;

int main() {
  const EspNowDeliveryKey key{0x1234, 77, 9};
  EspNowAckPayload ack{key, EspNowAckStatus::ACCEPTED};
  EspNowAckCodec codec{};
  esphome::espnow_net_protocol::BoundedBytes<
      EspNowAckPayload::ENCODED_SIZE> bytes{};
  assert(codec.encode(ack, bytes));
  assert(bytes.size() == EspNowAckPayload::ENCODED_SIZE);
  EspNowAckPayload decoded{};
  assert(codec.decode(bytes.data(), bytes.size(), decoded));
  assert(decoded.acknowledged == key);
  assert(decoded.status == EspNowAckStatus::ACCEPTED);

  EspNowReliableDelivery delivery{};
  assert(delivery.configure({100, 3}));
  assert(delivery.start(key));
  assert(delivery.state() == EspNowDeliveryState::READY_TO_SEND);
  assert(delivery.mark_sent(1000));
  delivery.loop(1099);
  assert(delivery.state() == EspNowDeliveryState::WAITING_FOR_ACK);
  delivery.loop(1100);
  assert(delivery.state() == EspNowDeliveryState::READY_TO_SEND);
  assert(delivery.mark_sent(1100));
  assert(!delivery.accept_ack({{0x1234, 78, 9}, EspNowAckStatus::ACCEPTED}));
  assert(delivery.accept_ack({key, EspNowAckStatus::DUPLICATE}));
  assert(delivery.state() == EspNowDeliveryState::ACKNOWLEDGED);

  delivery.reset();
  assert(delivery.start(key));
  assert(delivery.mark_sent(0));
  delivery.loop(100);
  assert(delivery.mark_sent(100));
  delivery.loop(200);
  assert(delivery.mark_sent(200));
  delivery.loop(300);
  assert(delivery.state() == EspNowDeliveryState::TIMED_OUT);
  assert(delivery.attempt_count() == 3);

  EspNowDuplicateCache duplicates{};
  assert(duplicates.observe(key) == EspNowDuplicateObservation::NEW_MESSAGE);
  assert(duplicates.observe(key) == EspNowDuplicateObservation::DUPLICATE);
  for (uint64_t index = 1; index <= EspNowDuplicateCache::CAPACITY; index++)
    assert(duplicates.observe({0x9000 + index, 100 + index, 1}) ==
           EspNowDuplicateObservation::NEW_MESSAGE);
  assert(duplicates.size() == EspNowDuplicateCache::CAPACITY);
  assert(duplicates.observe(key) == EspNowDuplicateObservation::NEW_MESSAGE);
  return 0;
}
