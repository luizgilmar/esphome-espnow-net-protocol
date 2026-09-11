#include "espnow_net_protocol.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_random.h>
#include <cstring>

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol";

bool EspNowNetProtocolComponent::send_command(
    const char *peer_id, const char *device_id, const char *resource,
    const char *command, const char *payload, uint32_t timeout_ms,
    uint32_t now_ms) {
  if (declarative_command_state_ != DeclarativeCommandState::IDLE ||
      reliable_message_owner_ != ReliableMessageOwner::NONE ||
      sender_.state() != ReliableSenderState::IDLE || peer_id == nullptr ||
      device_id == nullptr || resource == nullptr || command == nullptr ||
      payload == nullptr || timeout_ms == 0)
    return false;
  const PeerIndex peer = radio_.peer_index(peer_id);
  if (peer == INVALID_PEER_INDEX) return false;
  NetCommand request{};
  request.transaction_id = 0xE000000000000000ULL |
                           next_declarative_transaction_id_++;
  if (next_declarative_transaction_id_ == 0 ||
      next_declarative_transaction_id_ > 0x0FFFFFFFFFFFFFFFULL)
    next_declarative_transaction_id_ = 1;
  request.timeout_ms = timeout_ms;
  if (!request.device_id.assign(device_id) ||
      !request.resource.assign(resource) || !request.name.assign(command) ||
      !request.payload.assign(reinterpret_cast<const uint8_t *>(payload),
                              std::strlen(payload)) ||
      !request.valid())
    return false;
  EspNowCommandPayload encoded{};
  if (!command_codec_.encode(request, encoded) ||
      !sender_.start(peer, EspNowFrameKind::COMMAND, request.transaction_id,
                     encoded.data.data(), encoded.data.size()))
    return false;
  declarative_command_ = request;
  declarative_command_peer_ = peer;
  declarative_command_started_ms_ = now_ms;
  declarative_command_state_ =
      DeclarativeCommandState::WAITING_FOR_DELIVERY_ACK;
  reliable_message_owner_ = ReliableMessageOwner::DECLARATIVE_COMMAND;
  ESP_LOGI(TAG, "Command started peer=%s device=%s resource=%s command=%s tx=%llu",
           peer_id, device_id, resource, command,
           static_cast<unsigned long long>(request.transaction_id));
  return true;
}

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
  process_application_message_(now_ms);
  if (declarative_command_state_ ==
          DeclarativeCommandState::WAITING_FOR_RESULT &&
      now_ms - declarative_command_started_ms_ >=
          declarative_command_.timeout_ms)
    fail_declarative_command_(NetErrorCode::TIMED_OUT,
                              "functional result timed out", now_ms);
  command_dispatcher_.loop(now_ms);
  sender_.loop(now_ms);
  process_owned_sender_completion_();
  process_dispatcher_(now_ms);
  dispatch_application_ack_();
  dispatch_sender_frame_(now_ms);
}

void EspNowNetProtocolComponent::process_application_message_(
    uint32_t now_ms) {
  if (!runtime_.application_message_ready()) return;
  const EspNowFrameKind kind = runtime_.application_message_kind();
  const bool declarative_result = inbound_matches_declarative_command_();
  // Keep the reassembler-owned payload in place until its bounded consumer is
  // ready. This prevents an ACCEPTED delivery ACK followed by a local drop.
  if ((kind == EspNowFrameKind::COMMAND &&
       command_dispatcher_.state() != InboundCommandDispatcherState::IDLE) ||
      (kind == EspNowFrameKind::RESULT && result_message_ready_ &&
       !declarative_result))
    return;
  EspNowInboundApplicationMessage inbound{};
  if (!runtime_.take_application_message(inbound)) return;
  if (inbound.message.envelope.kind == EspNowFrameKind::COMMAND) {
    if (!command_dispatcher_.accept(inbound, now_ms)) {
      ESP_LOGW(TAG, "Inbound command dispatcher busy tx=%llu",
               static_cast<unsigned long long>(
                   inbound.message.envelope.transaction_id));
    }
    return;
  }
  if (inbound.message.envelope.kind == EspNowFrameKind::RESULT) {
    if (declarative_result) {
      process_declarative_result_(inbound, now_ms);
      return;
    }
    result_message_ = inbound;
    result_message_ready_ = true;
    return;
  }
  ESP_LOGW(TAG, "Inbound application message dropped kind=%u tx=%llu",
           static_cast<unsigned>(inbound.message.envelope.kind),
           static_cast<unsigned long long>(
               inbound.message.envelope.transaction_id));
}

void EspNowNetProtocolComponent::process_dispatcher_(uint32_t now_ms) {
  if (!command_dispatcher_.has_result() ||
      reliable_message_owner_ != ReliableMessageOwner::NONE ||
      sender_.state() != ReliableSenderState::IDLE)
    return;
  PendingNetResult pending{};
  if (!command_dispatcher_.take_result(pending)) return;
  if (!sender_.start(pending.peer_index, EspNowFrameKind::RESULT,
                     pending.transaction_id, pending.payload.data.data(),
                     pending.payload.data.size())) {
    result_delivery_failure_count_++;
    return;
  }
  reliable_message_owner_ = ReliableMessageOwner::DISPATCHER_RESULT;
  (void) now_ms;
}

void EspNowNetProtocolComponent::process_owned_sender_completion_() {
  if (reliable_message_owner_ == ReliableMessageOwner::DECLARATIVE_COMMAND) {
    if (sender_.state() == ReliableSenderState::ACKNOWLEDGED) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      declarative_command_state_ = DeclarativeCommandState::WAITING_FOR_RESULT;
    } else if (sender_.state() == ReliableSenderState::REJECTED) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      fail_declarative_command_(NetErrorCode::REMOTE_REJECTED,
                                "delivery rejected", millis());
    } else if (sender_.state() == ReliableSenderState::TIMED_OUT) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      fail_declarative_command_(NetErrorCode::TIMED_OUT,
                                "delivery acknowledgement timed out",
                                millis());
    }
    return;
  }
  if (reliable_message_owner_ != ReliableMessageOwner::DISPATCHER_RESULT)
    return;
  if (sender_.state() == ReliableSenderState::ACKNOWLEDGED) {
    result_delivery_success_count_++;
    sender_.reset();
    reliable_message_owner_ = ReliableMessageOwner::NONE;
  } else if (sender_.state() == ReliableSenderState::REJECTED ||
             sender_.state() == ReliableSenderState::TIMED_OUT) {
    result_delivery_failure_count_++;
    sender_.reset();
    reliable_message_owner_ = ReliableMessageOwner::NONE;
  }
}

bool EspNowNetProtocolComponent::inbound_matches_declarative_command_() const {
  return declarative_command_state_ ==
             DeclarativeCommandState::WAITING_FOR_RESULT &&
         runtime_.application_message_kind() == EspNowFrameKind::RESULT &&
         runtime_.application_message_peer_index() == declarative_command_peer_ &&
         runtime_.application_message_transaction_id() ==
             declarative_command_.transaction_id;
}

void EspNowNetProtocolComponent::process_declarative_result_(
    const EspNowInboundApplicationMessage &inbound, uint32_t now_ms) {
  NetResult result{};
  if (!result_codec_.decode(
          declarative_command_.transaction_id, inbound.message.data.data(),
          inbound.message.data.size(),
          now_ms - declarative_command_started_ms_, result)) {
    fail_declarative_command_(NetErrorCode::PROTOCOL_ERROR,
                              "invalid functional result", now_ms);
    return;
  }
  ESP_LOGI(TAG, "Command result tx=%llu status=%u latency=%u error=%u",
           static_cast<unsigned long long>(result.transaction_id),
           static_cast<unsigned>(result.status),
           static_cast<unsigned>(result.latency_ms),
           static_cast<unsigned>(result.error.code));
  if (result.status == NetResultStatus::IN_PROGRESS) return;
  if (result.status == NetResultStatus::SUCCEEDED)
    declarative_command_success_count_++;
  else
    declarative_command_failure_count_++;
  declarative_command_ = {};
  declarative_command_peer_ = INVALID_PEER_INDEX;
  declarative_command_state_ = DeclarativeCommandState::IDLE;
}

void EspNowNetProtocolComponent::fail_declarative_command_(
    NetErrorCode error, const char *message, uint32_t now_ms) {
  declarative_command_failure_count_++;
  ESP_LOGW(TAG, "Command failed tx=%llu latency=%u error=%u reason=%s",
           static_cast<unsigned long long>(
               declarative_command_.transaction_id),
           static_cast<unsigned>(now_ms - declarative_command_started_ms_),
           static_cast<unsigned>(error), message);
  declarative_command_ = {};
  declarative_command_peer_ = INVALID_PEER_INDEX;
  declarative_command_state_ = DeclarativeCommandState::IDLE;
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
                "channel_match=%s rx=%u dropped=%u tx=%u failed=%u "
                "result_ok=%u result_failed=%u command_state=%u "
                "command_ok=%u command_failed=%u",
                radio_.initialized() ? "READY" : "WAITING",
                static_cast<unsigned>(radio_.expected_channel()),
                static_cast<unsigned>(radio_.current_channel()),
                static_cast<unsigned>(radio_.peer_count()),
                YESNO(radio_.channel_matches()),
                static_cast<unsigned>(radio_.received_frame_count()),
                static_cast<unsigned>(radio_.dropped_frame_count()),
                static_cast<unsigned>(radio_.sent_frame_count()),
                static_cast<unsigned>(radio_.failed_send_count()),
                static_cast<unsigned>(result_delivery_success_count_),
                static_cast<unsigned>(result_delivery_failure_count_),
                static_cast<unsigned>(declarative_command_state_),
                static_cast<unsigned>(declarative_command_success_count_),
                static_cast<unsigned>(declarative_command_failure_count_));
#ifdef USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND
  ESP_LOGCONFIG(TAG, "Declarative inbound bindings=%u",
                static_cast<unsigned>(
                    declarative_command_handler_.binding_count()));
#endif
}

}  // namespace espnow_net_protocol
}  // namespace esphome
