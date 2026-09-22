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

class IdentityProbe : public NetCommandIdentityObserver {
 public:
  void on_command_identity(PeerIndex peer, const NetCommand &command) override {
    ++observations;
    last_peer = peer;
    last_boot_id = command.source_boot_id;
  }
  unsigned observations{0};
  PeerIndex last_peer{INVALID_PEER_INDEX};
  uint64_t last_boot_id{0};
};

EspNowInboundApplicationMessage command_message(TransactionId transaction_id,
                                                bool identified = false,
                                                const char *name = "turn_on") {
  NetCommand command{};
  command.transaction_id = transaction_id;
  command.device_id.assign("relay-room");
  command.resource.assign("relay/1");
  command.name.assign(name);
  command.timeout_ms = 100;
  if (identified) {
    assert(command.source_device_id.assign("tx"));
    command.source_boot_id = 123456;
  }
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
  IdentityProbe probe{};
  dispatcher.set_identity_observer(&probe);
  dispatcher.set_handler(&handler);
  assert(dispatcher.accept(command_message(77), 10));
  assert(probe.observations == 1 && probe.last_peer == 2 && probe.last_boot_id == 0);
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
  assert(dispatcher.accept(command_message(79, true), 210));
  assert(probe.observations == 3 && probe.last_peer == 2 &&
         probe.last_boot_id == 123456);
  InboundCommandDispatcher interrupt{};
  FakeHandler interrupt_handler{};
  interrupt.set_handler(&interrupt_handler);
  assert(!interrupt.accept_interrupt(command_message(80, true, "turn_on"),
                                     dispatcher, 211));
  assert(!interrupt.accept_interrupt(command_message(80, false, "stop"),
                                     dispatcher, 211));
  assert(interrupt.accept_interrupt(command_message(80, true, "stop"),
                                    dispatcher, 211));
  assert(interrupt.state() == InboundCommandDispatcherState::ACTIVE);
  dispatcher.set_identity_observer(nullptr);
  return 0;
}
