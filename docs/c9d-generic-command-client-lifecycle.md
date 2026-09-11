# C9D — Generic outbound command lifecycle

`espnow_net_protocol` owns one bounded outbound command client. The public
`start_command()` API and the YAML `send_command` action share it.

The lifecycle separates radio delivery from functional execution:

1. the encrypted command is delivered reliably;
2. an accepted delivery moves the client to `WAITING_FOR_RESULT`;
3. each correlated `IN_PROGRESS` result is reported to the observer without
   releasing the command;
4. the first correlated terminal result releases the client;
5. delivery rejection, delivery timeout, invalid result and functional timeout
   are emitted as terminal `FAILED` results through the same observer.

Failures before the delivery ACK report `execution.started: false`, allowing a
consumer to select another transport safely. Failures after the ACK report
`execution.started: true`, because the remote command may already have changed
state and must not be repeated blindly. `cancel_command(transaction_id)`
releases the bounded client and quarantines a late result from that transaction.

The protocol retains no dynamic collection. A direct observer callback provides
result delivery without allocating a result queue. The most recent completed
transaction is retained only as peer/transaction identifiers so a late
duplicate terminal result cannot leak into the raw application mailbox.

Device code remains responsible for translating `NetCommand` and `NetResult`
to its domain model and for presenting UX. It does not own ACK retries,
correlation or command lifecycle. Cross-transport routing and MQTT fallback are
outside this radio protocol component.
