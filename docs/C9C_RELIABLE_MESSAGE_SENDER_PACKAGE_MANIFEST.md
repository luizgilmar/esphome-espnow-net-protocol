# C.9C Reliable Message Sender — Package Manifest

## Added

- `components/espnow_net_protocol/reliable_sender.h`
- `tests/reliable_sender_test.cpp`
- `docs/c9c-reliable-message-sender.md`

## Guarantees

- fixed-capacity message storage;
- no dynamic containers;
- one radio frame in flight at a time;
- whole-message retry preserves delivery identity;
- correlated `ACCEPTED` and `DUPLICATE` ACKs complete delivery;
- no TX transport registration or runtime behavior change.

## Validation

Seven standalone protocol C++ tests pass. The TX Python suite remains at 821
tests because this increment changes only the independent protocol repository.
