#pragma once

#include <cstdint>

#include "command_codec.h"
#include "protocol_runtime.h"
#include "result_codec.h"

namespace esphome {
namespace espnow_net_protocol {

enum class NetCommandHandlerStartStatus : uint8_t {
  STARTED,
  BUSY,
  INVALID_COMMAND,
  REJECTED,
};

class NetCommandHandler {
 public:
  virtual ~NetCommandHandler() = default;
  virtual NetCommandHandlerStartStatus start(const NetCommand &command,
                                               uint32_t now_ms) = 0;
  virtual void loop(uint32_t now_ms) = 0;
  virtual bool has_result() const = 0;
  virtual bool take_result(NetResult &result) = 0;
  virtual bool cancel(TransactionId transaction_id) = 0;
};

// Called only after a command payload has been decoded. The source fields
// remain claims until the receiver binds them to its authenticated peer.
class NetCommandIdentityObserver {
 public:
  virtual ~NetCommandIdentityObserver() = default;
  virtual void on_command_identity(PeerIndex peer, const NetCommand &command) = 0;
};

enum class InboundCommandDispatcherState : uint8_t {
  IDLE,
  ACTIVE,
  RESULT_READY,
};

struct PendingNetResult {
  PeerIndex peer_index{INVALID_PEER_INDEX};
  TransactionId transaction_id{0};
  EspNowResultPayload payload{};
  bool terminal{true};
};

class InboundCommandDispatcher {
 public:
  void set_handler(NetCommandHandler *handler) { handler_ = handler; }
  void set_identity_observer(NetCommandIdentityObserver *observer) {
    identity_observer_ = observer;
  }

  bool accept(const EspNowInboundApplicationMessage &inbound,
              uint32_t now_ms) {
    if (state_ != InboundCommandDispatcherState::IDLE ||
        inbound.peer_index == INVALID_PEER_INDEX ||
        inbound.message.envelope.kind != EspNowFrameKind::COMMAND ||
        inbound.message.envelope.transaction_id == 0)
      return false;

    peer_index_ = inbound.peer_index;
    transaction_id_ = inbound.message.envelope.transaction_id;
    started_ms_ = now_ms;
    command_ = {};
    if (!command_codec_.decode(transaction_id_, inbound.message.data.data(),
                               inbound.message.data.size(), command_)) {
      this->complete_failure_(NetErrorCode::INVALID_REQUEST,
                              "invalid ESP-NOW command payload", false,
                              now_ms);
      return true;
    }
    if (identity_observer_ != nullptr)
      identity_observer_->on_command_identity(peer_index_, command_);
    if (handler_ == nullptr) {
      this->complete_failure_(NetErrorCode::TARGET_UNAVAILABLE,
                              "no command handler registered", true, now_ms);
      return true;
    }

    const NetCommandHandlerStartStatus status = handler_->start(command_, now_ms);
    if (status == NetCommandHandlerStartStatus::STARTED) {
      state_ = InboundCommandDispatcherState::ACTIVE;
      return true;
    }
    if (status == NetCommandHandlerStartStatus::INVALID_COMMAND) {
      this->complete_failure_(NetErrorCode::INVALID_REQUEST,
                              "command handler rejected invalid command",
                              false, now_ms);
    } else if (status == NetCommandHandlerStartStatus::BUSY) {
      this->complete_failure_(NetErrorCode::TARGET_UNAVAILABLE,
                              "command handler busy", true, now_ms);
    } else {
      this->complete_failure_(NetErrorCode::REMOTE_REJECTED,
                              "command handler rejected command", false,
                              now_ms);
    }
    return true;
  }

  void loop(uint32_t now_ms) {
    if (state_ != InboundCommandDispatcherState::ACTIVE) return;
    if ((now_ms - started_ms_) >= command_.timeout_ms) {
      if (handler_ != nullptr) handler_->cancel(transaction_id_);
      this->complete_failure_(NetErrorCode::TIMED_OUT,
                              "remote command execution timed out", false,
                              now_ms);
      return;
    }
    handler_->loop(now_ms);
    if (!handler_->has_result()) return;
    NetResult result{};
    if (!handler_->take_result(result) ||
        result.transaction_id != transaction_id_ || !result.consistent()) {
      this->complete_failure_(NetErrorCode::INTERNAL_ERROR,
                              "command handler returned invalid result", false,
                              now_ms);
      return;
    }
    this->complete_result_(result);
  }

  bool has_result() const {
    return state_ == InboundCommandDispatcherState::RESULT_READY;
  }

  bool take_result(PendingNetResult &pending) {
    if (!this->has_result()) return false;
    pending = pending_;
    const bool terminal = pending_.terminal;
    pending_ = {};
    if (terminal) {
      this->reset_();
    } else {
      state_ = InboundCommandDispatcherState::ACTIVE;
    }
    return true;
  }

  InboundCommandDispatcherState state() const { return state_; }
  TransactionId active_transaction_id() const { return transaction_id_; }

 private:
  void complete_result_(const NetResult &result) {
    if (this->stage_result_(result)) return;
    NetResult fallback{};
    fallback.transaction_id = transaction_id_;
    fallback.status = NetResultStatus::FAILED;
    fallback.error.code = NetErrorCode::INTERNAL_ERROR;
    fallback.error.message.assign("failed to encode command result");
    fallback.latency_ms = result.latency_ms;
    if (!this->stage_result_(fallback)) this->reset_();
  }

  bool stage_result_(const NetResult &result) {
    PendingNetResult pending{};
    pending.peer_index = peer_index_;
    pending.transaction_id = transaction_id_;
    pending.terminal = result.status != NetResultStatus::IN_PROGRESS;
    if (!result_codec_.encode(result, pending.payload)) {
      return false;
    }
    pending_ = pending;
    state_ = InboundCommandDispatcherState::RESULT_READY;
    return true;
  }

  void complete_failure_(NetErrorCode code, const char *message,
                         bool retryable, uint32_t now_ms) {
    NetResult result{};
    result.transaction_id = transaction_id_;
    result.status = NetResultStatus::FAILED;
    result.error.code = code;
    result.error.retryable = retryable;
    result.error.message.assign(message);
    result.latency_ms = now_ms - started_ms_;
    this->complete_result_(result);
  }

  void reset_() {
    command_ = {};
    pending_ = {};
    peer_index_ = INVALID_PEER_INDEX;
    transaction_id_ = 0;
    started_ms_ = 0;
    state_ = InboundCommandDispatcherState::IDLE;
  }

  NetCommandHandler *handler_{nullptr};
  NetCommandIdentityObserver *identity_observer_{nullptr};
  EspNowCommandCodec command_codec_{};
  EspNowResultCodec result_codec_{};
  NetCommand command_{};
  PendingNetResult pending_{};
  PeerIndex peer_index_{INVALID_PEER_INDEX};
  TransactionId transaction_id_{0};
  uint32_t started_ms_{0};
  InboundCommandDispatcherState state_{InboundCommandDispatcherState::IDLE};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
