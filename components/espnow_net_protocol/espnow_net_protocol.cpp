#include "espnow_net_protocol.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_random.h>

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol";

void EspNowNetProtocolComponent::setup() {
  local_boot_id_ = (static_cast<uint64_t>(esp_random()) << 32U) | esp_random();
  if (local_boot_id_ == 0) local_boot_id_ = 1;
  if (!runtime_.configure(local_boot_id_) ||
      !sender_.configure(local_boot_id_, retry_policy_)) {
    ESP_LOGE(TAG, "Failed to configure protocol runtime");
    mark_failed();
  }
}

void EspNowNetProtocolComponent::loop() {
  const uint32_t now_ms = millis();
  radio_.loop(now_ms);
  if (!radio_.initialized() || is_failed()) return;
  process_send_completion_(now_ms);
  process_received_frame_();
  sender_.loop(now_ms);
  dispatch_application_ack_();
  dispatch_sender_frame_(now_ms);
}

void EspNowNetProtocolComponent::process_send_completion_(uint32_t now_ms) {
  EspNowSendCompletion completion{};
  if (!radio_.take_send_completion(completion)) return;
  if (radio_transmission_owner_ == RadioTransmissionOwner::NONE ||
      completion.peer_index != radio_transmission_peer_)
    return;
  if (radio_transmission_owner_ == RadioTransmissionOwner::SENDER &&
      sender_.frame_in_flight() &&
      completion.peer_index == radio_transmission_peer_)
    sender_.complete_frame(completion.succeeded, now_ms);
  radio_transmission_owner_ = RadioTransmissionOwner::NONE;
  radio_transmission_peer_ = INVALID_PEER_INDEX;
}

void EspNowNetProtocolComponent::process_received_frame_() {
  EspNowReceivedFrame received{};
  if (!radio_.take_received_frame(received)) return;
  const EspNowProtocolAcceptResult accepted = runtime_.accept(received);
  if (accepted == EspNowProtocolAcceptResult::DELIVERY_ACK_READY) {
    EspNowAckPayload ack{};
    if (runtime_.take_delivery_ack(ack)) sender_.accept_ack(ack);
  }
}

void EspNowNetProtocolComponent::dispatch_sender_frame_(uint32_t now_ms) {
  if (radio_transmission_owner_ != RadioTransmissionOwner::NONE) return;
  PeerIndex peer = INVALID_PEER_INDEX;
  EspNowRadioFrame frame{};
  if (!sender_.take_frame(peer, frame)) return;
  if (!radio_.send_frame(peer, frame.data.data(), frame.data.size())) {
    sender_.complete_frame(false, now_ms);
    return;
  }
  radio_transmission_owner_ = RadioTransmissionOwner::SENDER;
  radio_transmission_peer_ = peer;
}

void EspNowNetProtocolComponent::dispatch_application_ack_() {
  if (radio_transmission_owner_ != RadioTransmissionOwner::NONE) return;
  EspNowPendingApplicationAck pending{};
  if (!runtime_.take_application_ack(pending)) return;
  BoundedBytes<EspNowAckPayload::ENCODED_SIZE> payload{};
  EspNowAckCodec ack_codec{};
  if (!ack_codec.encode(pending.payload, payload)) return;
  ack_sequence_++;
  if (ack_sequence_ == 0) ack_sequence_++;
  EspNowFrameEnvelope envelope{};
  envelope.kind = EspNowFrameKind::ACK;
  envelope.source_boot_id = local_boot_id_;
  envelope.transaction_id = pending.payload.acknowledged.transaction_id;
  envelope.sequence = ack_sequence_;
  EspNowRadioFrame frame{};
  EspNowFrameCodec frame_codec{};
  if (frame_codec.encode_fragment(envelope, payload.data(), payload.size(), 0,
                                  frame) &&
      radio_.send_frame(pending.peer_index, frame.data.data(),
                        frame.data.size())) {
    radio_transmission_owner_ = RadioTransmissionOwner::ACK;
    radio_transmission_peer_ = pending.peer_index;
  }
}

void EspNowNetProtocolComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ESP-NOW NetProtocol: %s channel=%u current=%u peers=%u "
                "channel_match=%s rx=%u dropped=%u tx=%u failed=%u",
                radio_.initialized() ? "READY" : "WAITING",
                static_cast<unsigned>(radio_.expected_channel()),
                static_cast<unsigned>(radio_.current_channel()),
                static_cast<unsigned>(radio_.peer_count()),
                YESNO(radio_.channel_matches()),
                static_cast<unsigned>(radio_.received_frame_count()),
                static_cast<unsigned>(radio_.dropped_frame_count()),
                static_cast<unsigned>(radio_.sent_frame_count()),
                static_cast<unsigned>(radio_.failed_send_count()));
}

}  // namespace espnow_net_protocol
}  // namespace esphome
