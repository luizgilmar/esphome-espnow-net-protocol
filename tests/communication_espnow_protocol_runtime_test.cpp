#include <cassert>
#include <cstring>

#include "components/espnow_net_protocol/protocol_runtime.h"

using namespace esphome::espnow_net_protocol;

static EspNowReceivedFrame frame_for(EspNowFrameCodec &codec,
                                     const EspNowFrameEnvelope &envelope,
                                     const uint8_t *payload, size_t size,
                                     uint8_t fragment, uint8_t peer = 1) {
  EspNowReceivedFrame received{};
  received.peer_index = peer;
  assert(codec.encode_fragment(envelope, payload, size, fragment,
                               received.frame));
  return received;
}

int main() {
  EspNowFrameCodec codec{};
  EspNowProtocolRuntime runtime{0xAA55};
  uint8_t payload[300]{};
  for (size_t i = 0; i < sizeof(payload); i++) payload[i] = i & 0xFFU;
  EspNowFrameEnvelope command{EspNowFrameKind::COMMAND,
                              EspNowFrameCodec::FLAG_ACK_REQUIRED,
                              0x1111, 77, 5};

  auto second = frame_for(codec, command, payload, sizeof(payload), 1);
  auto first = frame_for(codec, command, payload, sizeof(payload), 0);
  assert(runtime.accept(second) ==
         EspNowProtocolAcceptResult::FRAGMENT_ACCEPTED);
  assert(runtime.accept(first) ==
         EspNowProtocolAcceptResult::APPLICATION_MESSAGE_READY);

  EspNowInboundApplicationMessage inbound{};
  assert(runtime.take_application_message(inbound));
  assert(inbound.peer_index == 1);
  assert(inbound.message.data.size() == sizeof(payload));
  assert(std::memcmp(inbound.message.data.data(), payload, sizeof(payload)) == 0);
  EspNowPendingApplicationAck pending{};
  assert(runtime.take_application_ack(pending));
  assert(pending.payload.status == EspNowAckStatus::ACCEPTED);
  assert(pending.payload.acknowledged.transaction_id == 77);

  assert(runtime.accept(first) ==
         EspNowProtocolAcceptResult::FRAGMENT_ACCEPTED);
  assert(runtime.accept(second) ==
         EspNowProtocolAcceptResult::DUPLICATE_ACK_READY);
  assert(!runtime.take_application_message(inbound));
  assert(runtime.take_application_ack(pending));
  assert(pending.payload.status == EspNowAckStatus::DUPLICATE);
  assert(runtime.duplicate_count() == 1);

  EspNowAckPayload ack{{0xAA55, 91, 8}, EspNowAckStatus::ACCEPTED};
  esphome::espnow_net_protocol::BoundedBytes<
      EspNowAckPayload::ENCODED_SIZE> ack_bytes{};
  EspNowAckCodec ack_codec{};
  assert(ack_codec.encode(ack, ack_bytes));
  EspNowFrameEnvelope ack_envelope{EspNowFrameKind::ACK, 0, 0x2222, 91, 1};
  auto ack_frame = frame_for(codec, ack_envelope, ack_bytes.data(),
                             ack_bytes.size(), 0, 2);
  assert(runtime.accept(ack_frame) ==
         EspNowProtocolAcceptResult::DELIVERY_ACK_READY);
  EspNowAckPayload taken{};
  assert(runtime.take_delivery_ack(taken));
  assert(taken.acknowledged.transaction_id == 91);

  EspNowFrameEnvelope damaged_envelope{EspNowFrameKind::COMMAND,
                                       EspNowFrameCodec::FLAG_ACK_REQUIRED,
                                       0x3333, 92, 9};
  uint8_t damaged_payload[]{1, 2, 3, 4};
  auto damaged = frame_for(codec, damaged_envelope, damaged_payload,
                           sizeof(damaged_payload), 0, 3);
  uint8_t damaged_frame_bytes[EspNowRadioFrame::MAX_SIZE]{};
  const size_t damaged_frame_size = damaged.frame.data.size();
  std::memcpy(damaged_frame_bytes, damaged.frame.data.data(),
              damaged_frame_size);
  damaged_frame_bytes[damaged_frame_size - 1] ^= 0xFFU;
  assert(damaged.frame.data.assign(damaged_frame_bytes, damaged_frame_size));
  assert(runtime.accept(damaged) ==
         EspNowProtocolAcceptResult::INVALID_FRAME);
  assert(!runtime.take_application_message(inbound));

  auto recovered = frame_for(codec, damaged_envelope, damaged_payload,
                             sizeof(damaged_payload), 0, 3);
  assert(runtime.accept(recovered) ==
         EspNowProtocolAcceptResult::APPLICATION_MESSAGE_READY);
  assert(runtime.take_application_message(inbound));
  assert(runtime.take_application_ack(pending));
  return 0;
}
