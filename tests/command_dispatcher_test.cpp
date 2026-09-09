#include <cassert>

#include "components/espnow_net_protocol/command_dispatcher.h"

using namespace esphome::espnow_net_protocol;

class FakeHandler : public NetCommandHandler {
 public:
  NetCommandHandlerStartStatus start(const NetCommand &command,
                                      uint32_t) override {
    transaction_id_ = command.transaction_id;
    return start_status;
  }
  void loop(uint32_t) override {}
  bool has_result() const override { return result_ready; }
  bool take_result(NetResult &result) override {
    if (!result_ready) return false;
    result = next_result;
    result_ready = false;
    return true;
  }
  bool cancel(TransactionId transaction_id) override {
    canceled = transaction_id == transaction_id_;
    return canceled;
  }

  NetCommandHandlerStartStatus start_status{
      NetCommandHandlerStartStatus::STARTED};
  NetResult next_result{};
  bool result_ready{false};
  bool canceled{false};
  TransactionId transaction_id_{0};
};

EspNowInboundApplicationMessage command_message(TransactionId transaction_id) {
  NetCommand command{};
  command.transaction_id = transaction_id;
  command.device_id.assign("relay-room");
  command.resource.assign("relay/1");
  command.name.assign("turn_on");
  command.timeout_ms = 100;
  EspNowCommandPayload payload{};
  EspNowCommandCodec codec{};
  assert(codec.encode(command, payload));
  EspNowInboundApplicationMessage inbound{};
  inbound.peer_index = 2;
  inbound.message.envelope.kind = EspNowFrameKind::COMMAND;
  inbound.message.envelope.transaction_id = transaction_id;
  assert(inbound.message.data.assign(payload.data.data(), payload.data.size()));
  return inbound;
}

int main() {
  InboundCommandDispatcher dispatcher{};
  FakeHandler handler{};
  dispatcher.set_handler(&handler);
  assert(dispatcher.accept(command_message(77), 10));
  assert(dispatcher.state() == InboundCommandDispatcherState::ACTIVE);

  handler.next_result.transaction_id = 77;
  handler.next_result.status = NetResultStatus::IN_PROGRESS;
  handler.next_result.execution.started = true;
  handler.next_result.execution.has_estimated_completion = true;
  handler.next_result.execution.estimated_completion_ms = 50;
  handler.result_ready = true;
  dispatcher.loop(20);
  PendingNetResult pending{};
  assert(dispatcher.take_result(pending));
  assert(!pending.terminal);
  assert(dispatcher.state() == InboundCommandDispatcherState::ACTIVE);

  handler.next_result = {};
  handler.next_result.transaction_id = 77;
  handler.next_result.status = NetResultStatus::SUCCEEDED;
  handler.next_result.execution.started = true;
  handler.result_ready = true;
  dispatcher.loop(30);
  assert(dispatcher.take_result(pending));
  assert(pending.terminal);
  assert(dispatcher.state() == InboundCommandDispatcherState::IDLE);

  assert(dispatcher.accept(command_message(78), 100));
  dispatcher.loop(200);
  assert(handler.canceled);
  assert(dispatcher.take_result(pending));
  assert(pending.terminal);
  return 0;
}
