# C9D runtime health diagnostics — package manifest

## Component

- `components/espnow_net_protocol/espidf_espnow_encrypted_radio.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`

## Tests

- `tests/test_c9d_runtime_health_diagnostics_contract.py`

The instrumentation records main-task stack headroom, heap integrity and the
two callback-to-loop queue depths at command start and immediately before a
terminal timeout is published. It does not change retry, timeout or delivery
semantics.
