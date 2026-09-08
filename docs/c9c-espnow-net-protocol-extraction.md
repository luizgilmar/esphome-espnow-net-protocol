# C.9C — Espnow NetProtocol Extraction

## Decision

Reliable local communication is a network capability, not a TX Ultimate
hardware capability. The canonical implementation will live in
`components/espnow_net_protocol` and can be linked into any compatible ESPHome
ESP32 device. The public YAML name will be `espnow_net_protocol`; the principal
C++ type will follow ESPHome naming as `EspNowNetProtocolComponent`.

## Foundation boundary

This increment introduces only generic fixed-capacity values, peer identity
and delivery identity. It is not yet exposed as a YAML component and has no
runtime effect. A configuration block will only become available after radio,
protocol runtime and validation rules form a usable end-to-end component.

## Migration strategy

The existing C.9A–C.9C implementation remains temporarily under
`tx_ultimate`. Subsequent increments move each canonical implementation into
the new component and leave compatibility wrappers at the old include paths.
Wrappers will be removed only after all internal consumers and documented
examples use the independent component.

The migration must preserve the already validated channel ownership,
encryption, bounded memory, ACK/retry, deduplication and command/result
semantics. It must not change the current Quartogian MQTT route while the new
component is incomplete.

## Intended topology

Every participating ESPHome device compiles the same protocol component. Each
node may send commands, receive commands, return functional results and relay
state without depending on TX touch, LED, relay or audio classes. Peer keys and
allowed commands remain declarative and device-specific.
