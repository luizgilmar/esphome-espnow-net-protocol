#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esphome/core/automation.h"
#include "esphome/core/log.h"

#include "command_dispatcher.h"
#include "command_completion_probe.h"

namespace esphome {
namespace espnow_net_protocol {

class DeclarativeCommandBinding : public Trigger<> {
 public:
  void set_resource(const char *resource) { resource_ = resource; }
  void set_command(const char *command) { command_ = command; }
  void set_completion_probe(CommandCompletionProbe *probe) { probe_ = probe; }
  void set_completion_timeout(uint32_t timeout_ms) {
    completion_timeout_ms_ = timeout_ms;
  }

  bool matches(const NetCommand &request) const {
    return resource_ != nullptr && command_ != nullptr &&
           std::strcmp(request.resource.c_str(), resource_) == 0 &&
           std::strcmp(request.name.c_str(), command_) == 0;
  }

  CommandCompletionProbe *completion_probe() const { return probe_; }
  uint32_t completion_timeout_ms() const { return completion_timeout_ms_; }

 private:
  const char *resource_{nullptr};
  const char *command_{nullptr};
  CommandCompletionProbe *probe_{nullptr};
  uint32_t completion_timeout_ms_{2000};
};

class DeclarativeCommandHandler final : public NetCommandHandler {
 public:
  static constexpr size_t MAX_BINDINGS = 16;

  void set_device_id(const char *device_id) { device_id_ = device_id; }

  bool add_binding(DeclarativeCommandBinding *binding) {
    if (binding == nullptr || binding_count_ >= bindings_.size()) return false;
    bindings_[binding_count_++] = binding;
    return true;
  }

  NetCommandHandlerStartStatus start(const NetCommand &request,
                                      uint32_t now_ms) override {
    if (result_ready_ || active_binding_ != nullptr)
      return NetCommandHandlerStartStatus::BUSY;
    if (device_id_ == nullptr ||
        std::strcmp(request.device_id.c_str(), device_id_) != 0)
      return NetCommandHandlerStartStatus::REJECTED;

    DeclarativeCommandBinding *binding = nullptr;
    for (size_t index = 0; index < binding_count_; index++) {
      if (bindings_[index]->matches(request)) {
        binding = bindings_[index];
        break;
      }
    }
    if (binding == nullptr)
      return NetCommandHandlerStartStatus::INVALID_COMMAND;

    transaction_id_ = request.transaction_id;
    started_ms_ = now_ms;
    active_binding_ = binding;
    if (binding->completion_probe() != nullptr)
      binding->completion_probe()->begin();
    ESP_LOGI("espnow_net_protocol.inbound",
             "Binding dispatched device=%s resource=%s command=%s tx=%llu",
             request.device_id.c_str(), request.resource.c_str(),
             request.name.c_str(),
             static_cast<unsigned long long>(request.transaction_id));
    binding->trigger();
    if (binding->completion_probe() == nullptr) {
      this->complete_success_(now_ms);
    } else {
      result_ = {};
      result_.transaction_id = transaction_id_;
      result_.status = NetResultStatus::IN_PROGRESS;
      result_.execution.started = true;
      result_.execution.has_estimated_completion = true;
      result_.execution.estimated_completion_ms =
          binding->completion_timeout_ms();
      result_.execution.has_expected_final_state =
          binding->completion_probe()->expected_snapshot(
              result_.execution.expected_final_state);
      result_.latency_ms = 0;
      result_ready_ = true;
    }
    return NetCommandHandlerStartStatus::STARTED;
  }

  void loop(uint32_t now_ms) override {
    if (active_binding_ == nullptr || result_ready_ ||
        active_binding_->completion_probe() == nullptr)
      return;
    if (active_binding_->completion_probe()->completed()) {
      this->complete_success_(now_ms);
      return;
    }
    if (now_ms - started_ms_ >= active_binding_->completion_timeout_ms())
      this->complete_timeout_(now_ms);
  }
  bool has_result() const override { return result_ready_; }

  bool take_result(NetResult &result) override {
    if (!result_ready_) return false;
    result = result_;
    const bool terminal = result_.status != NetResultStatus::IN_PROGRESS;
    result_ = {};
    result_ready_ = false;
    if (terminal) this->reset_();
    return true;
  }

  bool cancel(TransactionId transaction_id) override {
    if (transaction_id_ != transaction_id)
      return false;
    this->reset_();
    return true;
  }

  size_t binding_count() const { return binding_count_; }

 private:
  void complete_success_(uint32_t now_ms) {
    result_ = {};
    result_.transaction_id = transaction_id_;
    result_.status = NetResultStatus::SUCCEEDED;
    result_.execution.started = true;
    if (active_binding_ != nullptr &&
        active_binding_->completion_probe() != nullptr)
      active_binding_->completion_probe()->observed_snapshot(
          result_.remote_state);
    result_.latency_ms = now_ms - started_ms_;
    result_ready_ = true;
  }

  void complete_timeout_(uint32_t now_ms) {
    result_ = {};
    result_.transaction_id = transaction_id_;
    result_.status = NetResultStatus::FAILED;
    result_.execution.started = true;
    result_.error.code = NetErrorCode::TIMED_OUT;
    result_.error.retryable = false;
    result_.error.message.assign("declared completion state not observed");
    result_.latency_ms = now_ms - started_ms_;
    result_ready_ = true;
  }

  void reset_() {
    result_ = {};
    result_ready_ = false;
    active_binding_ = nullptr;
    transaction_id_ = 0;
    started_ms_ = 0;
  }

  const char *device_id_{nullptr};
  std::array<DeclarativeCommandBinding *, MAX_BINDINGS> bindings_{};
  size_t binding_count_{0};
  NetResult result_{};
  bool result_ready_{false};
  DeclarativeCommandBinding *active_binding_{nullptr};
  TransactionId transaction_id_{0};
  uint32_t started_ms_{0};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
