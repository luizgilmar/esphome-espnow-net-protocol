#pragma once

#include <cstddef>
#include <cstdint>

#include "radio_queue.h"
#include "reliability.h"

namespace esphome {
namespace espnow_net_protocol {

enum class EspNowProtocolAcceptResult : uint8_t {
  FRAGMENT_ACCEPTED,
  APPLICATION_MESSAGE_READY,
  DELIVERY_ACK_READY,
  DUPLICATE_ACK_READY,
  REJECTED_ACK_READY,
  INVALID_FRAME,
};

struct EspNowInboundApplicationMessage {
  uint8_t peer_index{0};
  EspNowReassembledMessage message{};
};

struct EspNowPendingApplicationAck {
  uint8_t peer_index{0};
  EspNowAckPayload payload{};
};

class EspNowProtocolRuntime {
 public:
  EspNowProtocolRuntime() = default;
  explicit EspNowProtocolRuntime(uint64_t local_boot_id)
      : local_boot_id_(local_boot_id) {}

  bool configure(uint64_t local_boot_id) {
    if (local_boot_id == 0 || local_boot_id_ != 0 || assembling_ ||
        application_message_ready_ || delivery_ack_ready_ ||
        application_ack_ready_)
      return false;
    local_boot_id_ = local_boot_id;
    return true;
  }

  EspNowProtocolAcceptResult accept(const EspNowReceivedFrame &received) {
    if (local_boot_id_ == 0 || received.frame.data.empty())
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    if (application_message_ready_ || application_ack_ready_) {
      rejected_count_++;
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    }

    if (assembling_ && received.peer_index != active_peer_index_) {
      reassembler_.reset();
      assembling_ = false;
    }
    if (!reassembler_.accept(received.frame.data.data(),
                             received.frame.data.size())) {
      if (reassembler_.last_error() !=
          EspNowFrameError::CORRELATION_MISMATCH)
        return EspNowProtocolAcceptResult::INVALID_FRAME;
      // A lost fragment must not block a new message indefinitely. Replace the
      // partial assembly only after the incoming frame has passed basic codec
      // validation and then attempt it once as the start of the new message.
      reassembler_.reset();
      assembling_ = false;
      if (!reassembler_.accept(received.frame.data.data(),
                               received.frame.data.size()))
        return EspNowProtocolAcceptResult::INVALID_FRAME;
    }
    if (!assembling_) active_peer_index_ = received.peer_index;
    if (!reassembler_.complete()) {
      assembling_ = true;
      return EspNowProtocolAcceptResult::FRAGMENT_ACCEPTED;
    }
    assembling_ = false;

    if (!reassembler_.validate_complete()) {
      invalid_frame_count_++;
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    }

    const EspNowFrameEnvelope *envelope = reassembler_.complete_envelope();
    if (envelope == nullptr)
      return EspNowProtocolAcceptResult::INVALID_FRAME;

    if (envelope->kind == EspNowFrameKind::ACK)
      return this->accept_delivery_ack_();
    return this->accept_application_message_(*envelope);
  }

  bool take_application_message(EspNowInboundApplicationMessage &message) {
    if (!application_message_ready_) return false;
    message = {};
    const EspNowFrameEnvelope *envelope = reassembler_.complete_envelope();
    if (envelope == nullptr ||
        !message.message.data.assign(reassembler_.complete_data(),
                                     reassembler_.complete_size()))
      return false;
    message.peer_index = application_peer_index_;
    message.message.envelope = *envelope;
    reassembler_.release_complete();
    application_message_ready_ = false;
    return true;
  }

  bool application_message_ready() const { return application_message_ready_; }

  EspNowFrameKind application_message_kind() const {
    if (!application_message_ready_) return EspNowFrameKind::ACK;
    const EspNowFrameEnvelope *envelope = reassembler_.complete_envelope();
    return envelope == nullptr ? EspNowFrameKind::ACK : envelope->kind;
  }

  bool take_delivery_ack(EspNowAckPayload &ack) {
    if (!delivery_ack_ready_) return false;
    ack = delivery_ack_;
    delivery_ack_ready_ = false;
    return true;
  }

  bool take_application_ack(EspNowPendingApplicationAck &ack) {
    if (!application_ack_ready_) return false;
    ack = application_ack_;
    application_ack_ready_ = false;
    return true;
  }

  uint32_t invalid_frame_count() const { return invalid_frame_count_; }
  uint32_t duplicate_count() const { return duplicate_count_; }
  uint32_t rejected_count() const { return rejected_count_; }

 private:
  EspNowProtocolAcceptResult accept_delivery_ack_() {
    EspNowAckPayload decoded{};
    if (!ack_codec_.decode(reassembler_.complete_data(),
                           reassembler_.complete_size(), decoded)) {
      reassembler_.release_complete();
      invalid_frame_count_++;
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    }
    if (delivery_ack_ready_) {
      reassembler_.release_complete();
      rejected_count_++;
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    }
    delivery_ack_ = decoded;
    delivery_ack_ready_ = true;
    reassembler_.release_complete();
    return EspNowProtocolAcceptResult::DELIVERY_ACK_READY;
  }

  EspNowProtocolAcceptResult accept_application_message_(
      const EspNowFrameEnvelope &envelope) {
    if ((envelope.flags & EspNowFrameCodec::FLAG_ACK_REQUIRED) == 0) {
      reassembler_.release_complete();
      invalid_frame_count_++;
      return EspNowProtocolAcceptResult::INVALID_FRAME;
    }
    const EspNowDeliveryKey key{envelope.source_boot_id,
                                envelope.transaction_id,
                                envelope.sequence};
    EspNowAckStatus status = EspNowAckStatus::REJECTED;
    EspNowProtocolAcceptResult result =
        EspNowProtocolAcceptResult::REJECTED_ACK_READY;

    if (duplicates_.contains(key)) {
      duplicate_count_++;
      status = EspNowAckStatus::DUPLICATE;
      result = EspNowProtocolAcceptResult::DUPLICATE_ACK_READY;
      reassembler_.release_complete();
    } else if (!application_message_ready_ &&
               envelope.kind != EspNowFrameKind::ACK) {
      application_peer_index_ = active_peer_index_;
      application_message_ready_ = true;
      duplicates_.observe(key);
      status = EspNowAckStatus::ACCEPTED;
      result = EspNowProtocolAcceptResult::APPLICATION_MESSAGE_READY;
    } else {
      rejected_count_++;
      reassembler_.release_complete();
    }
    application_ack_.peer_index = active_peer_index_;
    application_ack_.payload.acknowledged = key;
    application_ack_.payload.status = status;
    application_ack_ready_ = true;
    return result;
  }

  uint64_t local_boot_id_{0};
  EspNowFrameReassembler reassembler_{};
  EspNowAckCodec ack_codec_{};
  EspNowDuplicateCache duplicates_{};
  EspNowAckPayload delivery_ack_{};
  EspNowPendingApplicationAck application_ack_{};
  uint32_t invalid_frame_count_{0};
  uint32_t duplicate_count_{0};
  uint32_t rejected_count_{0};
  uint8_t active_peer_index_{0};
  uint8_t application_peer_index_{0};
  bool application_message_ready_{false};
  bool delivery_ack_ready_{false};
  bool application_ack_ready_{false};
  bool assembling_{false};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
