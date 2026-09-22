#pragma once

#include "esphome/core/component.h"
#include "command_dispatcher.h"
#include "command_codec.h"
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
#include "peer_application_identity.h"
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND
#include "declarative_command_handler.h"
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_LIGHT_COMPLETION
#include "light_command_completion_probe.h"
#endif
#include "espidf_espnow_encrypted_radio.h"
#include "protocol_runtime.h"
#include "reliable_sender.h"
#include "result_codec.h"

namespace esphome {
namespace espnow_net_protocol {

class NetCommandResultObserver {
 public:
  virtual ~NetCommandResultObserver() = default;
  virtual void on_net_command_result(PeerIndex peer,
                                     const NetResult &result) = 0;
};

class EspNowNetProtocolComponent : public Component,
                                    public NetCommandIdentityObserver {
 public:
  enum class CommandClientState : uint8_t {
    IDLE,
    WAITING_FOR_DELIVERY_ACK,
    WAITING_FOR_RESULT,
  };
  using DeclarativeCommandState = CommandClientState;
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
  void set_runtime_enabled(bool enabled);
  bool runtime_enabled() const { return runtime_enabled_; }

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
    reliable_message_owner_ = ReliableMessageOwner::API_CALLER;
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
#ifdef USE_ESPNOW_NET_PROTOCOL_INTERRUPTIBLE_INBOUND
    interrupt_dispatcher_.set_handler(handler);
#endif
  }
#ifdef USE_ESPNOW_NET_PROTOCOL_INTERRUPTIBLE_INBOUND
  // Enable only when the application handler supports an interrupt while an
  // operation is active. Existing installations retain single-command flow.
  void set_interruptible_inbound(bool enabled) { interruptible_inbound_ = enabled; }
#endif
  void set_observe_application_identity(bool enabled) {
    command_dispatcher_.set_identity_observer(enabled ? this : nullptr);
#ifdef USE_ESPNOW_NET_PROTOCOL_INTERRUPTIBLE_INBOUND
    interrupt_dispatcher_.set_identity_observer(enabled ? this : nullptr);
#endif
  }
  void on_command_identity(PeerIndex peer, const NetCommand &command) override;
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  // Passive integration point. The observer must not retain a reference to
  // command or attempt to dispatch/acknowledge it.
  void set_verified_command_observer(NetCommandIdentityObserver *observer) {
    verified_command_observer_ = observer;
  }
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  bool set_expected_application_source(const char *peer_id,
                                       const char *source_id) {
    return peer_application_identity_.set(this->peer_index(peer_id), source_id);
  }
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND
  void configure_declarative_inbound(const char *device_id) {
    declarative_command_handler_.set_device_id(device_id);
    command_dispatcher_.set_handler(&declarative_command_handler_);
  }
  bool add_declarative_binding(DeclarativeCommandBinding *binding) {
    return declarative_command_handler_.add_binding(binding);
  }
#endif
  bool send_command(const char *peer_id, const char *device_id,
                    const char *resource, const char *command,
                    const char *payload, uint32_t timeout_ms,
                    uint32_t now_ms);
  bool start_command(PeerIndex peer, const NetCommand &command,
                     uint32_t now_ms);
  bool cancel_command(TransactionId transaction_id);
  void set_command_result_observer(NetCommandResultObserver *observer) {
    command_result_observer_ = observer;
  }
  CommandClientState command_client_state() const {
    return command_client_state_;
  }
  TransactionId active_command_transaction_id() const {
    return command_client_command_.transaction_id;
  }
  DeclarativeCommandState declarative_command_state() const {
    return command_client_state_;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RadioTransmissionOwner : uint8_t { NONE, SENDER, ACK };
  enum class ReliableMessageOwner : uint8_t {
    NONE,
    API_CALLER,
    DISPATCHER_RESULT,
    COMMAND_CLIENT,
  };
  EspIdfEspNowEncryptedRadio radio_{};
  EspNowProtocolRuntime runtime_{};
  ReliableMessageSender sender_{};
  InboundCommandDispatcher command_dispatcher_{};
#ifdef USE_ESPNOW_NET_PROTOCOL_INTERRUPTIBLE_INBOUND
  InboundCommandDispatcher interrupt_dispatcher_{};
  bool interruptible_inbound_{false};
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  PeerApplicationIdentity peer_application_identity_{};
#endif
#ifdef USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND
  DeclarativeCommandHandler declarative_command_handler_{};
#endif
  EspNowRetryPolicy retry_policy_{};
  EspNowInboundApplicationMessage result_message_{};
  bool result_message_ready_{false};
  ReliableMessageOwner reliable_message_owner_{ReliableMessageOwner::NONE};
  uint32_t result_delivery_success_count_{0};
  uint32_t result_delivery_failure_count_{0};
  EspNowCommandCodec command_codec_{};
  EspNowResultCodec result_codec_{};
  NetCommand command_client_command_{};
  PeerIndex command_client_peer_{INVALID_PEER_INDEX};
  NetCommandResultObserver *command_result_observer_{nullptr};
#ifdef USE_ESPNOW_NET_PROTOCOL_IDENTITY_OBSERVATION
  NetCommandIdentityObserver *verified_command_observer_{nullptr};
#endif
  NetResult command_result_notification_{};
  PeerIndex command_result_notification_peer_{INVALID_PEER_INDEX};
  bool command_result_notification_ready_{false};
  TransactionId next_declarative_transaction_id_{1};
  uint32_t command_client_started_ms_{0};
  uint32_t command_client_progress_count_{0};
  uint32_t command_client_success_count_{0};
  uint32_t command_client_failure_count_{0};
  uint32_t command_client_cancel_count_{0};
  CommandClientState command_client_state_{CommandClientState::IDLE};
  PeerIndex last_terminal_peer_{INVALID_PEER_INDEX};
  TransactionId last_terminal_transaction_id_{0};
  uint64_t local_boot_id_{0};
  uint32_t ack_sequence_{0};
  RadioTransmissionOwner radio_transmission_owner_{
      RadioTransmissionOwner::NONE};
  PeerIndex radio_transmission_peer_{INVALID_PEER_INDEX};
  bool runtime_enabled_{true};

  void process_send_completion_(uint32_t now_ms);
  void process_received_frame_();
  void process_application_message_(uint32_t now_ms);
  void process_dispatcher_(uint32_t now_ms);
  void process_owned_sender_completion_();
  bool inbound_matches_command_client_() const;
  bool inbound_matches_last_terminal_() const;
  void process_command_client_result_(
      const EspNowInboundApplicationMessage &inbound, uint32_t now_ms);
  void fail_command_client_(NetErrorCode error, const char *message,
                            uint32_t now_ms);
  void finish_command_client_(const NetResult &result);
  void clear_command_client_();
  void queue_command_result_notification_(PeerIndex peer,
                                          const NetResult &result);
  void dispatch_command_result_notification_();
  void dispatch_sender_frame_(uint32_t now_ms);
  void dispatch_application_ack_();
  void discard_radio_events_();
};

}  // namespace espnow_net_protocol
}  // namespace esphome
