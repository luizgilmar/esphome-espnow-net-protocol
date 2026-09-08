#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "frame.h"
#include "message_model.h"

namespace esphome {
namespace espnow_net_protocol {

enum class EspNowResultCodecError : uint8_t {
  NONE,
  INVALID_RESULT,
  PAYLOAD_TOO_LARGE,
  INVALID_PAYLOAD,
  UNSUPPORTED_VERSION,
};

struct EspNowResultPayload {
  static constexpr size_t MAX_SIZE = EspNowFrameCodec::MAX_MESSAGE_SIZE;
  BoundedBytes<MAX_SIZE> data{};
};

class EspNowResultCodec {
 public:
  static constexpr uint8_t VERSION = 1;
  static constexpr size_t HEADER_SIZE = 20;
  static constexpr uint8_t FLAG_STARTED = 0x01;
  static constexpr uint8_t FLAG_ESTIMATED = 0x02;
  static constexpr uint8_t FLAG_PROGRESS = 0x04;
  static constexpr uint8_t FLAG_EXPECTED_STATE = 0x08;
  static constexpr uint8_t FLAG_REMOTE_STATE = 0x10;
  static constexpr uint8_t FLAG_ERROR = 0x20;

  bool encode(const NetResult &result, EspNowResultPayload &payload) {
    payload.data.clear();
    if (!result.consistent()) return fail_(EspNowResultCodecError::INVALID_RESULT);
    const NetStateSnapshot &expected = result.execution.expected_final_state;
    const NetStateSnapshot &remote = result.remote_state;
    const size_t expected_schema = result.execution.has_expected_final_state
                                       ? expected.schema.size() : 0;
    const size_t expected_data = result.execution.has_expected_final_state
                                     ? expected.data.size() : 0;
    const bool has_remote = remote.completeness !=
                            NetStateCompleteness::NOT_PROVIDED;
    const size_t remote_schema = has_remote ? remote.schema.size() : 0;
    const size_t remote_data = has_remote ? remote.data.size() : 0;
    const bool has_error = result.error.code != NetErrorCode::NONE;
    const size_t error_size = has_error ? result.error.message.size() : 0;
    const size_t total = HEADER_SIZE + expected_schema + expected_data +
                         remote_schema + remote_data + error_size;
    if (total > EspNowResultPayload::MAX_SIZE)
      return fail_(EspNowResultCodecError::PAYLOAD_TOO_LARGE);

    uint8_t bytes[EspNowResultPayload::MAX_SIZE]{};
    bytes[0] = VERSION;
    bytes[1] = static_cast<uint8_t>(result.status);
    uint8_t flags = result.execution.started ? FLAG_STARTED : 0;
    if (result.execution.has_estimated_completion) flags |= FLAG_ESTIMATED;
    if (result.execution.has_progress) flags |= FLAG_PROGRESS;
    if (result.execution.has_expected_final_state) flags |= FLAG_EXPECTED_STATE;
    if (has_remote) flags |= FLAG_REMOTE_STATE;
    if (has_error) flags |= FLAG_ERROR;
    bytes[2] = flags;
    bytes[3] = static_cast<uint8_t>(expected.completeness);
    bytes[4] = static_cast<uint8_t>(remote.completeness);
    bytes[5] = static_cast<uint8_t>(result.error.code);
    bytes[6] = result.error.retryable ? 1 : 0;
    bytes[7] = result.execution.progress_percent;
    put_u32_(bytes + 8, result.execution.estimated_completion_ms);
    bytes[12] = static_cast<uint8_t>(expected_schema);
    put_u16_(bytes + 13, static_cast<uint16_t>(expected_data));
    bytes[15] = static_cast<uint8_t>(remote_schema);
    put_u16_(bytes + 16, static_cast<uint16_t>(remote_data));
    bytes[18] = static_cast<uint8_t>(error_size);
    bytes[19] = 0;
    size_t offset = HEADER_SIZE;
    append_(bytes, offset, expected.schema.c_str(), expected_schema);
    append_(bytes, offset, expected.data.data(), expected_data);
    append_(bytes, offset, remote.schema.c_str(), remote_schema);
    append_(bytes, offset, remote.data.data(), remote_data);
    append_(bytes, offset, result.error.message.c_str(), error_size);
    if (!payload.data.assign(bytes, offset))
      return fail_(EspNowResultCodecError::PAYLOAD_TOO_LARGE);
    last_error_ = EspNowResultCodecError::NONE;
    return true;
  }

  bool decode(TransactionId transaction_id, const uint8_t *data, size_t size,
              uint32_t latency_ms, NetResult &result) {
    result = {};
    if (transaction_id == 0 || data == nullptr || size < HEADER_SIZE)
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    if (data[0] != VERSION)
      return fail_(EspNowResultCodecError::UNSUPPORTED_VERSION);
    const uint8_t flags = data[2];
    if ((flags & ~(FLAG_STARTED | FLAG_ESTIMATED | FLAG_PROGRESS |
                   FLAG_EXPECTED_STATE | FLAG_REMOTE_STATE | FLAG_ERROR)) != 0)
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    const auto result_status = static_cast<NetResultStatus>(data[1]);
    const auto expected_completeness =
        static_cast<NetStateCompleteness>(data[3]);
    const auto remote_completeness =
        static_cast<NetStateCompleteness>(data[4]);
    const auto error_code = static_cast<NetErrorCode>(data[5]);
    if (!valid_result_(result_status) ||
        !valid_completeness_(expected_completeness) ||
        !valid_completeness_(remote_completeness) ||
        !valid_error_(error_code))
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    const size_t expected_schema = data[12];
    const size_t expected_data = get_u16_(data + 13);
    const size_t remote_schema = data[15];
    const size_t remote_data = get_u16_(data + 16);
    const size_t error_size = data[18];
    const size_t expected = HEADER_SIZE + expected_schema + expected_data +
                            remote_schema + remote_data + error_size;
    if (expected != size ||
        expected_schema > NetStateSnapshot::MAX_SCHEMA_LENGTH ||
        remote_schema > NetStateSnapshot::MAX_SCHEMA_LENGTH ||
        expected_data > NetStateSnapshot::MAX_DATA_SIZE ||
        remote_data > NetStateSnapshot::MAX_DATA_SIZE ||
        error_size > NetError::MAX_MESSAGE_LENGTH)
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);

    result.transaction_id = transaction_id;
    result.status = result_status;
    result.execution.started = (flags & FLAG_STARTED) != 0;
    result.execution.has_estimated_completion = (flags & FLAG_ESTIMATED) != 0;
    result.execution.has_progress = (flags & FLAG_PROGRESS) != 0;
    result.execution.has_expected_final_state =
        (flags & FLAG_EXPECTED_STATE) != 0;
    result.execution.estimated_completion_ms = get_u32_(data + 8);
    result.execution.progress_percent = data[7];
    result.latency_ms = latency_ms;
    size_t offset = HEADER_SIZE;
    if (!decode_state_(data, offset, expected_schema, expected_data,
                       expected_completeness,
                       result.execution.expected_final_state) ||
        !decode_state_(data, offset, remote_schema, remote_data,
                       remote_completeness,
                       result.remote_state))
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    if ((flags & FLAG_ERROR) != 0) {
      char message[NetError::MAX_MESSAGE_LENGTH + 1]{};
      if (error_size != 0) std::memcpy(message, data + offset, error_size);
      result.error.code = error_code;
      result.error.retryable = data[6] != 0;
      if (!result.error.message.assign(message))
        return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    }
    offset += error_size;
    if (offset != size || !flags_consistent_(flags, expected_schema,
                                             expected_data, remote_schema,
                                             remote_data, error_size) ||
        !result.consistent())
      return fail_(EspNowResultCodecError::INVALID_PAYLOAD);
    last_error_ = EspNowResultCodecError::NONE;
    return true;
  }

  EspNowResultCodecError last_error() const { return last_error_; }

 private:
  static bool valid_result_(NetResultStatus value) {
    return value == NetResultStatus::SUCCEEDED ||
           value == NetResultStatus::IN_PROGRESS ||
           value == NetResultStatus::REJECTED ||
           value == NetResultStatus::FAILED;
  }
  static bool valid_completeness_(NetStateCompleteness value) {
    return value == NetStateCompleteness::NOT_PROVIDED ||
           value == NetStateCompleteness::PARTIAL ||
           value == NetStateCompleteness::COMPLETE;
  }
  static bool valid_error_(NetErrorCode value) {
    return static_cast<uint8_t>(value) <=
           static_cast<uint8_t>(NetErrorCode::INTERNAL_ERROR);
  }
  bool fail_(EspNowResultCodecError error) { last_error_ = error; return false; }
  static bool flags_consistent_(uint8_t flags, size_t es, size_t ed,
                                size_t rs, size_t rd, size_t error) {
    return (((flags & FLAG_EXPECTED_STATE) != 0) == (es != 0 && ed != 0)) &&
           (((flags & FLAG_REMOTE_STATE) != 0) == (rs != 0 && rd != 0)) &&
           (((flags & FLAG_ERROR) != 0) == (error != 0));
  }
  static bool decode_state_(const uint8_t *data, size_t &offset,
                            size_t schema_size, size_t data_size,
                            NetStateCompleteness completeness,
                            NetStateSnapshot &state) {
    if (schema_size == 0 && data_size == 0) {
      state = {};
      return completeness == NetStateCompleteness::NOT_PROVIDED;
    }
    if (schema_size == 0 || data_size == 0 ||
        completeness == NetStateCompleteness::NOT_PROVIDED)
      return false;
    char schema[NetStateSnapshot::MAX_SCHEMA_LENGTH + 1]{};
    std::memcpy(schema, data + offset, schema_size);
    offset += schema_size;
    state.completeness = completeness;
    if (!state.schema.assign(schema) ||
        !state.data.assign(data + offset, data_size)) return false;
    offset += data_size;
    return state.consistent();
  }
  static void append_(uint8_t *out, size_t &offset, const void *value,
                      size_t size) {
    if (size != 0) std::memcpy(out + offset, value, size);
    offset += size;
  }
  static void put_u16_(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8U); out[1] = value;
  }
  static void put_u32_(uint8_t *out, uint32_t value) {
    for (uint8_t i = 0; i < 4; i++) out[i] = value >> (24U - i * 8U);
  }
  static uint16_t get_u16_(const uint8_t *in) {
    return static_cast<uint16_t>((static_cast<uint16_t>(in[0]) << 8U) | in[1]);
  }
  static uint32_t get_u32_(const uint8_t *in) {
    uint32_t value = 0; for (uint8_t i = 0; i < 4; i++) value = (value << 8U) | in[i]; return value;
  }
  EspNowResultCodecError last_error_{EspNowResultCodecError::NONE};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
