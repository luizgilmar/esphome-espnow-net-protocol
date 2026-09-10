from pathlib import Path


C = Path("components/espnow_net_protocol")


def read(name: str) -> str:
    return (C / name).read_text(encoding="utf-8")


def test_radio_waits_for_a_stable_channel_before_initialization() -> None:
    header = read("espidf_espnow_encrypted_radio.h")
    source = read("espidf_espnow_encrypted_radio.cpp")
    assert "CHANNEL_STABILIZATION_MS = 5000" in header
    assert "channel_stable_since_ms_" in header
    assert "now_ms - channel_stable_since_ms_ < CHANNEL_STABILIZATION_MS" in source
    assert "channel_stable_ = false" in source


def test_failed_initialization_is_rate_limited_and_diagnostic() -> None:
    header = read("espidf_espnow_encrypted_radio.h")
    source = read("espidf_espnow_encrypted_radio.cpp")
    assert "INITIALIZATION_RETRY_MS = 5000" in header
    assert "last_initialization_error_" in header
    assert "esp_err_to_name" in source
    assert "last_initialization_attempt_ms_" in source
