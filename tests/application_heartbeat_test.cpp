#include "application_heartbeat.h"
#include <cassert>
#include <cstdint>
using namespace esphome::espnow_net_protocol;
int main() {
  HeartbeatPacket ping{};
  ping.requester_boot = 0x1122334455667788ULL;
  ping.sequence = 0x89abcdefU;
  uint8_t bytes[HeartbeatPacket::SIZE];
  ping.encode(bytes);
  HeartbeatPacket decoded{};
  assert(HeartbeatPacket::decode(bytes, sizeof(bytes), decoded));
  assert(decoded.requester_boot == ping.requester_boot && decoded.sequence == ping.sequence);
  assert(!HeartbeatPacket::decode(bytes, sizeof(bytes)-1, decoded));
  assert(!HeartbeatPacket::decode(nullptr, sizeof(bytes), decoded));
  bytes[4] = 3;
  assert(!HeartbeatPacket::decode(bytes, sizeof(bytes), decoded));
  ping.encode(bytes);
  bytes[0] = 'X';
  assert(!HeartbeatPacket::decode(bytes, sizeof(bytes), decoded));
  HeartbeatPacket pong = ping;
  pong.kind = 2;
  pong.responder_boot = 55;
  HeartbeatExchange exchange;
  uint32_t rtt = 0;
  exchange.start(ping, 1000, 2000);
  assert(!exchange.accept(ping, 1100, rtt)); // request is not application reply
  auto wrong = pong;
  ++wrong.sequence;
  assert(!exchange.accept(wrong, 1100, rtt));
  wrong = pong;
  ++wrong.requester_boot;
  assert(!exchange.accept(wrong, 1100, rtt));
  assert(exchange.accept(pong, 1100, rtt) && rtt == 100);
  assert(!exchange.accept(pong, 1200, rtt)); // duplicate
  exchange.start(ping, 1000, 2000);
  assert(exchange.expired(3000));
  assert(!exchange.accept(pong, 3000, rtt)); // deadline inclusive
  exchange.reset();
  assert(!exchange.pending());
  exchange.start(ping, UINT32_MAX - 50, 2000);
  assert(exchange.accept(pong, 49, rtt) && rtt == 100); // uptime rollover
  pong.encode(bytes);
  assert(HeartbeatPacket::decode(bytes, sizeof(bytes), decoded));
  assert(decoded.responder_boot == 55);
}
