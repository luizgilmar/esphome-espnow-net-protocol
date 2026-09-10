# C.9D — Declarative Command Action

The generic component exposes `espnow_net_protocol.send_command` as a native
ESPHome automation action. Any ESP32 consumer can select a declared peer and
provide the logical device, resource, command, optional payload and timeout
without an application lambda.

The endpoint generates the transaction identity, uses the existing reliable
sender for delivery, then waits independently for the correlated functional
result. Delivery rejection, delivery timeout, functional timeout,
`IN_PROGRESS`, and terminal results are logged and counted.

The implementation retains one bounded declarative outbound command. It does
not add knowledge of TX actions or scenes to the reusable protocol component.
