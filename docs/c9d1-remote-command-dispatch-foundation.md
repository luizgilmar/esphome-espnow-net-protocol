# C.9D.1 — Remote Command Dispatch Foundation

The protocol now defines a hardware-neutral asynchronous command-handler port
and a fixed-capacity inbound dispatcher. It decodes a received `COMMAND`,
preserves peer and transaction correlation, advances the handler cooperatively
and encodes both `IN_PROGRESS` and terminal functional `RESULT` messages.

One command may be active and one encoded result may be pending. Busy, invalid,
rejected, missing-handler and timeout outcomes are explicit bounded results.
Timeout arithmetic is wrap-safe and cancellation is forwarded to the handler.

This foundation does not yet consume the endpoint application mailbox or send
result frames. The following binding increment will arbitrate the endpoint's
single reliable sender between outbound commands and remote results.
