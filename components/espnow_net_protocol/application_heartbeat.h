#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::espnow_net_protocol {

// Independent application diagnostic. It never enters command dispatch.
struct HeartbeatPacket {
  static constexpr size_t SIZE = 25;
  uint8_t kind{1}; // 1 PING, 2 PONG
  uint64_t requester_boot{0};
  uint32_t sequence{0};
  uint64_t responder_boot{0};

  static bool has_magic(const uint8_t *data, size_t size) {
    return data != nullptr && size >= 4 && std::memcmp(data, "HB01", 4) == 0;
  }
  void encode(uint8_t (&data)[SIZE]) const {
    std::memcpy(data, "HB01", 4);
    data[4] = kind;
    for (unsigned i = 0; i < 8; ++i) data[5+i] = uint8_t(requester_boot >> (8*i));
    for (unsigned i = 0; i < 4; ++i) data[13+i] = uint8_t(sequence >> (8*i));
    for (unsigned i = 0; i < 8; ++i) data[17+i] = uint8_t(responder_boot >> (8*i));
  }
  static bool decode(const uint8_t *data, size_t size, HeartbeatPacket &out) {
    if (!has_magic(data, size) || size != SIZE || (data[4] != 1 && data[4] != 2)) return false;
    HeartbeatPacket value{};
    value.kind = data[4];
    for (unsigned i = 0; i < 8; ++i) value.requester_boot |= uint64_t(data[5+i]) << (8*i);
    for (unsigned i = 0; i < 4; ++i) value.sequence |= uint32_t(data[13+i]) << (8*i);
    for (unsigned i = 0; i < 8; ++i) value.responder_boot |= uint64_t(data[17+i]) << (8*i);
    if (!value.requester_boot || !value.sequence || (value.kind == 2 && !value.responder_boot)) return false;
    out = value;
    return true;
  }
};

// Correlation and expiry use monotonic uptime, including millis() wraparound.
class HeartbeatExchange {
 public:
  void start(const HeartbeatPacket &request, uint32_t now, uint32_t timeout) {
    request_ = request;
    started_ = now;
    timeout_ = timeout;
    pending_ = true;
  }
  bool expired(uint32_t now) const { return pending_ && uint32_t(now - started_) >= timeout_; }
  bool accept(const HeartbeatPacket &reply, uint32_t now, uint32_t &rtt) {
    if (!pending_ || expired(now) || reply.kind != 2 || !reply.responder_boot ||
        reply.requester_boot != request_.requester_boot || reply.sequence != request_.sequence) return false;
    rtt = uint32_t(now - started_);
    pending_ = false;
    return true;
  }
  bool pending() const { return pending_; }
  void reset() { pending_ = false; }
 private:
  HeartbeatPacket request_{};
  uint32_t started_{0}, timeout_{0};
  bool pending_{false};
};

} // namespace esphome::espnow_net_protocol
