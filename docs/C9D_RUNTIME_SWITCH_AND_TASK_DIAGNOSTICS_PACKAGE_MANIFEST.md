# C9D runtime switch and task diagnostics — package manifest

## Generic protocol

- `components/espnow_net_protocol/espidf_espnow_encrypted_radio.h`
- `components/espnow_net_protocol/espidf_espnow_encrypted_radio.cpp`
- `components/espnow_net_protocol/espnow_net_protocol.h`
- `components/espnow_net_protocol/espnow_net_protocol.cpp`
- `components/espnow_net_protocol/espnow_net_protocol_switch.h`
- `components/espnow_net_protocol/espnow_net_protocol_switch.cpp`
- `components/espnow_net_protocol/switch/__init__.py`

## Tests

- `tests/test_c9d_runtime_health_diagnostics_contract.py`
- `tests/test_c9d_runtime_switch_contract.py`

The native switch gates protocol activity without deinitializing the shared
Wi-Fi radio. Diagnostics identify the cooperative loop and ESP-NOW callback
task stack margins independently.
