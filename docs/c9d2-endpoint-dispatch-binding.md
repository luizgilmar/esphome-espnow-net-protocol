# C.9D.2 — Endpoint Dispatch Binding

The endpoint now consumes the protocol runtime's application mailbox and
routes `COMMAND` messages to the bounded dispatcher while retaining one
`RESULT` message for the originating device transport adapter.

The single reliable sender is explicitly owned either by an external command
originator or by a dispatcher result. Delivery ACK processing is shared, but
only dispatcher-owned terminal delivery state is reset internally. This keeps
delivery acknowledgement distinct from functional completion.

Application ACK frames retain radio priority. Dispatcher results wait until
the sender is idle, then use the same bounded retry and correlation machinery.
Successful and failed result deliveries are counted for diagnostics.

The runtime-owned reassembly buffer remains retained while the dispatcher or
the single result mailbox is occupied. This supplies bounded backpressure
before consumption and prevents acknowledging an application payload that
would then be dropped locally.

No hardware-specific execution logic is introduced. Consumers register a
`NetCommandHandler`; absence of a handler produces a correlated functional
failure instead of silently accepting a command.
