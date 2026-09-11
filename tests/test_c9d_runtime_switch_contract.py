from pathlib import Path


C = Path("components/espnow_net_protocol")


def test_native_switch_controls_generic_protocol_runtime() -> None:
    schema = (C / "switch" / "__init__.py").read_text(encoding="utf-8")
    header = (C / "espnow_net_protocol.h").read_text(encoding="utf-8")
    source = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    assert 'DEPENDENCIES = ["espnow_net_protocol"]' in schema
    assert "set_runtime_enabled(bool enabled)" in header
    assert "if (!runtime_enabled_)" in source
    assert "discard_radio_events_();" in source
    assert "index < EspIdfEspNowEncryptedRadio::RX_QUEUE_CAPACITY" in source
    assert (
        "index < EspIdfEspNowEncryptedRadio::SEND_COMPLETION_QUEUE_CAPACITY"
        in source
    )
    assert "while (" not in source


def test_runtime_disable_does_not_deinitialize_radio_callbacks() -> None:
    source = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    disable = source[source.index("void EspNowNetProtocolComponent::set_runtime_enabled") :]
    disable = disable[:disable.index("bool EspNowNetProtocolComponent::send_command")]
    assert "esp_now_deinit" not in disable
    assert "rollback_initialization" not in disable
