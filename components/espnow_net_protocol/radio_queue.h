#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "frame.h"

namespace esphome {
namespace espnow_net_protocol {

template<typename T, size_t Capacity> class EspNowSpscQueue {
 public:
  static_assert(Capacity > 0, "ESP-NOW queue capacity must be positive");

  bool push(const T &value) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= Capacity) return false;
    entries_[head % Capacity] = value;
    head_.store(head + 1U, std::memory_order_release);
    return true;
  }

  bool pop(T &value) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    value = entries_[tail % Capacity];
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
  }

  size_t size() const {
    const uint32_t head = head_.load(std::memory_order_acquire);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    return static_cast<size_t>(head - tail);
  }

  constexpr size_t capacity() const { return Capacity; }

 private:
  T entries_[Capacity]{};
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};

struct EspNowReceivedFrame {
  uint8_t peer_index{0};
  EspNowRadioFrame frame{};
};

struct EspNowSendCompletion {
  uint8_t peer_index{0};
  bool succeeded{false};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
