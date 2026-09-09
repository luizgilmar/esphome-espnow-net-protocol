from pathlib import Path


C = Path("components/espnow_net_protocol")


def test_inbound_dispatcher_is_neutral_bounded_and_async() -> None:
    source = (C / "command_dispatcher.h").read_text(encoding="utf-8")
    assert "class NetCommandHandler" in source
    assert "class InboundCommandDispatcher" in source
    assert "NetCommand command_{}" in source
    assert "PendingNetResult pending_{}" in source
    assert "NetCommandHandlerStartStatus" in source
    assert "handler_->loop(now_ms)" in source
    assert "std::vector" not in source
    assert "std::queue" not in source
    assert "std::string" not in source


def test_delivery_and_execution_results_remain_separate() -> None:
    source = (C / "command_dispatcher.h").read_text(encoding="utf-8")
    assert "EspNowInboundApplicationMessage" in source
    assert "EspNowFrameKind::COMMAND" in source
    assert "EspNowResultCodec result_codec_" in source
    assert "NetResultStatus::IN_PROGRESS" in source
    assert "result.transaction_id != transaction_id_" in source
    assert "remote command execution timed out" in source
