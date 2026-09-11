# ESPHome ESP-NOW NetProtocol

Reusable, hardware-neutral ESP-NOW protocol component for ESPHome ESP32 nodes.
It provides bounded frames, neutral command/result DTOs, ACK/retry/duplicate
suppression, cooperative processing, an encrypted ESP-IDF radio and a
fixed-capacity peer registry.

## Local development

Place this repository beside the consuming device repository and import it:

```yaml
external_components:
  - source:
      type: local
      path: ../../esphome-espnow-net-protocol/components
    components: [espnow_net_protocol]

espnow_net_protocol:
  id: espnow_network
  channel: 1
  pmk: !secret espnow_pmk
  peers:
    - id: relay_room
      address: !secret relay_room_mac
      lmk: !secret relay_room_lmk
```

Device components reference this instance by its ESPHome ID. MAC addresses and
keys remain private to the protocol component.

Inbound `COMMAND` messages are dispatched through the neutral asynchronous
`NetCommandHandler` port. Functional `RESULT` messages are returned through the
same reliable sender, explicitly arbitrated against locally originated
commands.

Any ESP32 consumer can originate a reliable command declaratively:

```yaml
- espnow_net_protocol.send_command:
    id: espnow_network
    peer: tx_quartogian
    device: tx_quartogian
    resource: night_scene
    command: toggle_scene
    timeout: 5s
```

Outbound commands use one component-owned lifecycle. Delivery acknowledgement,
correlation, any number of `IN_PROGRESS` notifications, the single terminal
result and the end-to-end timeout are handled by `espnow_net_protocol`.
Device integrations register a `NetCommandResultObserver`; they do not need to
reimplement the ESP-NOW transaction state machine. The declarative action above
uses this same command client.

At startup, the radio waits until the configured Wi-Fi channel remains stable
for five seconds before initializing ESP-NOW. Failed initialization attempts
are rate-limited and report the underlying ESP-IDF error.

## Constraints

- ESP-IDF backend;
- fixed channel shared with Wi-Fi;
- encrypted unicast peers;
- up to sixteen statically allocated peers;
- fixed-capacity storage and no dynamic containers in the protocol runtime;
- bounded reliable sender with correlated ACK and whole-message retry;
- neutral asynchronous inbound command dispatcher with correlated functional
  results;
- MIT licensed.
