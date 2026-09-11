# C9D declarative inbound bindings — package manifest

## Component

- `components/espnow_net_protocol/__init__.py`
- `components/espnow_net_protocol/declarative_command_handler.h`
- `components/espnow_net_protocol/espnow_net_protocol.h`

## Tests

- `tests/test_declarative_inbound_binding_contract.py`

## Documentation

- `docs/c9d-declarative-inbound-bindings.md`

The package adds a bounded, type-neutral and lambda-free inbound executor. It
does not change radio framing, encryption, reliable delivery or existing TX
handler integration.

The binding automation schema is validated with `single=True`, preserving each
binding as one mapping before duplicate-route validation and code generation.
