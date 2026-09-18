#include "espnow_net_protocol.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol";

void EspNowNetProtocolComponent::on_command_identity(
    PeerIndex peer, const NetCommand &command) {
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  const auto match = this->peer_application_identity_.check(
      peer, command.source_device_id.c_str(), command.source_boot_id);
  const char *match_text = "UNCONFIGURED";
  switch (match) {
    case PeerApplicationIdentityMatch::MATCH: match_text = "MATCH"; break;
    case PeerApplicationIdentityMatch::LEGACY: match_text = "LEGACY"; break;
    case PeerApplicationIdentityMatch::MISMATCH: match_text = "MISMATCH"; break;
    case PeerApplicationIdentityMatch::UNCONFIGURED: break;
  }
#else
  const char *match_text = "UNCONFIGURED";
#endif
  ESP_LOGI(TAG,
           "Inbound identity observed peer=%u source_claim=%s app_boot_id=%llu tx=%llu version=%s peer_source=%s (observation only)",
           static_cast<unsigned>(peer),
           command.source_device_id.empty() ? "<absent>" : command.source_device_id.c_str(),
           static_cast<unsigned long long>(command.source_boot_id),
           static_cast<unsigned long long>(command.transaction_id),
           command.source_boot_id == 0 ? "v1" : "v2", match_text);
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  if (match == PeerApplicationIdentityMatch::MATCH &&
      this->verified_command_observer_ != nullptr)
    this->verified_command_observer_->on_command_identity(peer, command);
#endif
}

static void log_runtime_health_(const char *phase,
                                const EspIdfEspNowEncryptedRadio &radio) {
  const bool heap_ok = heap_caps_check_integrity_all(false);
  ESP_LOGW(TAG,
           "Runtime health phase=%s task=%s core=%d stack_free_bytes=%u "
           "send_cb_core=%d send_cb_stack_free_bytes=%u recv_cb_core=%d "
           "recv_cb_stack_free_bytes=%u heap_free=%u "
           "heap_min=%u heap_ok=%s rx_queue=%u completion_queue=%u "
           "dropped_rx=%u dropped_completion=%u",
           phase, pcTaskGetName(nullptr), static_cast<int>(xPortGetCoreID()),
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
           static_cast<int>(radio.send_callback_core()),
           static_cast<unsigned>(radio.send_callback_stack_free_bytes()),
           static_cast<int>(radio.receive_callback_core()),
           static_cast<unsigned>(radio.receive_callback_stack_free_bytes()),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
           static_cast<unsigned>(
               heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
           YESNO(heap_ok), static_cast<unsigned>(radio.received_queue_depth()),
           static_cast<unsigned>(radio.completion_queue_depth()),
           static_cast<unsigned>(radio.dropped_frame_count()),
           static_cast<unsigned>(radio.dropped_completion_count()));
}

void EspNowNetProtocolComponent::set_runtime_enabled(bool enabled) {
  if (runtime_enabled_ == enabled) return;
  runtime_enabled_ = enabled;
  if (!enabled) {
    if (command_client_state_ != CommandClientState::IDLE)
      (void) cancel_command(command_client_command_.transaction_id);
    sender_.reset();
    reliable_message_owner_ = ReliableMessageOwner::NONE;
    radio_transmission_owner_ = RadioTransmissionOwner::NONE;
    radio_transmission_peer_ = INVALID_PEER_INDEX;
    command_result_notification_ready_ = false;
    result_message_ready_ = false;
    discard_radio_events_();
  }
  ESP_LOGI(TAG, "Runtime %s", enabled ? "enabled" : "disabled");
}

bool EspNowNetProtocolComponent::send_command(
    const char *peer_id, const char *device_id, const char *resource,
    const char *command, const char *payload, uint32_t timeout_ms,
    uint32_t now_ms) {
  if (peer_id == nullptr ||
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
  if (!start_command(peer, request, now_ms)) return false;
  ESP_LOGI(TAG, "Command started peer=%s device=%s resource=%s command=%s tx=%llu",
           peer_id, device_id, resource, command,
           static_cast<unsigned long long>(request.transaction_id));
  return true;
}

bool EspNowNetProtocolComponent::start_command(
    PeerIndex peer, const NetCommand &command, uint32_t now_ms) {
  if (!runtime_enabled_ ||
      command_client_state_ != CommandClientState::IDLE ||
      reliable_message_owner_ != ReliableMessageOwner::NONE ||
      sender_.state() != ReliableSenderState::IDLE ||
      peer == INVALID_PEER_INDEX || !command.valid())
    return false;
  EspNowCommandPayload encoded{};
  if (!command_codec_.encode(command, encoded) ||
      !sender_.start(peer, EspNowFrameKind::COMMAND, command.transaction_id,
                     encoded.data.data(), encoded.data.size()))
    return false;
  command_client_command_ = command;
  command_client_peer_ = peer;
  command_client_started_ms_ = now_ms;
  command_client_state_ = CommandClientState::WAITING_FOR_DELIVERY_ACK;
  reliable_message_owner_ = ReliableMessageOwner::COMMAND_CLIENT;
  log_runtime_health_("command_started", radio_);
  return true;
}

bool EspNowNetProtocolComponent::cancel_command(TransactionId transaction_id) {
  if (transaction_id == 0 || command_client_state_ == CommandClientState::IDLE ||
      command_client_command_.transaction_id != transaction_id)
    return false;
  if (reliable_message_owner_ == ReliableMessageOwner::COMMAND_CLIENT) {
    sender_.reset();
    reliable_message_owner_ = ReliableMessageOwner::NONE;
  }
  last_terminal_peer_ = command_client_peer_;
  last_terminal_transaction_id_ = transaction_id;
  command_client_cancel_count_++;
  clear_command_client_();
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
  if (!runtime_enabled_) {
    discard_radio_events_();
    return;
  }
  // Notify integrations only after the protocol call chain that produced the
  // result has unwound. This bounds main-task stack use during radio failures.
  dispatch_command_result_notification_();
  process_send_completion_(now_ms);
  process_received_frame_();
  process_application_message_(now_ms);
  if (command_client_state_ == CommandClientState::WAITING_FOR_RESULT &&
      now_ms - command_client_started_ms_ >= command_client_command_.timeout_ms)
    fail_command_client_(NetErrorCode::TIMED_OUT,
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
  const bool command_client_result = inbound_matches_command_client_();
  const bool late_terminal = inbound_matches_last_terminal_();
  // Keep the reassembler-owned payload in place until its bounded consumer is
  // ready. This prevents an ACCEPTED delivery ACK followed by a local drop.
  if ((kind == EspNowFrameKind::COMMAND &&
       command_dispatcher_.state() != InboundCommandDispatcherState::IDLE) ||
      (kind == EspNowFrameKind::RESULT && result_message_ready_ &&
       !command_client_result && !late_terminal) ||
      (kind == EspNowFrameKind::RESULT && command_client_result &&
       command_result_notification_ready_))
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
    if (command_client_result) {
      process_command_client_result_(inbound, now_ms);
      return;
    }
    if (late_terminal) {
      ESP_LOGD(TAG, "Late command result discarded peer=%u tx=%llu",
               static_cast<unsigned>(inbound.peer_index),
               static_cast<unsigned long long>(
                   inbound.message.envelope.transaction_id));
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
  if (reliable_message_owner_ == ReliableMessageOwner::COMMAND_CLIENT) {
    if (sender_.state() == ReliableSenderState::ACKNOWLEDGED) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      command_client_state_ = CommandClientState::WAITING_FOR_RESULT;
    } else if (sender_.state() == ReliableSenderState::REJECTED) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      fail_command_client_(NetErrorCode::REMOTE_REJECTED,
                           "delivery rejected", millis());
    } else if (sender_.state() == ReliableSenderState::TIMED_OUT) {
      sender_.reset();
      reliable_message_owner_ = ReliableMessageOwner::NONE;
      fail_command_client_(NetErrorCode::TIMED_OUT,
                           "delivery acknowledgement timed out", millis());
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

bool EspNowNetProtocolComponent::inbound_matches_command_client_() const {
  return command_client_state_ == CommandClientState::WAITING_FOR_RESULT &&
         runtime_.application_message_kind() == EspNowFrameKind::RESULT &&
         runtime_.application_message_peer_index() == command_client_peer_ &&
         runtime_.application_message_transaction_id() ==
             command_client_command_.transaction_id;
}

bool EspNowNetProtocolComponent::inbound_matches_last_terminal_() const {
  return last_terminal_transaction_id_ != 0 &&
         runtime_.application_message_kind() == EspNowFrameKind::RESULT &&
         runtime_.application_message_peer_index() == last_terminal_peer_ &&
         runtime_.application_message_transaction_id() ==
             last_terminal_transaction_id_;
}

void EspNowNetProtocolComponent::process_command_client_result_(
    const EspNowInboundApplicationMessage &inbound, uint32_t now_ms) {
  NetResult result{};
  if (!result_codec_.decode(
          command_client_command_.transaction_id, inbound.message.data.data(),
          inbound.message.data.size(),
          now_ms - command_client_started_ms_, result)) {
    fail_command_client_(NetErrorCode::PROTOCOL_ERROR,
                         "invalid functional result", now_ms);
    return;
  }
  ESP_LOGI(TAG, "Command result tx=%llu status=%u latency=%u error=%u",
           static_cast<unsigned long long>(result.transaction_id),
           static_cast<unsigned>(result.status),
           static_cast<unsigned>(result.latency_ms),
           static_cast<unsigned>(result.error.code));
  if (result.status == NetResultStatus::IN_PROGRESS) {
    command_client_progress_count_++;
    queue_command_result_notification_(command_client_peer_, result);
    return;
  }
  if (result.status == NetResultStatus::SUCCEEDED)
    command_client_success_count_++;
  else
    command_client_failure_count_++;
  finish_command_client_(result);
}

void EspNowNetProtocolComponent::fail_command_client_(
    NetErrorCode error, const char *message, uint32_t now_ms) {
  log_runtime_health_(error == NetErrorCode::TIMED_OUT ? "before_timeout_failure"
                                                       : "before_command_failure",
                      radio_);
  command_client_failure_count_++;
  NetResult result{};
  result.transaction_id = command_client_command_.transaction_id;
  result.status = NetResultStatus::FAILED;
  result.latency_ms = now_ms - command_client_started_ms_;
  result.error.code = error;
  result.error.retryable = error == NetErrorCode::TIMED_OUT ||
                           error == NetErrorCode::CONNECTION_FAILED;
  (void) result.error.message.assign(message);
  result.execution.started =
      command_client_state_ == CommandClientState::WAITING_FOR_RESULT;
  ESP_LOGW(TAG, "Command failed tx=%llu latency=%u error=%u reason=%s",
           static_cast<unsigned long long>(
               result.transaction_id),
           static_cast<unsigned>(result.latency_ms),
           static_cast<unsigned>(error), message);
  finish_command_client_(result);
}

void EspNowNetProtocolComponent::finish_command_client_(
    const NetResult &result) {
  const PeerIndex peer = command_client_peer_;
  last_terminal_peer_ = peer;
  last_terminal_transaction_id_ = result.transaction_id;
  clear_command_client_();
  queue_command_result_notification_(peer, result);
}

void EspNowNetProtocolComponent::clear_command_client_() {
  command_client_command_ = {};
  command_client_peer_ = INVALID_PEER_INDEX;
  command_client_started_ms_ = 0;
  command_client_state_ = CommandClientState::IDLE;
}

void EspNowNetProtocolComponent::queue_command_result_notification_(
    PeerIndex peer, const NetResult &result) {
  if (command_result_observer_ == nullptr ||
      command_result_notification_ready_)
    return;
  command_result_notification_ = result;
  command_result_notification_peer_ = peer;
  command_result_notification_ready_ = true;
}

void EspNowNetProtocolComponent::dispatch_command_result_notification_() {
  if (!command_result_notification_ready_) return;
  command_result_notification_ready_ = false;
  if (command_result_observer_ != nullptr)
    command_result_observer_->on_net_command_result(
        command_result_notification_peer_, command_result_notification_);
  command_result_notification_ = {};
  command_result_notification_peer_ = INVALID_PEER_INDEX;
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

void EspNowNetProtocolComponent::discard_radio_events_() {
  EspNowReceivedFrame received{};
  for (size_t index = 0;
       index < EspIdfEspNowEncryptedRadio::RX_QUEUE_CAPACITY; index++) {
    if (!radio_.take_received_frame(received)) break;
  }
  EspNowSendCompletion completion{};
  for (size_t index = 0;
       index < EspIdfEspNowEncryptedRadio::SEND_COMPLETION_QUEUE_CAPACITY;
       index++) {
    if (!radio_.take_send_completion(completion)) break;
  }
}

void EspNowNetProtocolComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ESP-NOW NetProtocol: %s runtime=%s channel=%u current=%u peers=%u "
                "channel_match=%s rx=%u dropped=%u tx=%u failed=%u "
                "result_ok=%u result_failed=%u command_state=%u "
                "command_progress=%u command_ok=%u command_failed=%u "
                "command_canceled=%u",
                radio_.initialized() ? "READY" : "WAITING",
                runtime_enabled_ ? "ENABLED" : "DISABLED",
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
                static_cast<unsigned>(command_client_state_),
                static_cast<unsigned>(command_client_progress_count_),
                static_cast<unsigned>(command_client_success_count_),
                static_cast<unsigned>(command_client_failure_count_),
                static_cast<unsigned>(command_client_cancel_count_));
#ifdef USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND
  ESP_LOGCONFIG(TAG, "Declarative inbound bindings=%u",
                static_cast<unsigned>(
                    declarative_command_handler_.binding_count()));
#endif
}

}  // namespace espnow_net_protocol
}  // namespace esphome
