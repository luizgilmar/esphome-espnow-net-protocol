#pragma once

#include "esphome/core/component.h"
#include "espidf_espnow_encrypted_radio.h"
#include "protocol_runtime.h"
#include "reliable_sender.h"

namespace esphome {
namespace espnow_net_protocol {

class EspNowNetProtocolComponent : public Component {
 public:
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
    return sender_.start(peer, kind, transaction_id, data, size);
  }
  ReliableSenderState sender_state() const { return sender_.state(); }
  void reset_sender() { sender_.reset(); }
  bool take_application_message(EspNowInboundApplicationMessage &message) {
    return runtime_.take_application_message(message);
  }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  enum class RadioTransmissionOwner : uint8_t { NONE, SENDER, ACK };
  EspIdfEspNowEncryptedRadio radio_{};
  EspNowProtocolRuntime runtime_{};
  ReliableMessageSender sender_{};
  EspNowRetryPolicy retry_policy_{};
  uint64_t local_boot_id_{0};
  uint32_t ack_sequence_{0};
  RadioTransmissionOwner radio_transmission_owner_{
      RadioTransmissionOwner::NONE};
  PeerIndex radio_transmission_peer_{INVALID_PEER_INDEX};

  void process_send_completion_(uint32_t now_ms);
  void process_received_frame_();
  void dispatch_sender_frame_(uint32_t now_ms);
  void dispatch_application_ack_();
};

}  // namespace espnow_net_protocol
}  // namespace esphome
