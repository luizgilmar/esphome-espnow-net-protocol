#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frame.h"
#include "message_model.h"

namespace esphome {
namespace espnow_net_protocol {

enum class EspNowCommandCodecError : uint8_t {
  NONE,
  INVALID_REQUEST,
  INVALID_PAYLOAD,
  UNSUPPORTED_VERSION,
};

struct EspNowCommandPayload {
  static constexpr size_t MAX_SIZE = 511;
  BoundedBytes<MAX_SIZE> data{};
};

class EspNowCommandCodec {
 public:
  static constexpr uint8_t VERSION = 1;
  static constexpr uint8_t IDENTITY_VERSION = 2;
  static constexpr size_t HEADER_SIZE = 10;

  bool encode(const NetCommand &request, EspNowCommandPayload &payload) {
    payload.data.clear();
    if (!request.valid()) return fail_(EspNowCommandCodecError::INVALID_REQUEST);
    const size_t device_size = request.device_id.size();
    const size_t resource_size = request.resource.size();
    const size_t command_size = request.name.size();
    const size_t body_size = request.payload.size();
    const size_t source_size = request.source_device_id.size();
    const size_t total = HEADER_SIZE + device_size + resource_size +
                         command_size + body_size + (source_size ? 9 + source_size : 0);
    if (device_size > UINT8_MAX || resource_size > UINT8_MAX ||
        command_size > UINT8_MAX || body_size > UINT16_MAX ||
        total > EspNowCommandPayload::MAX_SIZE)
      return fail_(EspNowCommandCodecError::INVALID_REQUEST);

    uint8_t bytes[EspNowCommandPayload::MAX_SIZE]{};
    bytes[0] = source_size ? IDENTITY_VERSION : VERSION;
    bytes[1] = static_cast<uint8_t>(device_size);
    bytes[2] = static_cast<uint8_t>(resource_size);
    bytes[3] = static_cast<uint8_t>(command_size);
    put_u16_(bytes + 4, static_cast<uint16_t>(body_size));
    put_u32_(bytes + 6, request.timeout_ms);
    size_t offset = HEADER_SIZE;
    copy_(bytes, offset, request.device_id.c_str(), device_size);
    copy_(bytes, offset, request.resource.c_str(), resource_size);
    copy_(bytes, offset, request.name.c_str(), command_size);
    if (body_size != 0) {
      std::memcpy(bytes + offset, request.payload.data(), body_size);
      offset += body_size;
    }
    if (source_size != 0) {
      bytes[offset++] = static_cast<uint8_t>(source_size);
      put_u64_(bytes + offset, request.source_boot_id);
      offset += 8;
      copy_(bytes, offset, request.source_device_id.c_str(), source_size);
    }
    if (!payload.data.assign(bytes, offset))
      return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
    last_error_ = EspNowCommandCodecError::NONE;
    return true;
  }

  bool decode(TransactionId transaction_id, const uint8_t *data, size_t size,
              NetCommand &request) {
    request = {};
    if (transaction_id == 0 || data == nullptr || size < HEADER_SIZE)
      return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
    if (data[0] != VERSION && data[0] != IDENTITY_VERSION)
      return fail_(EspNowCommandCodecError::UNSUPPORTED_VERSION);
    const size_t device_size = data[1];
    const size_t resource_size = data[2];
    const size_t command_size = data[3];
    const size_t body_size = get_u16_(data + 4);
    const uint32_t timeout_ms = get_u32_(data + 6);
    const size_t expected = HEADER_SIZE + device_size + resource_size +
                            command_size + body_size;
    if (device_size == 0 ||
        device_size > NetCommand::MAX_DEVICE_ID_LENGTH ||
        resource_size > NetCommand::MAX_RESOURCE_LENGTH ||
        command_size == 0 || command_size > NetCommand::MAX_NAME_LENGTH ||
        body_size > NetCommand::MAX_PAYLOAD_SIZE || timeout_ms == 0 ||
        expected > size ||
        (data[0] == VERSION && expected != size))
      return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);

    size_t source_size = 0;
    uint64_t source_boot_id = 0;
    if (data[0] == IDENTITY_VERSION) {
      if (size < expected + 9) return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
      source_size = data[expected];
      source_boot_id = get_u64_(data + expected + 1);
      if (source_size == 0 || source_size > NetCommand::MAX_DEVICE_ID_LENGTH ||
          source_boot_id == 0 || expected + 9 + source_size != size)
        return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
    }

    size_t offset = HEADER_SIZE;
    char device[NetCommand::MAX_DEVICE_ID_LENGTH + 1]{};
    char resource[NetCommand::MAX_RESOURCE_LENGTH + 1]{};
    char command[NetCommand::MAX_NAME_LENGTH + 1]{};
    copy_text_(device, data, offset, device_size);
    copy_text_(resource, data, offset, resource_size);
    copy_text_(command, data, offset, command_size);
    char source[NetCommand::MAX_DEVICE_ID_LENGTH + 1]{};
    if (source_size != 0) {
      std::memcpy(source, data + expected + 9, source_size);
      if (std::memchr(source, '\0', source_size) != nullptr)
        return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
    }
    request.transaction_id = transaction_id;
    request.timeout_ms = timeout_ms;
    request.source_boot_id = source_boot_id;
    if (!request.device_id.assign(device) ||
        !request.resource.assign(resource) ||
        !request.name.assign(command) ||
        !request.payload.assign(data + offset, body_size) ||
        (source_size != 0 && !request.source_device_id.assign(source)) ||
        !request.valid())
      return fail_(EspNowCommandCodecError::INVALID_PAYLOAD);
    last_error_ = EspNowCommandCodecError::NONE;
    return true;
  }

  EspNowCommandCodecError last_error() const { return last_error_; }

 private:
  bool fail_(EspNowCommandCodecError error) {
    last_error_ = error;
    return false;
  }
  static void copy_(uint8_t *out, size_t &offset, const char *value,
                    size_t size) {
    if (size != 0) std::memcpy(out + offset, value, size);
    offset += size;
  }
  static void copy_text_(char *out, const uint8_t *data, size_t &offset,
                         size_t size) {
    if (size != 0) std::memcpy(out, data + offset, size);
    out[size] = '\0';
    offset += size;
  }
  static void put_u16_(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8U);
    out[1] = static_cast<uint8_t>(value);
  }
  static void put_u32_(uint8_t *out, uint32_t value) {
    for (uint8_t index = 0; index < 4; index++)
      out[index] = static_cast<uint8_t>(value >> (24U - index * 8U));
  }
  static void put_u64_(uint8_t *out, uint64_t value) {
    for (uint8_t index = 0; index < 8; ++index)
      out[index] = static_cast<uint8_t>(value >> (56U - index * 8U));
  }
  static uint64_t get_u64_(const uint8_t *in) {
    uint64_t value = 0;
    for (uint8_t index = 0; index < 8; ++index) value = (value << 8U) | in[index];
    return value;
  }
  static uint16_t get_u16_(const uint8_t *in) {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8U) | in[1]);
  }
  static uint32_t get_u32_(const uint8_t *in) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < 4; index++) value = (value << 8U) | in[index];
    return value;
  }

  EspNowCommandCodecError last_error_{EspNowCommandCodecError::NONE};
};

static_assert(EspNowCommandPayload::MAX_SIZE <=
                  EspNowFrameCodec::MAX_MESSAGE_SIZE,
              "ESP-NOW command payload must fit the frame protocol");

}  // namespace espnow_net_protocol
}  // namespace esphome
