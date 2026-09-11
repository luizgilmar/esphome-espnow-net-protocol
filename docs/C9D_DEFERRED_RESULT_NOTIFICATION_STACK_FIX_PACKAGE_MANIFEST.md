# C9D deferred result notification stack fix — package manifest

## Component

- `components/espnow_net_protocol/espnow_net_protocol.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`

## Tests

- `tests/test_c9d_deferred_command_notification_contract.py`

This package prevents a device observer from running inside the deep radio
failure/result processing call chain. One bounded member slot defers the event
until the next cooperative protocol loop and preserves progress-before-terminal
ordering without dynamic allocation.
