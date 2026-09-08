#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frame.h"

namespace esphome {
namespace espnow_net_protocol {

enum class EspNowAckStatus : uint8_t {
  ACCEPTED = 1,
  DUPLICATE = 2,
  REJECTED = 3,
};

using EspNowDeliveryKey = DeliveryIdentity;

struct EspNowAckPayload {
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t ENCODED_SIZE = 22;

  EspNowDeliveryKey acknowledged{};
  EspNowAckStatus status{EspNowAckStatus::REJECTED};
};

class EspNowAckCodec {
 public:
  bool encode(const EspNowAckPayload &payload,
              BoundedBytes<EspNowAckPayload::ENCODED_SIZE> &output) const {
    output.clear();
    if (!payload.acknowledged.valid() || !valid_status_(payload.status))
      return false;
    uint8_t bytes[EspNowAckPayload::ENCODED_SIZE]{};
    bytes[0] = EspNowAckPayload::VERSION;
    bytes[1] = static_cast<uint8_t>(payload.status);
    put_u64_(bytes + 2, payload.acknowledged.source_boot_id);
    put_u64_(bytes + 10, payload.acknowledged.transaction_id);
    put_u32_(bytes + 18, payload.acknowledged.sequence);
    return output.assign(bytes, sizeof(bytes));
  }

  bool decode(const uint8_t *data, size_t size,
              EspNowAckPayload &payload) const {
    if (data == nullptr || size != EspNowAckPayload::ENCODED_SIZE ||
        data[0] != EspNowAckPayload::VERSION)
      return false;
    const auto status = static_cast<EspNowAckStatus>(data[1]);
    if (!valid_status_(status)) return false;
    EspNowAckPayload decoded{};
    decoded.status = status;
    decoded.acknowledged.source_boot_id = get_u64_(data + 2);
    decoded.acknowledged.transaction_id = get_u64_(data + 10);
    decoded.acknowledged.sequence = get_u32_(data + 18);
    if (!decoded.acknowledged.valid()) return false;
    payload = decoded;
    return true;
  }

 private:
  static bool valid_status_(EspNowAckStatus status) {
    return status == EspNowAckStatus::ACCEPTED ||
           status == EspNowAckStatus::DUPLICATE ||
           status == EspNowAckStatus::REJECTED;
  }

  static void put_u32_(uint8_t *out, uint32_t value) {
    for (uint8_t index = 0; index < 4; index++)
      out[index] = static_cast<uint8_t>(value >> (24U - index * 8U));
  }
  static void put_u64_(uint8_t *out, uint64_t value) {
    for (uint8_t index = 0; index < 8; index++)
      out[index] = static_cast<uint8_t>(value >> (56U - index * 8U));
  }
  static uint32_t get_u32_(const uint8_t *in) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < 4; index++) value = (value << 8U) | in[index];
    return value;
  }
  static uint64_t get_u64_(const uint8_t *in) {
    uint64_t value = 0;
    for (uint8_t index = 0; index < 8; index++) value = (value << 8U) | in[index];
    return value;
  }
};

enum class EspNowDeliveryState : uint8_t {
  IDLE,
  READY_TO_SEND,
  WAITING_FOR_ACK,
  ACKNOWLEDGED,
  REJECTED,
  TIMED_OUT,
};

struct EspNowRetryPolicy {
  uint32_t ack_timeout_ms{250};
  uint8_t max_attempts{3};

  bool valid() const { return ack_timeout_ms != 0 && max_attempts != 0; }
};

class EspNowReliableDelivery {
 public:
  bool configure(const EspNowRetryPolicy &policy) {
    if (state_ != EspNowDeliveryState::IDLE || !policy.valid()) return false;
    policy_ = policy;
    return true;
  }

  bool start(const EspNowDeliveryKey &key) {
    if (!key.valid() || active()) return false;
    key_ = key;
    attempt_count_ = 0;
    deadline_ms_ = 0;
    state_ = EspNowDeliveryState::READY_TO_SEND;
    return true;
  }

  bool mark_sent(uint32_t now_ms) {
    if (state_ != EspNowDeliveryState::READY_TO_SEND ||
        attempt_count_ >= policy_.max_attempts)
      return false;
    attempt_count_++;
    deadline_ms_ = now_ms + policy_.ack_timeout_ms;
    state_ = EspNowDeliveryState::WAITING_FOR_ACK;
    return true;
  }

  void loop(uint32_t now_ms) {
    if (state_ != EspNowDeliveryState::WAITING_FOR_ACK ||
        static_cast<int32_t>(now_ms - deadline_ms_) < 0)
      return;
    state_ = attempt_count_ < policy_.max_attempts
                 ? EspNowDeliveryState::READY_TO_SEND
                 : EspNowDeliveryState::TIMED_OUT;
  }

  bool accept_ack(const EspNowAckPayload &ack) {
    if (state_ != EspNowDeliveryState::WAITING_FOR_ACK ||
        !(ack.acknowledged == key_))
      return false;
    state_ = ack.status == EspNowAckStatus::REJECTED
                 ? EspNowDeliveryState::REJECTED
                 : EspNowDeliveryState::ACKNOWLEDGED;
    return true;
  }

  void reset() {
    key_ = {};
    attempt_count_ = 0;
    deadline_ms_ = 0;
    state_ = EspNowDeliveryState::IDLE;
  }

  bool active() const {
    return state_ == EspNowDeliveryState::READY_TO_SEND ||
           state_ == EspNowDeliveryState::WAITING_FOR_ACK;
  }
  EspNowDeliveryState state() const { return state_; }
  uint8_t attempt_count() const { return attempt_count_; }
  const EspNowDeliveryKey &key() const { return key_; }

 private:
  EspNowRetryPolicy policy_{};
  EspNowDeliveryKey key_{};
  uint32_t deadline_ms_{0};
  uint8_t attempt_count_{0};
  EspNowDeliveryState state_{EspNowDeliveryState::IDLE};
};

enum class EspNowDuplicateObservation : uint8_t { NEW_MESSAGE, DUPLICATE };

class EspNowDuplicateCache {
 public:
  static constexpr size_t CAPACITY = 8;

  EspNowDuplicateObservation observe(const EspNowDeliveryKey &key) {
    if (this->contains(key)) return EspNowDuplicateObservation::DUPLICATE;
    if (!key.valid()) return EspNowDuplicateObservation::DUPLICATE;
    entries_[next_] = key;
    next_ = (next_ + 1U) % CAPACITY;
    if (count_ < CAPACITY) count_++;
    return EspNowDuplicateObservation::NEW_MESSAGE;
  }

  size_t size() const { return count_; }

  bool contains(const EspNowDeliveryKey &key) const {
    if (!key.valid()) return false;
    for (size_t index = 0; index < count_; index++)
      if (entries_[index] == key) return true;
    return false;
  }

 private:
  EspNowDeliveryKey entries_[CAPACITY]{};
  size_t count_{0};
  size_t next_{0};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
