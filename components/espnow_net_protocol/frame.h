#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol_identity.h"

namespace esphome {
namespace espnow_net_protocol {

enum class EspNowFrameKind : uint8_t {
  COMMAND = 1,
  RESULT = 2,
  ACK = 3,
};

enum class EspNowFrameError : uint8_t {
  NONE,
  INVALID_ARGUMENT,
  MESSAGE_TOO_LARGE,
  INVALID_MAGIC,
  UNSUPPORTED_VERSION,
  INVALID_KIND,
  INVALID_FRAGMENT,
  CORRELATION_MISMATCH,
  CHECKSUM_MISMATCH,
};

struct EspNowFrameEnvelope {
  EspNowFrameKind kind{EspNowFrameKind::COMMAND};
  uint8_t flags{0};
  uint64_t source_boot_id{0};
  TransactionId transaction_id{0};
  uint32_t sequence{0};
};

struct EspNowRadioFrame {
  static constexpr size_t MAX_SIZE = 250;
  BoundedBytes<MAX_SIZE> data{};
};

struct EspNowReassembledMessage {
  static constexpr size_t MAX_SIZE = 640;
  EspNowFrameEnvelope envelope{};
  BoundedBytes<MAX_SIZE> data{};
};

class EspNowFrameCodec {
 public:
  static constexpr uint16_t MAGIC = 0x5458;
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t HEADER_SIZE = 36;
  static constexpr size_t MAX_FRAGMENT_DATA =
      EspNowRadioFrame::MAX_SIZE - HEADER_SIZE;
  static constexpr size_t MAX_MESSAGE_SIZE = EspNowReassembledMessage::MAX_SIZE;
  static constexpr uint8_t MAX_FRAGMENTS = 3;
  static constexpr uint8_t FLAG_ACK_REQUIRED = 0x01;

  uint8_t fragment_count(size_t message_size) const {
    if (message_size == 0 || message_size > MAX_MESSAGE_SIZE) return 0;
    return static_cast<uint8_t>((message_size + MAX_FRAGMENT_DATA - 1U) /
                                MAX_FRAGMENT_DATA);
  }

  bool encode_fragment(const EspNowFrameEnvelope &envelope,
                       const uint8_t *message, size_t message_size,
                       uint8_t fragment_index, EspNowRadioFrame &frame) {
    frame.data.clear();
    const uint8_t count = this->fragment_count(message_size);
    if (message == nullptr || envelope.source_boot_id == 0 ||
        envelope.transaction_id == 0 || envelope.sequence == 0 || count == 0) {
      return this->fail_(message_size > MAX_MESSAGE_SIZE
                             ? EspNowFrameError::MESSAGE_TOO_LARGE
                             : EspNowFrameError::INVALID_ARGUMENT);
    }
    if (!valid_kind_(envelope.kind) || fragment_index >= count) {
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);
    }

    uint8_t bytes[EspNowRadioFrame::MAX_SIZE]{};
    const size_t offset = static_cast<size_t>(fragment_index) * MAX_FRAGMENT_DATA;
    const size_t fragment_size =
        message_size - offset > MAX_FRAGMENT_DATA ? MAX_FRAGMENT_DATA
                                                   : message_size - offset;
    put_u16_(bytes + 0, MAGIC);
    bytes[2] = VERSION;
    bytes[3] = static_cast<uint8_t>(envelope.kind);
    bytes[4] = envelope.flags;
    bytes[5] = fragment_index;
    bytes[6] = count;
    bytes[7] = 0;
    put_u64_(bytes + 8, envelope.source_boot_id);
    put_u64_(bytes + 16, envelope.transaction_id);
    put_u32_(bytes + 24, envelope.sequence);
    put_u16_(bytes + 28, static_cast<uint16_t>(message_size));
    put_u16_(bytes + 30, static_cast<uint16_t>(fragment_size));
    put_u32_(bytes + 32, crc32_(message, message_size));
    std::memcpy(bytes + HEADER_SIZE, message + offset, fragment_size);
    if (!frame.data.assign(bytes, HEADER_SIZE + fragment_size)) {
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);
    }
    this->last_error_ = EspNowFrameError::NONE;
    return true;
  }

  EspNowFrameError last_error() const { return this->last_error_; }

 private:
  friend class EspNowFrameReassembler;

  static bool valid_kind_(EspNowFrameKind kind) {
    return kind == EspNowFrameKind::COMMAND || kind == EspNowFrameKind::RESULT ||
           kind == EspNowFrameKind::ACK;
  }

  bool fail_(EspNowFrameError error) {
    this->last_error_ = error;
    return false;
  }

  static void put_u16_(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8U);
    out[1] = static_cast<uint8_t>(value);
  }
  static void put_u32_(uint8_t *out, uint32_t value) {
    for (uint8_t i = 0; i < 4; i++)
      out[i] = static_cast<uint8_t>(value >> (24U - i * 8U));
  }
  static void put_u64_(uint8_t *out, uint64_t value) {
    for (uint8_t i = 0; i < 8; i++)
      out[i] = static_cast<uint8_t>(value >> (56U - i * 8U));
  }
  static uint16_t get_u16_(const uint8_t *in) {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8U) | in[1]);
  }
  static uint32_t get_u32_(const uint8_t *in) {
    uint32_t value = 0;
    for (uint8_t i = 0; i < 4; i++) value = (value << 8U) | in[i];
    return value;
  }
  static uint64_t get_u64_(const uint8_t *in) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < 8; i++) value = (value << 8U) | in[i];
    return value;
  }
  static uint32_t crc32_(const uint8_t *data, size_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0; index < size; index++) {
      crc ^= data[index];
      for (uint8_t bit = 0; bit < 8; bit++)
        crc = (crc >> 1U) ^ (0xEDB88320U &
                             static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1U))));
    }
    return ~crc;
  }

  EspNowFrameError last_error_{EspNowFrameError::NONE};
};

class EspNowFrameReassembler {
 public:
  bool accept(const uint8_t *frame, size_t frame_size) {
    if (frame == nullptr || frame_size < EspNowFrameCodec::HEADER_SIZE ||
        frame_size > EspNowRadioFrame::MAX_SIZE) {
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);
    }
    if (EspNowFrameCodec::get_u16_(frame) != EspNowFrameCodec::MAGIC)
      return this->fail_(EspNowFrameError::INVALID_MAGIC);
    if (frame[2] != EspNowFrameCodec::VERSION)
      return this->fail_(EspNowFrameError::UNSUPPORTED_VERSION);
    const auto kind = static_cast<EspNowFrameKind>(frame[3]);
    if (!EspNowFrameCodec::valid_kind_(kind))
      return this->fail_(EspNowFrameError::INVALID_KIND);

    const uint8_t fragment_index = frame[5];
    const uint8_t fragment_count = frame[6];
    const uint16_t message_size = EspNowFrameCodec::get_u16_(frame + 28);
    const uint16_t fragment_size = EspNowFrameCodec::get_u16_(frame + 30);
    if (message_size == 0 || message_size > EspNowFrameCodec::MAX_MESSAGE_SIZE ||
        fragment_count == 0 ||
        fragment_count > EspNowFrameCodec::MAX_FRAGMENTS ||
        fragment_count != codec_.fragment_count(message_size) ||
        fragment_index >= fragment_count ||
        fragment_size > EspNowFrameCodec::MAX_FRAGMENT_DATA ||
        frame_size != EspNowFrameCodec::HEADER_SIZE + fragment_size) {
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);
    }
    const size_t offset =
        static_cast<size_t>(fragment_index) * EspNowFrameCodec::MAX_FRAGMENT_DATA;
    const size_t expected_size =
        message_size - offset > EspNowFrameCodec::MAX_FRAGMENT_DATA
            ? EspNowFrameCodec::MAX_FRAGMENT_DATA
            : message_size - offset;
    if (fragment_size != expected_size)
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);

    EspNowFrameEnvelope envelope{};
    envelope.kind = kind;
    envelope.flags = frame[4];
    envelope.source_boot_id = EspNowFrameCodec::get_u64_(frame + 8);
    envelope.transaction_id = EspNowFrameCodec::get_u64_(frame + 16);
    envelope.sequence = EspNowFrameCodec::get_u32_(frame + 24);
    const uint32_t checksum = EspNowFrameCodec::get_u32_(frame + 32);
    if (envelope.source_boot_id == 0 || envelope.transaction_id == 0 ||
        envelope.sequence == 0)
      return this->fail_(EspNowFrameError::INVALID_FRAGMENT);

    if (received_mask_ == 0) {
      envelope_ = envelope;
      message_size_ = message_size;
      fragment_count_ = fragment_count;
      checksum_ = checksum;
    } else if (!matches_(envelope, message_size, fragment_count, checksum)) {
      return this->fail_(EspNowFrameError::CORRELATION_MISMATCH);
    }

    std::memcpy(message_ + offset, frame + EspNowFrameCodec::HEADER_SIZE,
                fragment_size);
    received_mask_ |= static_cast<uint8_t>(1U << fragment_index);
    last_error_ = EspNowFrameError::NONE;
    return true;
  }

  bool complete() const {
    if (fragment_count_ == 0) return false;
    const uint8_t expected = static_cast<uint8_t>((1U << fragment_count_) - 1U);
    return received_mask_ == expected;
  }

  bool take(EspNowReassembledMessage &message) {
    if (!this->validate_complete()) return false;
    message.envelope = envelope_;
    const bool assigned = message.data.assign(message_, message_size_);
    this->reset();
    last_error_ = assigned ? EspNowFrameError::NONE
                           : EspNowFrameError::INVALID_FRAGMENT;
    return assigned;
  }

  bool validate_complete() {
    if (!this->complete()) return false;
    if (EspNowFrameCodec::crc32_(message_, message_size_) == checksum_)
      return true;
    this->reset();
    last_error_ = EspNowFrameError::CHECKSUM_MISMATCH;
    return false;
  }

  const EspNowFrameEnvelope *complete_envelope() const {
    return this->complete() ? &envelope_ : nullptr;
  }

  const uint8_t *complete_data() const {
    return this->complete() ? message_ : nullptr;
  }

  size_t complete_size() const {
    return this->complete() ? message_size_ : 0;
  }

  void release_complete() {
    if (this->complete()) this->reset();
  }

  void reset() {
    envelope_ = {};
    message_size_ = 0;
    checksum_ = 0;
    fragment_count_ = 0;
    received_mask_ = 0;
  }

  EspNowFrameError last_error() const { return last_error_; }

 private:
  bool matches_(const EspNowFrameEnvelope &value, uint16_t message_size,
                uint8_t fragment_count, uint32_t checksum) const {
    return value.kind == envelope_.kind && value.flags == envelope_.flags &&
           value.source_boot_id == envelope_.source_boot_id &&
           value.transaction_id == envelope_.transaction_id &&
           value.sequence == envelope_.sequence && message_size == message_size_ &&
           fragment_count == fragment_count_ && checksum == checksum_;
  }

  bool fail_(EspNowFrameError error) {
    last_error_ = error;
    return false;
  }

  EspNowFrameCodec codec_{};
  EspNowFrameEnvelope envelope_{};
  uint8_t message_[EspNowFrameCodec::MAX_MESSAGE_SIZE]{};
  uint16_t message_size_{0};
  uint32_t checksum_{0};
  uint8_t fragment_count_{0};
  uint8_t received_mask_{0};
  EspNowFrameError last_error_{EspNowFrameError::NONE};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
