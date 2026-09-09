# C.9D.2 Endpoint Dispatch Binding — Package Manifest

## Updated

- `components/espnow_net_protocol/espnow_net_protocol.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`
- `components/espnow_net_protocol/protocol_runtime.h`
- `tests/test_protocol_endpoint_contract.py`
- `README.md`

## Added

- `docs/c9d2-endpoint-dispatch-binding.md`
- `docs/C9D2_ENDPOINT_DISPATCH_BINDING_PACKAGE_MANIFEST.md`

## Boundary

The generic endpoint now routes inbound commands and results and sends handler
results reliably. Device-specific handler registration remains a consuming
component responsibility.
