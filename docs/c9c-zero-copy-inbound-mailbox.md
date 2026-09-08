# C9C — Zero-copy inbound mailbox

## Objective

Reduce static RAM used by the generic endpoint without changing its public
message API or delivery guarantees.

## Design

The protocol runtime now retains a completed application message in the frame
reassembler until the application consumes it. Previously the same bounded
640-byte payload was copied into a second persistent mailbox before delivery.

The consumer-facing `take_application_message()` contract is unchanged: it
still returns an owned `EspNowInboundApplicationMessage`. CRC validation occurs
as soon as reassembly completes and before the message can be acknowledged or
observed by the application. ACK frames, duplicates, rejected messages and
corrupt frames release the reassembly buffer immediately.

While an application message awaits consumption, another received frame is
rejected. This preserves the existing single-slot bounded mailbox behavior and
prevents the retained payload from being overwritten.

## Budget result

On the host ABI used by the contract tests, `sizeof(EspNowProtocolRuntime)`
decreases from 1704 to 1016 bytes, a structural reduction of 688 bytes. The
ESP32 linker report remains the authoritative hardware measurement.

## Validation

- all native C++ protocol tests pass;
- application and ACK delivery behavior remains unchanged;
- a deliberately corrupted payload is rejected before application delivery;
- the runtime recovers and accepts the following valid message;
- the source contract prevents reintroduction of the duplicate persistent
  inbound mailbox.
