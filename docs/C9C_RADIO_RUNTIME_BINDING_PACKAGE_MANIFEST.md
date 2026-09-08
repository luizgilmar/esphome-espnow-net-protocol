# C.9C Radio/runtime Binding — Package Manifest

## Updated

- protocol runtime gains safe deferred boot-ID configuration;
- reliable sender exposes current peer/frame ownership;
- generic component owns and pumps runtime, sender and radio;
- retry policy becomes declarative and bounded.

## Added

- serialized radio transmission ownership;
- bounded application ACK dispatch;
- endpoint contract tests and architecture notes.

## Runtime boundary

The endpoint is compiled and operational but is not yet registered as a TX
`CommunicationTransportAdapter`; MQTT routing therefore remains unchanged.
