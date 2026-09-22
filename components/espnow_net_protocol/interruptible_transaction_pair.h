#pragma once

#include <cstdint>

namespace esphome {
namespace espnow_net_protocol {

// Two independent application identities for one long-running operation and
// its interrupt. This object owns no radio buffers or command payloads.
class InterruptibleTransactionPair {
 public:
  bool begin_operation(uint64_t transaction_id) {
    if (transaction_id == 0 || operation_id_ != 0 || interrupt_id_ != 0)
      return false;
    operation_id_ = transaction_id;
    interrupted_ = false;
    return true;
  }

  bool begin_interrupt(uint64_t transaction_id) {
    if (transaction_id == 0 || operation_id_ == 0 || interrupt_id_ != 0 ||
        transaction_id == operation_id_)
      return false;
    interrupt_id_ = transaction_id;
    return true;
  }

  // A failed stop never changes the status of the original operation.
  bool finish_interrupt(uint64_t transaction_id, bool stopped) {
    if (transaction_id == 0 || transaction_id != interrupt_id_) return false;
    interrupt_id_ = 0;
    if (stopped && operation_id_ != 0) interrupted_ = true;
    return true;
  }

  bool finish_operation(uint64_t transaction_id) {
    if (transaction_id == 0 || transaction_id != operation_id_) return false;
    operation_id_ = 0;
    interrupted_ = false;
    return true;
  }

  bool operation_active() const { return operation_id_ != 0; }
  bool interrupt_active() const { return interrupt_id_ != 0; }
  bool operation_interrupted() const { return interrupted_; }
  uint64_t operation_id() const { return operation_id_; }
  uint64_t interrupt_id() const { return interrupt_id_; }

 private:
  uint64_t operation_id_{0};
  uint64_t interrupt_id_{0};
  bool interrupted_{false};
};

static_assert(sizeof(InterruptibleTransactionPair) <= 24,
              "interrupt correlation must remain bounded");

}  // namespace espnow_net_protocol
}  // namespace esphome
