from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_component_owns_runtime_sender_and_radio() -> None:
    header = read("espnow_net_protocol.h")
    assert "EspIdfEspNowEncryptedRadio radio_" in header
    assert "EspNowProtocolRuntime runtime_" in header
    assert "ReliableMessageSender sender_" in header


def test_component_processes_only_bounded_work_per_loop() -> None:
    source = read("espnow_net_protocol.cpp")
    assert "process_send_completion_(now_ms)" in source
    assert "process_received_frame_()" in source
    assert "dispatch_application_ack_()" in source
    assert "dispatch_sender_frame_(now_ms)" in source
    assert "while (" not in source


def test_radio_completions_have_serialized_ownership() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "RadioTransmissionOwner" in header
    assert "RadioTransmissionOwner::SENDER" in source
    assert "RadioTransmissionOwner::ACK" in source
    assert "radio_transmission_peer_" in source


def test_retry_policy_is_declarative_and_bounded() -> None:
    init = read("__init__.py")
    assert 'CONF_ACK_TIMEOUT = "ack_timeout"' in init
    assert 'CONF_MAX_ATTEMPTS = "max_attempts"' in init
    assert 'default="250ms"' in init
    assert "cv.int_range(min=1, max=8)" in init


def test_runtime_retains_one_reassembly_buffer_until_consumed() -> None:
    frame = read("frame.h")
    runtime = read("protocol_runtime.h")
    assert "bool validate_complete()" in frame
    assert "void release_complete()" in frame
    assert "reassembler_.complete_data()" in runtime
    assert "EspNowInboundApplicationMessage application_message_" not in runtime
