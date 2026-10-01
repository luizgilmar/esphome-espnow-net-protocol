"""Host checks: validation/codegen with stubs; these do not compile firmware."""
import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/espnow_net_protocol"


def load_function(name, namespace):
    path = COMPONENT / "__init__.py"
    tree = ast.parse(path.read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    namespace[target.id] = node.value.value
    fn = next(n for n in tree.body if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and n.name == name)
    exec(compile(ast.Module(body=[fn], type_ignores=[]), str(path), "exec"), namespace)
    return namespace[name]


def validate(config, wifi_config):
    namespace = {"cv": SimpleNamespace(Invalid=ValueError), "CONF_CHANNEL": "channel",
                 "fv": SimpleNamespace(full_config=SimpleNamespace(get=lambda: {"wifi": wifi_config}))}
    return load_function("_validate_wifi_reconnect_recovery", namespace)(config)


def test_recovery_validation_with_and_without_heartbeat():
    for heartbeat in (False, True):
        config = {"wifi_reconnect_recovery": True, "channel": 1}
        if heartbeat:
            config["heartbeat"] = {"peer": "hub"}
        assert validate(config, {"fixed_channel": 1}) is config
    assert validate({"channel": 6}, {}) == {"channel": 6}
    assert validate({"wifi_reconnect_recovery": False}, {}) == {"wifi_reconnect_recovery": False}


def test_recovery_rejects_channel_mismatch_and_conflicting_retry():
    for channel, wifi in ((1, {}), (1, {"fixed_channel": 6}), (6, {"fixed_channel": 1}),
                          (1, {"fixed_channel": 1, "fixed_channel_retry_interval": 20000})):
        try:
            validate({"wifi_reconnect_recovery": True, "channel": channel}, wifi)
        except ValueError:
            continue
        raise AssertionError("Invalid recovery configuration accepted")


def test_codegen_recovery_and_heartbeat_are_independent():
    for recovery in (False, True):
        for heartbeat in (False, True):
            calls, defines, lookups = [], [], []
            class Variable:
                def __getattr__(self, name):
                    return lambda *args: calls.append((name, args))
            var, wifi_var = Variable(), object()
            async def register_component(*args):
                pass
            async def get_variable(identifier):
                lookups.append(identifier)
                return wifi_var
            ns = {"CONF_CHANNEL": "channel", "CONF_ID": "id",
                  "CORE": SimpleNamespace(config={"wifi": {"id": "bench_wifi"}}),
                  "cg": SimpleNamespace(add=lambda x: None, add_define=defines.append,
                                        new_Pvariable=lambda x: var, register_component=register_component,
                                        get_variable=get_variable),
                  "wifi": SimpleNamespace(enable_runtime_reconnect_suppression=lambda: None),
                  "include_builtin_idf_component": lambda x: None,
                  "add_idf_sdkconfig_option": lambda *args: None}
            config = {"id": "radio", "diagnostics": False, "wifi_reconnect_recovery": recovery,
                      "channel": 1, "pmk": "0" * 32, "peers": [{"id": "hub", "address": "02:00:00:00:00:01", "lmk": "1" * 32}],
                      "ack_timeout": SimpleNamespace(total_milliseconds=250), "max_attempts": 3,
                      "interruptible_inbound": False}
            if heartbeat:
                config["heartbeat"] = {"peer": "hub", "send_interval": SimpleNamespace(total_milliseconds=5000),
                                       "timeout": SimpleNamespace(total_milliseconds=2000)}
            asyncio.run(load_function("to_code", ns)(config))
            assert (("set_wifi_reconnect_recovery", (wifi_var,)) in calls) == recovery
            assert lookups == (["bench_wifi"] if recovery else [])
            assert ("USE_ESPNOW_APPLICATION_HEARTBEAT" in defines) == heartbeat


def test_recovery_lives_outside_heartbeat_and_preserves_idle_gate():
    heartbeat = (COMPONENT / "application_heartbeat.cpp").read_text(encoding="utf-8")
    recovery = (COMPONENT / "wifi_peer_recovery.cpp").read_text(encoding="utf-8")
    main = (COMPONENT / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    assert "reconnect_peer_" not in heartbeat
    assert "USE_ESPNOW_APPLICATION_HEARTBEAT" not in recovery
    assert "heartbeat_peer_" not in recovery
    assert "reconnect_peer_recovery_tick_(recovery_permitted)" in main
    assert "command_client_state_2_ == CommandClientState::IDLE" in main
    assert "if (reconnect_peer_pending_ && idle)" in recovery
    assert "!runtime_enabled_ || !radio_.initialized() || is_failed()" in recovery
    assert "!reconnect_wifi_->fixed_channel_operation() || reconnect_wifi_->is_disabled()" in recovery
    assert 'log_peer_diagnostics("rc2_before_recovery")' in recovery
    assert 'log_peer_diagnostics("rc2_after_recovery")' in recovery
    assert "i < radio_.peer_count()" in recovery
