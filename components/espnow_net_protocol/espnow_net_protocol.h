#pragma once

#include "esphome/core/component.h"
#include "command_dispatcher.h"
#include "command_codec.h"
#include "espidf_espnow_encrypted_radio.h"
#include "protocol_runtime.h"
#include "reliable_sender.h"
#include "result_codec.h"

namespace esphome {
namespace espnow_net_protocol {

class EspNowNetProtocolComponent : public Component {
 public:
  enum class DeclarativeCommandState : uint8_t {
    IDLE,
    WAITING_FOR_DELIVERY_ACK,
    WAITING_FOR_RESULT,
  };
  bool configure(uint8_t channel, const char *pmk_hex) {
    return radio_.configure(channel, pmk_hex);
  }
  bool add_peer(const char *id, const char *address, const char *lmk_hex) {
    return radio_.add_peer(id, address, lmk_hex);
  }
  PeerIndex peer_index(const char *id) const { return radio_.peer_index(id); }
  bool send_frame(PeerIndex peer, const uint8_t *data, size_t size) {
    return radio_.send_frame(peer, data, size);
  }
  bool take_received_frame(EspNowReceivedFrame &frame) {
    return radio_.take_received_frame(frame);
  }
  bool take_send_completion(EspNowSendCompletion &completion) {
    return radio_.take_send_completion(completion);
  }
  EspIdfEspNowEncryptedRadio &radio() { return radio_; }
  const EspIdfEspNowEncryptedRadio &radio() const { return radio_; }

  void set_ack_timeout(uint32_t ack_timeout_ms) {
    retry_policy_.ack_timeout_ms = ack_timeout_ms;
  }
  void set_max_attempts(uint8_t max_attempts) {
    retry_policy_.max_attempts = max_attempts;
  }
  bool start_reliable_message(PeerIndex peer, EspNowFrameKind kind,
                              TransactionId transaction_id,
                              const uint8_t *data, size_t size) {
    if (reliable_message_owner_ != ReliableMessageOwner::NONE ||
        !sender_.start(peer, kind, transaction_id, data, size))
      return false;
    reliable_message_owner_ = ReliableMessageOwner::EXTERNAL;
    return true;
  }
  ReliableSenderState sender_state() const { return sender_.state(); }
  void reset_sender() {
    sender_.reset();
    reliable_message_owner_ = ReliableMessageOwner::NONE;
  }
  bool take_application_message(EspNowInboundApplicationMessage &message) {
    if (!result_message_ready_) return false;
    message = result_message_;
    result_message_ = {};
    result_message_ready_ = false;
    return true;
  }
  void set_command_handler(NetCommandHandler *handler) {
    command_dispatcher_.set_handler(handler);
  }
  bool send_command(const char *peer_id, const char *device_id,
                    const char *resource, const char *command,
                    const char *payload, uint32_t timeout_ms,
                    uint32_t now_ms);
  DeclarativeCommandState declarative_command_state() const {
    return declarative_command_state_;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RadioTransmissionOwner : uint8_t { NONE, SENDER, ACK };
  enum class ReliableMessageOwner : uint8_t {
    NONE,
    EXTERNAL,
    DISPATCHER_RESULT,
    DECLARATIVE_COMMAND,
  };
  EspIdfEspNowEncryptedRadio radio_{};
  EspNowProtocolRuntime runtime_{};
  ReliableMessageSender sender_{};
  InboundCommandDispatcher command_dispatcher_{};
  EspNowRetryPolicy retry_policy_{};
  EspNowInboundApplicationMessage result_message_{};
  bool result_message_ready_{false};
  ReliableMessageOwner reliable_message_owner_{ReliableMessageOwner::NONE};
  uint32_t result_delivery_success_count_{0};
  uint32_t result_delivery_failure_count_{0};
  EspNowCommandCodec command_codec_{};
  EspNowResultCodec result_codec_{};
  NetCommand declarative_command_{};
  PeerIndex declarative_command_peer_{INVALID_PEER_INDEX};
  TransactionId next_declarative_transaction_id_{1};
  uint32_t declarative_command_started_ms_{0};
  uint32_t declarative_command_success_count_{0};
  uint32_t declarative_command_failure_count_{0};
  DeclarativeCommandState declarative_command_state_{
      DeclarativeCommandState::IDLE};
  uint64_t local_boot_id_{0};
  uint32_t ack_sequence_{0};
  RadioTransmissionOwner radio_transmission_owner_{
      RadioTransmissionOwner::NONE};
  PeerIndex radio_transmission_peer_{INVALID_PEER_INDEX};

  void process_send_completion_(uint32_t now_ms);
  void process_received_frame_();
  void process_application_message_(uint32_t now_ms);
  void process_dispatcher_(uint32_t now_ms);
  void process_owned_sender_completion_();
  bool inbound_matches_declarative_command_() const;
  void process_declarative_result_(
      const EspNowInboundApplicationMessage &inbound, uint32_t now_ms);
  void fail_declarative_command_(NetErrorCode error, const char *message,
                                 uint32_t now_ms);
  void dispatch_sender_frame_(uint32_t now_ms);
  void dispatch_application_ack_();
};

}  // namespace espnow_net_protocol
}  // namespace esphome
