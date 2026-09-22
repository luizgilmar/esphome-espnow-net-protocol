from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_native_yaml_action_is_registered_without_lambda() -> None:
    actions = read("actions.py")
    init = read("__init__.py")
    assert '"espnow_net_protocol.send_command"' in actions
    assert "cv.Required(CONF_PEER)" in actions
    assert "cv.Required(CONF_DEVICE)" in actions
    assert "cv.Required(CONF_RESOURCE)" in actions
    assert "cv.Required(CONF_COMMAND)" in actions
    assert "from . import actions" in init
    assert "lambda" not in actions


def test_endpoint_owns_bounded_declarative_command_lifecycle() -> None:
    header = read("espnow_net_protocol.h")
    source = read("espnow_net_protocol.cpp")
    assert "enum class CommandClientState" in header
    assert "NetCommand command_client_command_{}" in header
    assert "std::vector" not in header
    assert "std::queue" not in header
    assert "ReliableMessageOwner::COMMAND_CLIENT" in source
    assert "WAITING_FOR_DELIVERY_ACK" in source
    assert "WAITING_FOR_RESULT" in source
    assert "functional result timed out" in source


def test_functional_result_is_correlated_separately_from_delivery_ack() -> None:
    runtime = read("protocol_runtime.h")
    source = read("espnow_net_protocol.cpp")
    assert "application_message_transaction_id() const" in runtime
    assert "application_message_peer_index() const" in runtime
    assert "matching_command_client_" in source
    assert "result_codec_.decode" in source
    assert "Command result tx=%llu" in source
