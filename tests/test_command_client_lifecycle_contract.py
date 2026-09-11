from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_component_exposes_one_generic_outbound_command_client() -> None:
    header = read("espnow_net_protocol.h")
    assert "class NetCommandResultObserver" in header
    assert "bool start_command(PeerIndex peer, const NetCommand &command" in header
    assert "set_command_result_observer" in header
    assert "active_command_transaction_id() const" in header
    assert "std::vector" not in header
    assert "std::queue" not in header


def test_yaml_action_uses_the_same_generic_command_client() -> None:
    source = read("espnow_net_protocol.cpp")
    send = source[source.index("bool EspNowNetProtocolComponent::send_command(") :]
    send = send[:send.index("void EspNowNetProtocolComponent::setup()")]
    assert "start_command(peer, request, now_ms)" in send
    wrapper = send[:send.index("bool EspNowNetProtocolComponent::start_command(")]
    assert "sender_.start" not in wrapper


def test_progress_is_observed_without_closing_the_transaction() -> None:
    source = read("espnow_net_protocol.cpp")
    branch = source[source.index("if (result.status == NetResultStatus::IN_PROGRESS)") :]
    branch = branch[:branch.index("finish_command_client_(result);")]
    assert "command_client_progress_count_++" in branch
    assert "on_net_command_result" in branch
    assert "command_client_state_ = CommandClientState::IDLE" not in branch


def test_every_terminal_path_notifies_then_releases_the_generic_client() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "void fail_command_client_" in header
    assert "void finish_command_client_" in header
    finish = source[source.index("void EspNowNetProtocolComponent::finish_command_client_") :]
    finish = finish[:finish.index("void EspNowNetProtocolComponent::process_send_completion_")]
    assert "last_terminal_transaction_id_ = result.transaction_id" in finish
    assert "command_client_state_ = CommandClientState::IDLE" in finish
    assert "on_net_command_result(peer, result)" in finish


def test_timeout_and_delivery_failures_are_terminal_result_events() -> None:
    source = read("espnow_net_protocol.cpp")
    assert "fail_command_client_(NetErrorCode::TIMED_OUT" in source
    assert "fail_command_client_(NetErrorCode::REMOTE_REJECTED" in source
    failure = source[source.index("void EspNowNetProtocolComponent::fail_command_client_") :]
    failure = failure[:failure.index("void EspNowNetProtocolComponent::finish_command_client_")]
    assert "NetResult result{}" in failure
    assert "result.status = NetResultStatus::FAILED" in failure
    assert "finish_command_client_(result)" in failure


def test_most_recent_late_terminal_is_consumed_without_raw_mailbox_pollution() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "last_terminal_peer_" in header
    assert "last_terminal_transaction_id_" in header
    assert "inbound_matches_last_terminal_" in source
    assert "Late terminal result ignored" in source
