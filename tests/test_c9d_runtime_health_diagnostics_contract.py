from pathlib import Path


C = Path("components/espnow_net_protocol")


def test_timeout_reports_stack_heap_and_callback_queue_health() -> None:
    source = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    header = (C / "espidf_espnow_encrypted_radio.h").read_text(encoding="utf-8")
    assert "uxTaskGetStackHighWaterMark(nullptr)" in source
    assert "heap_caps_check_integrity_all(false)" in source
    assert '"before_timeout_failure"' in source
    assert "received_queue_depth()" in header
    assert "completion_queue_depth()" in header


def test_diagnostics_do_not_change_retry_or_timeout_policy() -> None:
    source = (C / "espnow_net_protocol.cpp").read_text(encoding="utf-8")
    assert "sender_.loop(now_ms);" in source
    assert '"delivery acknowledgement timed out"' in source
