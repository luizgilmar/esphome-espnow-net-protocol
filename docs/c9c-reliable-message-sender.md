# C.9C — Reliable message sender

`ReliableMessageSender` combines bounded message storage, canonical framing and
the existing ACK/retry state machine. It emits one radio frame at a time and
waits for the corresponding asynchronous send completion before advancing.

Retries restart at fragment zero and retain the same delivery identity so the
receiver can acknowledge duplicates without executing the command twice. The
sender accepts both `ACCEPTED` and `DUPLICATE` as successful delivery ACKs.

This increment is transport-neutral and does not register ESP-NOW in a device
runner. The TX adapter will consume this sender in the next gate.
