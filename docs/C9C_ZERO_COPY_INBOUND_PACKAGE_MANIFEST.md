# C9C zero-copy inbound package manifest

## Component files

- `components/espnow_net_protocol/frame.h`
- `components/espnow_net_protocol/protocol_runtime.h`

## Validation files

- `tests/communication_espnow_protocol_runtime_test.cpp`
- `tests/test_protocol_endpoint_contract.py`

## Documentation

- `docs/c9c-zero-copy-inbound-mailbox.md`

## Expected effect

No public YAML or C++ API change. The duplicate persistent inbound payload
buffer is removed, reducing `EspNowProtocolRuntime` by 688 bytes on the host
ABI. Confirm the exact ESP32 DRAM reduction with clean builds of both benches.
