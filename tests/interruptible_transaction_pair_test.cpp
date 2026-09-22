#include <cassert>

#include "../components/espnow_net_protocol/interruptible_transaction_pair.h"

using esphome::espnow_net_protocol::InterruptibleTransactionPair;

int main() {
  InterruptibleTransactionPair pair;
  assert(!pair.begin_interrupt(20));
  assert(!pair.begin_operation(0));
  assert(pair.begin_operation(10));
  assert(!pair.begin_operation(11));
  assert(!pair.begin_interrupt(10));
  assert(pair.begin_interrupt(20));
  assert(!pair.begin_interrupt(21));
  assert(!pair.finish_interrupt(21, true));
  assert(pair.finish_interrupt(20, false));
  assert(!pair.operation_interrupted());
  assert(pair.begin_interrupt(22));
  assert(pair.finish_interrupt(22, true));
  assert(pair.operation_interrupted());
  assert(!pair.finish_operation(11));
  assert(pair.finish_operation(10));
  assert(!pair.operation_interrupted());
  assert(pair.begin_operation(30));
  assert(pair.begin_interrupt(31));
  assert(pair.finish_operation(30));
  assert(!pair.begin_operation(32));  // The interrupt still owns its identity.
  assert(pair.finish_interrupt(31, true));
  assert(!pair.operation_interrupted());
  assert(pair.begin_operation(32));
}
