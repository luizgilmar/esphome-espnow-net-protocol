# C.9C — Runtime Core Migration

The bounded SPSC queue and cooperative protocol runtime now have one canonical
implementation under `espnow_net_protocol`. They depend only on the generic
frame and reliability layers. TX include paths remain compatibility wrappers.

RX capacity, send-completion capacity, duplicate window and mailbox behavior
are unchanged. Existing C++ compatibility tests compile through the old paths.
This migration does not add another runtime instance or change MQTT routing.
