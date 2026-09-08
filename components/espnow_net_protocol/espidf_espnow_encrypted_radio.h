#pragma once

#ifdef USE_ESPNOW_NET_PROTOCOL_RADIO

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <esp_now.h>

#include "peer_registry.h"
#include "radio_queue.h"

namespace esphome {
namespace espnow_net_protocol {

using EspNowEncryptedPeer = PeerIdentity;

class EspIdfEspNowEncryptedRadio {
 public:
  static constexpr size_t KEY_SIZE = 16;
  static constexpr size_t MAX_PEERS = PeerRegistry::CAPACITY;
  static constexpr uint32_t INITIALIZATION_RETRY_MS = 1000;
  static constexpr size_t RX_QUEUE_CAPACITY = 3;
  static constexpr size_t SEND_COMPLETION_QUEUE_CAPACITY = 4;

  bool configure(uint8_t expected_channel, const char *pmk_hex);
  bool add_peer(const char *destination_id, const char *address,
                const char *lmk_hex);
  void loop(uint32_t now_ms);
  bool send_frame(PeerIndex peer_index, const uint8_t *data, size_t size);
  bool send_frame(const char *destination_id, const uint8_t *data, size_t size);
  bool take_received_frame(EspNowReceivedFrame &frame) {
    return received_frames_.pop(frame);
  }
  bool take_send_completion(EspNowSendCompletion &completion) {
    return send_completions_.pop(completion);
  }

  bool configured() const { return configured_; }
  bool initialized() const { return initialized_; }
  bool channel_matches() const { return channel_matches_; }
  uint8_t expected_channel() const { return expected_channel_; }
  uint8_t current_channel() const { return current_channel_; }
  size_t peer_count() const { return peers_.size(); }
  uint32_t received_frame_count() const {
    return received_frame_count_.load(std::memory_order_relaxed);
  }
  uint32_t dropped_frame_count() const {
    return dropped_frame_count_.load(std::memory_order_relaxed);
  }
  uint32_t sent_frame_count() const {
    return sent_frame_count_.load(std::memory_order_relaxed);
  }
  uint32_t failed_send_count() const {
    return failed_send_count_.load(std::memory_order_relaxed);
  }
  uint32_t dropped_completion_count() const {
    return dropped_completion_count_.load(std::memory_order_relaxed);
  }

  const EspNowEncryptedPeer *find_peer(const char *destination_id) const;
  const EspNowEncryptedPeer *peer(PeerIndex peer_index) const {
    return peers_.peer(peer_index);
  }
  PeerIndex peer_index(const char *destination_id) const;

 protected:
  bool initialize_();
  void rollback_initialization_();
  static bool parse_key_(const char *hex, uint8_t *out);
  static bool parse_address_(const char *value, uint8_t *out);
  static void receive_callback_(const esp_now_recv_info_t *info,
                                const uint8_t *data, int data_len);
  static void send_callback_(const esp_now_send_info_t *info,
                             esp_now_send_status_t status);
  bool known_address_(const uint8_t *address) const;
  int find_peer_index_by_address_(const uint8_t *address) const;

  static EspIdfEspNowEncryptedRadio *instance_;
  PeerRegistry peers_{};
  uint8_t pmk_[KEY_SIZE]{};
  uint32_t last_initialization_attempt_ms_{0};
  std::atomic<uint32_t> received_frame_count_{0};
  std::atomic<uint32_t> dropped_frame_count_{0};
  std::atomic<uint32_t> sent_frame_count_{0};
  std::atomic<uint32_t> failed_send_count_{0};
  std::atomic<uint32_t> dropped_completion_count_{0};
  EspNowSpscQueue<EspNowReceivedFrame, RX_QUEUE_CAPACITY> received_frames_{};
  EspNowSpscQueue<EspNowSendCompletion, SEND_COMPLETION_QUEUE_CAPACITY>
      send_completions_{};
  uint8_t expected_channel_{1};
  uint8_t current_channel_{0};
  bool configured_{false};
  bool initialized_{false};
  bool channel_matches_{false};
};

}  // namespace espnow_net_protocol
}  // namespace esphome

#endif  // USE_ESPNOW_NET_PROTOCOL_RADIO
