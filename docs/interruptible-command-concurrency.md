# Long operation and interrupt correlation

`InterruptibleTransactionPair` records one long-running operation and one
independent interrupt transaction without command payloads or heap allocation.
It permits a STOP while the original OPEN/CLOSE remains active, preserves both
transaction IDs, and marks the original operation interrupted only when STOP
succeeds. An unsuccessful STOP leaves the original operation pending. A second
OPEN/CLOSE or STOP is rejected while its slot is occupied.

The endpoint provides an opt-in second inbound dispatcher and an opt-in second
outbound command client. Both retain their own transaction ID, peer, timeout
and result correlation. The reliable radio sender remains single and bounded;
only frame transmission is serialized.

`communication_net_protocol` enables the inbound dispatcher when its YAML has
an `interruptible` operation and an `interrupts_active` stop route. TX Ultimate
enables the outbound client with `communication.esp_now.max_in_flight: 2`.
Configurations that omit these declarations keep the original single-lane
storage.

The native regression is `tests/interruptible_transaction_pair_test.cpp`.
