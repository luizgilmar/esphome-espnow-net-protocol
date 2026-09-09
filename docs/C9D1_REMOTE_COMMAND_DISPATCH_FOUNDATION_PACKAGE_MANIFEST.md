# C.9D.1 Remote Command Dispatch Foundation — Package Manifest

## Added

- `components/espnow_net_protocol/command_dispatcher.h`
- `tests/command_dispatcher_test.cpp`
- `tests/test_command_dispatcher_contract.py`
- `docs/c9d1-remote-command-dispatch-foundation.md`
- `docs/C9D1_REMOTE_COMMAND_DISPATCH_FOUNDATION_PACKAGE_MANIFEST.md`

## Boundary

This increment defines the portable asynchronous handler/dispatcher contract.
It does not bind the dispatcher to the radio endpoint, device actions or YAML.
