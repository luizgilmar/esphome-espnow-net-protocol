# C.9C — Radio/runtime binding

`EspNowNetProtocolComponent` now owns the radio, protocol runtime and reliable
sender as one cooperative endpoint. Each loop performs at most one completion,
one received frame, one application ACK and one outbound sender frame.

Radio transmissions are serialized. A small owner marker distinguishes sender
fragments from application ACKs because the ESP-IDF send callback reports only
the destination address and status. This prevents an ACK completion from being
mistaken for completion of a command fragment.

Retry policy is declarative through `ack_timeout` and `max_attempts`. Defaults
remain 250 ms and three attempts. The endpoint does not yet register itself in
the TX communication runner.
