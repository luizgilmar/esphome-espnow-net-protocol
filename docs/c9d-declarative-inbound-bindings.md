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
        completion:
          light_id: SpotChuveiro
          expected: toggled
          timeout: 2s
```

The handler is limited to sixteen bindings and rejects duplicate
`resource/command` routes during validation. Without `completion`, success means
only that the automation was dispatched. With light completion configured, the
handler captures the expected state before triggering the automation, reports
`IN_PROGRESS`, observes `LightState`, and reports terminal success only after the
expected state is visible. Timeout is terminal failure. Expected and observed
states use the compact `binary-state/v1` snapshot.

The handler is compiled only when `inbound` is present. The light probe is
compiled only when at least one completion uses it. Devices using an
application-owned handler do not pay either static cost.
