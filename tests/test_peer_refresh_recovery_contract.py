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


def test_radio_epoch_recovery_covers_wifi_lifecycle_and_delivery_timeout() -> None:
    endpoint = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    radio_h = (C / "espidf_espnow_encrypted_radio.h").read_text(encoding="utf-8")
    radio_cpp = (C / "espidf_espnow_encrypted_radio.cpp").read_text(encoding="utf-8")
    assert "RADIO_RECOVERY_BASE_DELAY_MS = 3000" in radio_h
    assert "RADIO_RECOVERY_JITTER_MS = 7000" in radio_h
    assert "RADIO_RECOVERY_COOLDOWN_MS = 30000" in radio_h
    assert "esp_wifi_sta_get_ap_info" in radio_cpp
    assert '"wifi_reconnected" : "wifi_disconnected"' in radio_cpp
    assert "request_radio_recovery(millis())" in endpoint
    assert "recovery_permitted" in endpoint
    assert "esp_now_unregister_send_cb()" in radio_cpp
    assert "esp_now_unregister_recv_cb()" in radio_cpp
    assert "esp_now_deinit()" in radio_cpp
    assert "radio_recovery_ok=%u radio_recovery_failed=%u" in endpoint


def test_radio_epoch_restart_is_bounded_and_rebuilds_all_peers() -> None:
    radio_cpp = (C / "espidf_espnow_encrypted_radio.cpp").read_text(encoding="utf-8")
    restart = radio_cpp.index(
        "bool EspIdfEspNowEncryptedRadio::process_radio_recovery_"
    )
    initialize = radio_cpp.index("bool EspIdfEspNowEncryptedRadio::initialize_()")
    assert "radio_recovery_pending_" in radio_cpp[restart:]
    assert "RADIO_RECOVERY_COOLDOWN_MS" in radio_cpp[restart:]
    assert "recovery_permitted_" in radio_cpp[restart:]
    assert "this->shutdown_radio_();" in radio_cpp[restart:]
    assert "for (size_t index = 0; index < peers_.size(); index++)" in radio_cpp[initialize:]
