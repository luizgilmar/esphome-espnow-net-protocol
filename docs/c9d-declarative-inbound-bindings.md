# C9D — Declarative inbound bindings

The reusable protocol component can own an optional bounded inbound command
handler. A binding matches one exact `device_id`, `resource` and `command`, then
starts an ordinary ESPHome automation. It does not specialize the protocol for
lights, covers, switches, scripts or scenes, and it does not require lambdas.

```yaml
espnow_net_protocol:
  # radio and peers omitted
  inbound:
    device_id: quartogian
    bindings:
      - resource: light/spot_chuveiro
        command: toggle
        then:
          - light.toggle: SpotChuveiro
```

The handler is limited to sixteen bindings and rejects duplicate
`resource/command` routes during validation. Its successful result means the
matching ESPHome automation was dispatched. It does not claim that a delayed
automation completed or that an entity reached a verified final state. A future
verification contract may provide asynchronous progress and state snapshots.

The handler and its bounded storage are compiled only when `inbound` is present.
Devices using an application-owned handler do not pay its static RAM cost.
