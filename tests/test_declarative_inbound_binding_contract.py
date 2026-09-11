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


def test_binding_without_probe_reports_immediate_dispatch_success() -> None:
    handler = read("declarative_command_handler.h")
    assert "if (binding->completion_probe() == nullptr)" in handler
    assert "this->complete_success_(now_ms)" in handler


def test_light_completion_is_optional_and_compile_time_guarded() -> None:
    schema = read("__init__.py")
    endpoint = read("espnow_net_protocol.h")
    probe = read("light_command_completion_probe.h")
    assert 'CONF_COMPLETION = "completion"' in schema
    assert '"toggled": LightExpectedState.TOGGLED' in schema
    assert 'cg.add_define(\n                    "USE_ESPNOW_NET_PROTOCOL_LIGHT_COMPLETION"' in schema
    assert "USE_ESPNOW_NET_PROTOCOL_LIGHT_COMPLETION" in endpoint
    assert "state_->current_values.is_on()" in probe
    assert 'snapshot.schema.assign("binary-state/v1")' in probe


def test_completion_emits_progress_then_verified_terminal_result() -> None:
    handler = read("declarative_command_handler.h")
    assert "result_.status = NetResultStatus::IN_PROGRESS" in handler
    assert "result_.execution.has_estimated_completion = true" in handler
    assert "expected_snapshot" in handler
    assert "completion_probe()->completed()" in handler
    assert "result_.status = NetResultStatus::SUCCEEDED" in handler
    assert "observed_snapshot" in handler
    assert "NetErrorCode::TIMED_OUT" in handler
