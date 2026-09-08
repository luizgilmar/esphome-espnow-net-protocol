# C.9C — Neutral Message Model

`espnow_net_protocol` now owns neutral command, result, execution, state and
error DTOs. They preserve the existing asynchronous semantics without
including any TX Ultimate header.

The TX integration uses `EspNowNetModelAdapter` for explicit field-by-field
conversion. No layout casting or shared object representation is assumed.
Inbound results are restored with `CommunicationTransport::ESP_NOW` and a
successful delivery transport status.

The neutral model and adapter are not instantiated in this increment, so MQTT
routing remains unchanged and there is no firmware memory effect. The next
increment moves the command/result wire codecs to operate on these neutral
DTOs, leaving their TX API as compatibility adapters.
