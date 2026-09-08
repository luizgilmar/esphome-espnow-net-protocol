#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace espnow_net_protocol {

template<size_t Capacity> class BoundedText {
 public:
  static_assert(Capacity > 0, "BoundedText capacity must be positive");

  bool assign(const char *value) {
    this->clear();
    if (value == nullptr) return false;
    size_t length = 0;
    while (length <= Capacity && value[length] != '\0') length++;
    if (length > Capacity) return false;
    if (length != 0) std::memcpy(data_, value, length);
    data_[length] = '\0';
    size_ = length;
    return true;
  }

  void clear() { data_[0] = '\0'; size_ = 0; }
  const char *c_str() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

 private:
  char data_[Capacity + 1]{};
  size_t size_{0};
};

template<size_t Capacity> class BoundedBytes {
 public:
  static_assert(Capacity > 0, "BoundedBytes capacity must be positive");

  bool assign(const uint8_t *value, size_t length) {
    this->clear();
    if ((value == nullptr && length != 0) || length > Capacity) return false;
    if (length != 0) std::memcpy(data_, value, length);
    size_ = length;
    return true;
  }

  void clear() { size_ = 0; }
  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

 private:
  uint8_t data_[Capacity]{};
  size_t size_{0};
};

}  // namespace espnow_net_protocol
}  // namespace esphome
