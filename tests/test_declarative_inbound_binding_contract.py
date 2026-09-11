from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "espnow_net_protocol"


def read(name: str) -> str:
    return (COMPONENT / name).read_text(encoding="utf-8")


def test_inbound_schema_is_bounded_and_automation_native() -> None:
    schema = read("__init__.py")
    assert 'CONF_INBOUND = "inbound"' in schema
    assert 'CONF_BINDINGS = "bindings"' in schema
    assert "cv.Length(min=1, max=16)" in schema
    assert "automation.validate_automation" in schema
    assert "single=True" in schema
    assert "automation.build_automation(binding, [], binding_config)" in schema
    assert 'cg.add_define("USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND")' in schema
    assert "duplicate inbound resource/command binding" in schema


def test_handler_matches_device_resource_and_command_without_heap_storage() -> None:
    handler = read("declarative_command_handler.h")
    endpoint = read("espnow_net_protocol.h")
    assert "public NetCommandHandler" in handler
    assert "static constexpr size_t MAX_BINDINGS = 16" in handler
    assert "std::array<DeclarativeCommandBinding *, MAX_BINDINGS>" in handler
    assert "request.device_id.c_str()" in handler
    assert "request.resource.c_str()" in handler
    assert "request.name.c_str()" in handler
    assert "binding->trigger()" in handler
    assert "Binding dispatched device=%s resource=%s command=%s tx=%llu" in handler
    assert "configure_declarative_inbound" in endpoint
    assert "command_dispatcher_.set_handler(&declarative_command_handler_)" in endpoint
    assert endpoint.count("USE_ESPNOW_NET_PROTOCOL_DECLARATIVE_INBOUND") >= 2


def test_immediate_result_reports_dispatch_not_verified_final_state() -> None:
    handler = read("declarative_command_handler.h")
    assert "result_.status = NetResultStatus::SUCCEEDED" in handler
    assert "result_.execution.started = true" in handler
    assert "result_.remote_state" not in handler
