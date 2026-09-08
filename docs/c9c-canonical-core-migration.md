# C.9C — Canonical NetProtocol Core Migration

## Decision

The independent component owns the canonical frame and reliability
implementations. `tx_ultimate` no longer owns copies of fragment encoding,
reassembly, CRC, ACK, retry or duplicate suppression.

The old TX include paths remain as thin compatibility wrappers during the
incremental migration. Existing consumers retain their source compatibility,
while new code includes `espnow_net_protocol/frame.h` and
`espnow_net_protocol/reliability.h` directly.

## Neutral command model

The network component will use neutral protocol DTOs and adapters at
integration boundaries. It will not include `CommandRequest`, `CommandResult`
or other TX-specific domain headers. The currently validated command/result
codecs remain in `tx_ultimate` until their neutral DTO replacements and mapping
tests are introduced.

## Dependency correction

The migration removed an accidental transitive include of
`communication_transaction.h`. The two TX command/result codec adapters now
declare that dependency explicitly. This makes their temporary ownership
visible and prevents the generic frame layer from depending on the TX model.

## Runtime boundary

This source migration has no runtime effect. Generated frame bytes, retry
defaults, queue behavior and the Quartogian MQTT policy remain unchanged.
