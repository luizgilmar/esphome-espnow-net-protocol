# C.9D Declarative Command Action — Package Manifest

## Added

- `components/espnow_net_protocol/actions.py`
- `components/espnow_net_protocol/send_command_action.h`
- `tests/test_declarative_send_command_contract.py`
- `docs/c9d-declarative-command-action.md`
- `docs/C9D_DECLARATIVE_COMMAND_ACTION_PACKAGE_MANIFEST.md`

## Updated

- `components/espnow_net_protocol/__init__.py`
- `components/espnow_net_protocol/espnow_net_protocol.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`
- `components/espnow_net_protocol/protocol_runtime.h`

## Boundary

The action is transport-generic and contains no TX-specific capability logic.
