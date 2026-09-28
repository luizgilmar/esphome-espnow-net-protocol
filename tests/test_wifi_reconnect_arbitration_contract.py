from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components" / "espnow_net_protocol"


def read(name: str) -> str:
    return (COMPONENT / name).read_text(encoding="utf-8")


def test_codegen_requires_wifi_and_enables_opt_in_api() -> None:
    schema = read("__init__.py")
    assert "from esphome.components import light, wifi" in schema
    assert 'DEPENDENCIES = ["wifi"]' in schema
    assert "wifi.enable_runtime_reconnect_suppression()" in schema


def test_policy_is_bounded_and_owns_timing() -> None:
    header = read("espidf_espnow_encrypted_radio.h")
    assert "WIFI_RECONNECT_GRACE_BASE_MS = 10000" in header
    assert "WIFI_RECONNECT_GRACE_JITTER_MS = 20000" in header
    assert "WIFI_RECONNECT_HOLD_BASE_MS = 30000" in header
    assert "WIFI_RECONNECT_HOLD_JITTER_MS = 30000" in header
    assert "WIFI_RECONNECT_WINDOW_MS = 8000" in header
    assert "WiFiArbitrationState" in header


def test_policy_pairs_every_accepted_request_with_release() -> None:
    source = read("espidf_espnow_encrypted_radio.cpp")
    assert "request_reconnect_suppression(" in source
    assert "wifi_reconnect_suppression_held_ = true" in source
    assert "release_reconnect_suppression()" in source
    assert "wifi_reconnect_suppression_held_ = false" in source
    assert "wifi_arbitration_jitter_ms_(" in source


def test_policy_does_not_control_wifi_driver_directly() -> None:
    source = read("espidf_espnow_encrypted_radio.cpp")
    arbitration_start = source.index(
        "void EspIdfEspNowEncryptedRadio::process_wifi_arbitration_"
    )
    arbitration_end = source.index(
        "void EspIdfEspNowEncryptedRadio::schedule_radio_recovery_",
        arbitration_start,
    )
    arbitration = source[arbitration_start:arbitration_end]
    assert "esp_wifi_scan_start" not in arbitration
    assert "esp_wifi_scan_stop" not in arbitration
    assert "esp_wifi_disconnect" not in arbitration
    assert "esp_wifi_set_channel" not in arbitration

