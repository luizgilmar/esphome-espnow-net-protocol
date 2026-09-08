# C.9C — Generic radio and peer registry migration

## Objective

Move encrypted ESP-IDF ESP-NOW radio ownership and peer registration from
`tx_ultimate` into the hardware-neutral `espnow_net_protocol` component.

## Peer references

YAML-facing peer IDs remain readable names such as `hub_quarto` or
`rele_persiana`. During setup each ID occupies one fixed slot in `PeerRegistry`.
Runtime send paths use the resulting `PeerIndex` (`uint8_t`) and therefore do
not scan strings for every frame. MAC addresses and LMKs remain private to the
generic radio.

The legacy string send overload remains temporarily available only as a
compatibility boundary. New executors and transports must resolve the peer once
and retain its compact index.

## Boundaries

- fixed capacity: sixteen encrypted unicast peers;
- no dynamic containers;
- duplicate logical IDs and duplicate MAC addresses are rejected;
- radio implementation is compiled only with
  `USE_ESPNOW_NET_PROTOCOL_RADIO`;
- existing `tx_ultimate` paths are compatibility wrappers;
- the TX compatibility translation unit incorporates the canonical radio
  implementation, avoiding an invalid external-component `AUTO_LOAD`;
- MQTT remains the active Quartogian command transport in this increment.

## Validation

This is an ownership-preserving migration. It does not change the configured
channel, ESP-NOW encryption, retry timing, callbacks, or radio queues. Hardware
upload is not required for closure, but the cumulative Quartogian bench must
continue compiling before ESP-NOW becomes an active fallback transport.
