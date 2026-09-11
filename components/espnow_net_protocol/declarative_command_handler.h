#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "esphome/core/automation.h"
#include "esphome/core/log.h"

#include "command_dispatcher.h"

namespace esphome {
namespace espnow_net_protocol {

class DeclarativeCommandBinding : public Trigger<> {
 public:
  void set_resource(const char *resource) { resource_ = resource; }
  void set_command(const char *command) { command_ = command; }

  bool matches(const NetCommand &request) const {
    return resource_ != nullptr && command_ != nullptr &&
           std::strcmp(request.resource.c_str(), resource_) == 0 &&
           std::strcmp(request.name.c_str(), command_) == 0;
  }

 private:
  const char *resource_{nullptr};
  const char *command_{nullptr};
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
    (void) now_ms;
    if (result_ready_) return NetCommandHandlerStartStatus::BUSY;
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

    result_ = {};
    result_.transaction_id = request.transaction_id;
    result_.status = NetResultStatus::SUCCEEDED;
    result_.execution.started = true;
    result_.latency_ms = 0;
    ESP_LOGI("espnow_net_protocol.inbound",
             "Binding dispatched device=%s resource=%s command=%s tx=%llu",
             request.device_id.c_str(), request.resource.c_str(),
             request.name.c_str(),
             static_cast<unsigned long long>(request.transaction_id));
    binding->trigger();
    result_ready_ = true;
    return NetCommandHandlerStartStatus::STARTED;
  }

  void loop(uint32_t now_ms) override { (void) now_ms; }
  bool has_result() const override { return result_ready_; }

  bool take_result(NetResult &result) override {
    if (!result_ready_) return false;
    result = result_;
    result_ = {};
    result_ready_ = false;
    return true;
  }

  bool cancel(TransactionId transaction_id) override {
    if (!result_ready_ || result_.transaction_id != transaction_id)
      return false;
    result_ = {};
    result_ready_ = false;
    return true;
  }

  size_t binding_count() const { return binding_count_; }

 private:
  const char *device_id_{nullptr};
  std::array<DeclarativeCommandBinding *, MAX_BINDINGS> bindings_{};
  size_t binding_count_{0};
  NetResult result_{};
  bool result_ready_{false};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
