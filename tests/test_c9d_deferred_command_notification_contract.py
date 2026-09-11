from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_command_results_use_one_bounded_deferred_notification_slot() -> None:
    header = read("espnow_net_protocol.h")
    assert "NetResult command_result_notification_{}" in header
    assert "PeerIndex command_result_notification_peer_" in header
    assert "bool command_result_notification_ready_{false}" in header
    assert "std::vector" not in header
    assert "std::queue" not in header


def test_deep_failure_and_decode_paths_do_not_call_device_observer() -> None:
    source = read("espnow_net_protocol.cpp")
    progress = source[source.index("if (result.status == NetResultStatus::IN_PROGRESS)") :]
    progress = progress[:progress.index("void EspNowNetProtocolComponent::fail_command_client_")]
    finish = source[source.index("void EspNowNetProtocolComponent::finish_command_client_") :]
    finish = finish[:finish.index("void EspNowNetProtocolComponent::clear_command_client_")]
    assert "queue_command_result_notification_" in progress
    assert "on_net_command_result" not in progress
    assert "queue_command_result_notification_" in finish
    assert "on_net_command_result" not in finish


def test_observer_is_called_only_from_shallow_loop_dispatch() -> None:
    source = read("espnow_net_protocol.cpp")
    loop = source[source.index("void EspNowNetProtocolComponent::loop()") :]
    loop = loop[:loop.index("void EspNowNetProtocolComponent::process_application_message_")]
    dispatch = source[source.index("void EspNowNetProtocolComponent::dispatch_command_result_notification_") :]
    dispatch = dispatch[:dispatch.index("void EspNowNetProtocolComponent::process_send_completion_")]
    assert "dispatch_command_result_notification_();" in loop
    assert "on_net_command_result" in dispatch
    assert source.count("on_net_command_result") == 1


def test_next_correlated_result_waits_until_previous_notification_is_consumed() -> None:
    source = read("espnow_net_protocol.cpp")
    process = source[source.index("void EspNowNetProtocolComponent::process_application_message_") :]
    process = process[:process.index("void EspNowNetProtocolComponent::process_dispatcher_")]
    assert "command_client_result &&" in process
    assert "command_result_notification_ready_" in process
