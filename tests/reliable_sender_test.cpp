#include <cassert>
#include <cstdint>

#include "components/espnow_net_protocol/reliable_sender.h"

using namespace esphome::espnow_net_protocol;

int main() {
  ReliableMessageSender sender;
  assert(sender.configure(0x1234, {25, 2}));
  uint8_t message[300]{};
  assert(sender.start(2, EspNowFrameKind::COMMAND, 77, message,
                      sizeof(message)));

  PeerIndex peer = INVALID_PEER_INDEX;
  EspNowRadioFrame frame{};
  assert(sender.take_frame(peer, frame));
  assert(peer == 2);
  assert(sender.complete_frame(true, 10));
  assert(sender.take_frame(peer, frame));
  assert(sender.complete_frame(true, 11));
  assert(sender.state() == ReliableSenderState::WAITING_FOR_ACK);
  assert(sender.attempt_count() == 1);

  sender.loop(36);
  assert(sender.state() == ReliableSenderState::READY);
  assert(sender.take_frame(peer, frame));
  assert(sender.complete_frame(true, 37));
  assert(sender.take_frame(peer, frame));
  assert(sender.complete_frame(true, 38));
  assert(sender.attempt_count() == 2);

  EspNowAckPayload ack{};
  ack.acknowledged = sender.delivery_key();
  ack.status = EspNowAckStatus::DUPLICATE;
  assert(sender.accept_ack(ack));
  assert(sender.state() == ReliableSenderState::ACKNOWLEDGED);
  sender.reset();
  assert(sender.state() == ReliableSenderState::IDLE);

  assert(sender.start(1, EspNowFrameKind::RESULT, 78, message, 10));
  assert(sender.take_frame(peer, frame));
  assert(sender.complete_frame(false, 100));
  sender.loop(125);
  assert(sender.state() == ReliableSenderState::READY);
  assert(sender.take_frame(peer, frame));
  assert(sender.complete_frame(false, 126));
  sender.loop(151);
  assert(sender.state() == ReliableSenderState::TIMED_OUT);
  return 0;
}
