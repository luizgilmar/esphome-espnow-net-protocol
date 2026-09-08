# C.9C — Neutral Codec Migration

Command and functional-result wire codecs now belong to
`espnow_net_protocol` and operate only on `NetCommand` and `NetResult`.
They have no dependency on TX Ultimate domain or hardware types.

The former TX codec headers are compatibility adapters. They convert through
`EspNowNetModelAdapter` and delegate encoding or decoding to the canonical
codec. Existing TX callers retain their API and identical wire bytes.

This migration has no runtime effect. MQTT routing and the compiled Quartogian
configuration remain unchanged.
