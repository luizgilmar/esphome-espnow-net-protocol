# Application identity in command payloads

The command payload codec accepts two versions. Version 1 is unchanged and
does not carry application source identity. Version 2 appends a bounded source
device ID and a nonzero application boot ID after the existing command body.
Both versions retain the transaction ID in the radio frame envelope.

The radio frame's `source_boot_id` belongs to reliable radio delivery. It must
not be substituted for the application boot ID used in MQTT commands and in
the cross-transport replay guard. An adapter can emit version 2 only when it
has both application identity fields from its originating transaction. If
either is missing, it must use version 1 and must not claim cross-transport
deduplication.

Decoding version 1 remains compatible with existing peers. Existing firmware
cannot decode version 2, so deployments must update receivers before senders
begin emitting version 2. The TX adapter can emit version 2 only with its
explicit `communication.esp_now.application_identity` option and a v2-capable
ESP-NOW component. The HUB can observe decoded identity with
`espnow_net_protocol.inbound.observe_application_identity: true`, without
changing its existing command handler. Neither option binds the HUB's shared
inbound gate or enables MQTT to ESP-NOW fallback.
No new sender identity is authenticated merely because these fields occur in
the payload: the adapter must bind them to a verified sender and session.

The bounded wire payload remains within the 640-byte reassembled radio
message limit: at most 439 bytes in version 1 and 511 bytes in version 2.
