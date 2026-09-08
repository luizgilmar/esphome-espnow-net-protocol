#pragma once

#include <cstddef>
#include <cstdint>

#include "frame.h"
#include "reliability.h"

namespace esphome {
namespace espnow_net_protocol {

enum class ReliableSenderState : uint8_t {
  IDLE,
  READY,
  FRAME_IN_FLIGHT,
  WAITING_FOR_ACK,
  ACKNOWLEDGED,
  REJECTED,
  TIMED_OUT,
};

class ReliableMessageSender {
 public:
  bool configure(uint64_t local_boot_id, const EspNowRetryPolicy &policy) {
    if (state_ != ReliableSenderState::IDLE || local_boot_id == 0 ||
        !delivery_.configure(policy))
      return false;
    local_boot_id_ = local_boot_id;
    return true;
  }

  bool start(PeerIndex peer_index, EspNowFrameKind kind,
             TransactionId transaction_id, const uint8_t *message,
             size_t message_size) {
    if (local_boot_id_ == 0 || state_ != ReliableSenderState::IDLE ||
        peer_index == INVALID_PEER_INDEX || transaction_id == 0 ||
        kind == EspNowFrameKind::ACK || message == nullptr ||
        !message_.assign(message, message_size))
      return false;
    const uint8_t count = codec_.fragment_count(message_size);
    if (count == 0) return false;
    sequence_++;
    if (sequence_ == 0) sequence_++;
    envelope_.kind = kind;
    envelope_.flags = EspNowFrameCodec::FLAG_ACK_REQUIRED;
    envelope_.source_boot_id = local_boot_id_;
    envelope_.transaction_id = transaction_id;
    envelope_.sequence = sequence_;
    peer_index_ = peer_index;
    fragment_count_ = count;
    fragment_index_ = 0;
    frame_in_flight_ = false;
    if (!delivery_.start({local_boot_id_, transaction_id, sequence_})) {
      this->reset();
      return false;
    }
    state_ = ReliableSenderState::READY;
    return true;
  }

  void loop(uint32_t now_ms) {
    delivery_.loop(now_ms);
    switch (delivery_.state()) {
      case EspNowDeliveryState::READY_TO_SEND:
        if (!frame_in_flight_) {
          fragment_index_ = 0;
          state_ = ReliableSenderState::READY;
        }
        break;
      case EspNowDeliveryState::WAITING_FOR_ACK:
        state_ = ReliableSenderState::WAITING_FOR_ACK;
        break;
      case EspNowDeliveryState::ACKNOWLEDGED:
        state_ = ReliableSenderState::ACKNOWLEDGED;
        break;
      case EspNowDeliveryState::REJECTED:
        state_ = ReliableSenderState::REJECTED;
        break;
      case EspNowDeliveryState::TIMED_OUT:
        state_ = ReliableSenderState::TIMED_OUT;
        break;
      case EspNowDeliveryState::IDLE:
        break;
    }
  }

  bool take_frame(PeerIndex &peer_index, EspNowRadioFrame &frame) {
    if (state_ != ReliableSenderState::READY || frame_in_flight_ ||
        fragment_index_ >= fragment_count_ ||
        !codec_.encode_fragment(envelope_, message_.data(), message_.size(),
                                fragment_index_, frame))
      return false;
    peer_index = peer_index_;
    frame_in_flight_ = true;
    state_ = ReliableSenderState::FRAME_IN_FLIGHT;
    return true;
  }

  bool complete_frame(bool succeeded, uint32_t now_ms) {
    if (!frame_in_flight_) return false;
    frame_in_flight_ = false;
    if (!succeeded) {
      if (!delivery_.mark_sent(now_ms)) return false;
      state_ = ReliableSenderState::WAITING_FOR_ACK;
      return true;
    }
    fragment_index_++;
    if (fragment_index_ < fragment_count_) {
      state_ = ReliableSenderState::READY;
      return true;
    }
    if (!delivery_.mark_sent(now_ms)) return false;
    state_ = ReliableSenderState::WAITING_FOR_ACK;
    return true;
  }

  bool accept_ack(const EspNowAckPayload &ack) {
    if (!delivery_.accept_ack(ack)) return false;
    state_ = delivery_.state() == EspNowDeliveryState::ACKNOWLEDGED
                 ? ReliableSenderState::ACKNOWLEDGED
                 : ReliableSenderState::REJECTED;
    return true;
  }

  void reset() {
    delivery_.reset();
    message_.clear();
    envelope_ = {};
    peer_index_ = INVALID_PEER_INDEX;
    fragment_count_ = 0;
    fragment_index_ = 0;
    frame_in_flight_ = false;
    state_ = ReliableSenderState::IDLE;
  }

  ReliableSenderState state() const { return state_; }
  bool active() const {
    return state_ == ReliableSenderState::READY ||
           state_ == ReliableSenderState::FRAME_IN_FLIGHT ||
           state_ == ReliableSenderState::WAITING_FOR_ACK;
  }
  uint8_t attempt_count() const { return delivery_.attempt_count(); }
  const EspNowDeliveryKey &delivery_key() const { return delivery_.key(); }
  PeerIndex peer_index() const { return peer_index_; }
  bool frame_in_flight() const { return frame_in_flight_; }

 private:
  uint64_t local_boot_id_{0};
  uint32_t sequence_{0};
  PeerIndex peer_index_{INVALID_PEER_INDEX};
  EspNowFrameCodec codec_{};
  EspNowReliableDelivery delivery_{};
  EspNowFrameEnvelope envelope_{};
  BoundedBytes<EspNowFrameCodec::MAX_MESSAGE_SIZE> message_{};
  uint8_t fragment_count_{0};
  uint8_t fragment_index_{0};
  bool frame_in_flight_{false};
  ReliableSenderState state_{ReliableSenderState::IDLE};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
