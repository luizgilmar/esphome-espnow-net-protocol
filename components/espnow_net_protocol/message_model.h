#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol_identity.h"

namespace esphome {
namespace espnow_net_protocol {

enum class NetResultStatus : uint8_t { SUCCEEDED, IN_PROGRESS, REJECTED, FAILED };
enum class NetStateCompleteness : uint8_t { NOT_PROVIDED, PARTIAL, COMPLETE };
enum class NetErrorCode : uint8_t {
  NONE, INVALID_REQUEST, TARGET_UNAVAILABLE, TRANSPORT_UNAVAILABLE,
  CONNECTION_FAILED, AUTHENTICATION_FAILED, TIMED_OUT, PROTOCOL_ERROR,
  REMOTE_REJECTED, INTERNAL_ERROR,
};

struct NetCommand {
  static constexpr size_t MAX_DEVICE_ID_LENGTH = 63;
  static constexpr size_t MAX_RESOURCE_LENGTH = 63;
  static constexpr size_t MAX_NAME_LENGTH = 47;
  static constexpr size_t MAX_PAYLOAD_SIZE = 256;
  TransactionId transaction_id{0};
  BoundedText<MAX_DEVICE_ID_LENGTH> device_id{};
  BoundedText<MAX_RESOURCE_LENGTH> resource{};
  BoundedText<MAX_NAME_LENGTH> name{};
  BoundedBytes<MAX_PAYLOAD_SIZE> payload{};
  uint32_t timeout_ms{0};
  bool valid() const {
    return transaction_id != 0 && !device_id.empty() && !name.empty() &&
           timeout_ms != 0;
  }
};

struct NetStateSnapshot {
  static constexpr size_t MAX_SCHEMA_LENGTH = 31;
  static constexpr size_t MAX_DATA_SIZE = 256;
  NetStateCompleteness completeness{NetStateCompleteness::NOT_PROVIDED};
  BoundedText<MAX_SCHEMA_LENGTH> schema{};
  BoundedBytes<MAX_DATA_SIZE> data{};
  bool consistent() const {
    if (completeness == NetStateCompleteness::NOT_PROVIDED)
      return schema.empty() && data.empty();
    return !schema.empty() && !data.empty();
  }
};

struct NetExecutionContext {
  bool started{false};
  bool has_estimated_completion{false};
  uint32_t estimated_completion_ms{0};
  bool has_progress{false};
  uint8_t progress_percent{0};
  bool has_expected_final_state{false};
  NetStateSnapshot expected_final_state{};
  bool consistent() const {
    if (has_estimated_completion && (!started || estimated_completion_ms == 0))
      return false;
    if (has_progress && (!started || progress_percent > 100)) return false;
    if (has_expected_final_state)
      return started && expected_final_state.consistent() &&
             expected_final_state.completeness !=
                 NetStateCompleteness::NOT_PROVIDED;
    return expected_final_state.consistent() &&
           expected_final_state.completeness ==
               NetStateCompleteness::NOT_PROVIDED;
  }
};

struct NetError {
  static constexpr size_t MAX_MESSAGE_LENGTH = 95;
  NetErrorCode code{NetErrorCode::NONE};
  bool retryable{false};
  BoundedText<MAX_MESSAGE_LENGTH> message{};
  bool consistent() const {
    return code == NetErrorCode::NONE
               ? !retryable && message.empty()
               : !message.empty();
  }
};

struct NetResult {
  TransactionId transaction_id{0};
  NetResultStatus status{NetResultStatus::FAILED};
  NetExecutionContext execution{};
  NetStateSnapshot remote_state{};
  NetError error{};
  uint32_t latency_ms{0};
  bool consistent() const {
    if (transaction_id == 0 || !execution.consistent() ||
        !remote_state.consistent() || !error.consistent()) return false;
    if (status == NetResultStatus::IN_PROGRESS)
      return execution.started && execution.has_estimated_completion &&
             error.code == NetErrorCode::NONE;
    if (status == NetResultStatus::SUCCEEDED)
      return execution.started && error.code == NetErrorCode::NONE;
    return error.code != NetErrorCode::NONE;
  }
};

}  // namespace espnow_net_protocol
}  // namespace esphome
