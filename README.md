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

## Constraints

- ESP-IDF backend;
- fixed channel shared with Wi-Fi;
- encrypted unicast peers;
- up to sixteen statically allocated peers;
- fixed-capacity storage and no dynamic containers in the protocol runtime;
- bounded reliable sender with correlated ACK and whole-message retry;
- MIT licensed.
