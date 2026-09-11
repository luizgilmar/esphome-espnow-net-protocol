# C9D generic command client lifecycle — package manifest

## Component

- `components/espnow_net_protocol/espnow_net_protocol.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`

## Tests

- `tests/test_declarative_send_command_contract.py`
- `tests/test_command_client_lifecycle_contract.py`

## Documentation

- `README.md`
- `docs/c9d-generic-command-client-lifecycle.md`

This package changes only `esphome-espnow-net-protocol`. The TX adapter
migration follows after this generic API compiles and passes hardware
validation.

The client also exposes correlated cancellation and distinguishes pre-delivery
failure from post-acceptance uncertainty through `NetResult.execution.started`.
