from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_background_result_is_a_single_shared_sender_owner() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "bool start_background_result(PeerIndex peer, const NetResult &result)" in header
    assert "BACKGROUND_RESULT" in header
    assert "std::queue" not in header
    assert "std::vector" not in header
    start = source[source.index("bool EspNowNetProtocolComponent::start_background_result(") :]
    start = start[:start.index("bool EspNowNetProtocolComponent::cancel_command(")]
    assert "reliable_message_owner_ != ReliableMessageOwner::NONE" in start
    assert "sender_.state() != ReliableSenderState::IDLE" in start
    assert "result_codec_.encode(result, encoded)" in start
    assert "EspNowFrameKind::RESULT" in start
    assert "ReliableMessageOwner::BACKGROUND_RESULT" in start
    assert "background_result_transaction_id_ = result.transaction_id" in start


def test_background_result_releases_sender_on_every_terminal_delivery() -> None:
    source = read("espnow_net_protocol.cpp")
    branch = source[source.index(
        "if (reliable_message_owner_ == ReliableMessageOwner::BACKGROUND_RESULT)"
    ) :]
    branch = branch[:branch.index(
        "if (reliable_message_owner_ != ReliableMessageOwner::DISPATCHER_RESULT)"
    )]
    assert "ReliableSenderState::ACKNOWLEDGED" in branch
    assert "ReliableSenderState::REJECTED" in branch
    assert "ReliableSenderState::TIMED_OUT" in branch
    assert branch.count("sender_.reset();") == 2
    assert branch.count("reliable_message_owner_ = ReliableMessageOwner::NONE;") == 2
    assert branch.count("background_result_completion_ready_ = true;") == 2


def test_background_completion_is_a_single_bounded_mailbox() -> None:
    header = read("espnow_net_protocol.h")
    assert "take_background_result_completion" in header
    assert "background_result_completion_ready_" in header
    assert "background_result_transaction_id_" in header
    assert "background_result_succeeded_" in header


def test_unsolicited_results_use_bounded_observers_without_raw_mailbox_theft() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "class NetUnsolicitedResultObserver" in header
    assert "NetUnsolicitedResultObserver *unsolicited_result_observers_[2]" in header
    assert "set_unsolicited_result_observer" in header
    assert "dispatch_unsolicited_result_(inbound)" in source
    dispatch = source[source.index(
        "bool EspNowNetProtocolComponent::dispatch_unsolicited_result_("
    ) :]
    dispatch = dispatch[:dispatch.index(
        "void EspNowNetProtocolComponent::process_dispatcher_("
    )]
    assert "result_codec_.decode" in dispatch
    assert "on_unsolicited_net_result" in dispatch
