# C.9C Sixteen-peer Capacity — Package Manifest

## Changes

- `PeerRegistry::CAPACITY` increases from 6 to 16;
- YAML `peers` limit increases from 6 to 16;
- unit coverage fills all sixteen slots and rejects the seventeenth;
- documentation records the new fixed capacity.

## Static-memory impact

On the host ABI `PeerIdentity` occupies 96 bytes. Increasing ten slots adds
960 bytes to `PeerRegistry` (584 to 1544 bytes). The ESP32 clean-build size
report is the authoritative target measurement.

## Validation

- all seven standalone protocol C++ tests pass;
- clean ESPHome compilation is required after applying this package;
- compare DRAM and `.bss` against the previous build.
