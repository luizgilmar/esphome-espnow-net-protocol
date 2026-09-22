# Long operation and interrupt correlation

`InterruptibleTransactionPair` records one long-running operation and one
independent interrupt transaction without command payloads or heap allocation.
It permits a STOP while the original OPEN/CLOSE remains active, preserves both
transaction IDs, and marks the original operation interrupted only when STOP
succeeds. An unsuccessful STOP leaves the original operation pending. A second
OPEN/CLOSE or STOP is rejected while its slot is occupied.

This is the bounded correlation contract, not an enabled cover handler. The
current ESP-NOW endpoint still has one command client and one inbound
dispatcher. Both sides must acquire a separate interrupt lane and retain their
own result routing before the HUB cover bindings and TX intents switch to this
contract. Keep the existing bridge for OPEN/CLOSE/STOP until all three actions
can complete or interrupt on both MQTT and ESP-NOW.

The native regression is `tests/interruptible_transaction_pair_test.cpp`.
