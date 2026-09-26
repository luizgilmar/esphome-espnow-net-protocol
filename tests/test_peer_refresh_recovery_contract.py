from pathlib import Path


C = Path("components/espnow_net_protocol")


def test_timed_out_reliable_delivery_requests_bounded_peer_refresh() -> None:
    endpoint = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    radio_h = (C / "espidf_espnow_encrypted_radio.h").read_text(encoding="utf-8")
    radio_cpp = (C / "espidf_espnow_encrypted_radio.cpp").read_text(encoding="utf-8")
    assert "request_peer_refresh(sender_.peer_index())" in endpoint
    assert "PEER_REFRESH_COOLDOWN_MS = 30000" in radio_h
    assert "peer_refresh_pending_mask_" in radio_h
    assert "process_peer_refresh_(now_ms)" in radio_cpp
    assert "now_ms - peer_refresh_last_ms_[peer_index]" in radio_cpp
    assert "esp_now_del_peer(configured_peer->address)" in radio_cpp
    assert "add_configured_peer_(peer_index)" in radio_cpp
    assert "peer_refresh_ok=%u peer_refresh_failed=%u" in endpoint


def test_peer_refresh_is_local_and_waits_for_stable_channel() -> None:
    radio_cpp = (C / "espidf_espnow_encrypted_radio.cpp").read_text(encoding="utf-8")
    stable = radio_cpp.index(
        "if (now_ms - channel_stable_since_ms_ < CHANNEL_STABILIZATION_MS) return;"
    )
    refresh = radio_cpp.index("this->process_peer_refresh_(now_ms);")
    assert stable < refresh
    function_start = radio_cpp.index(
        "void EspIdfEspNowEncryptedRadio::process_peer_refresh_"
    )
    function_end = radio_cpp.index(
        "\nvoid EspIdfEspNowEncryptedRadio::rollback_initialization_",
        function_start,
    )
    assert "esp_now_send" not in radio_cpp[function_start:function_end]
