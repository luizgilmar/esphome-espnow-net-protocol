#include "esphome/core/defines.h"

#include "espidf_espnow_encrypted_radio.h"

#ifdef USE_ESPNOW_NET_PROTOCOL_RADIO

#include <cstring>

#include <esp_err.h>
#include <esp_wifi.h>

#include "esphome/core/log.h"

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol.radio";

EspIdfEspNowEncryptedRadio *EspIdfEspNowEncryptedRadio::instance_ = nullptr;

bool EspIdfEspNowEncryptedRadio::configure(uint8_t expected_channel,
                                           const char *pmk_hex) {
  if (configured_ || expected_channel == 0 || expected_channel > 14 ||
      !parse_key_(pmk_hex, pmk_))
    return false;
  expected_channel_ = expected_channel;
  configured_ = true;
  return true;
}

bool EspIdfEspNowEncryptedRadio::add_peer(const char *destination_id,
                                          const char *address,
                                          const char *lmk_hex) {
  if (!configured_ || initialized_ || peers_.size() >= MAX_PEERS ||
      destination_id == nullptr || destination_id[0] == '\0' ||
      find_peer(destination_id) != nullptr)
    return false;

  EspNowEncryptedPeer peer{};
  if (!peer.id.assign(destination_id) ||
      !parse_address_(address, peer.address) ||
      !parse_key_(lmk_hex, peer.lmk) || known_address_(peer.address))
    return false;
  return peers_.add(peer);
}

const EspNowEncryptedPeer *EspIdfEspNowEncryptedRadio::find_peer(
    const char *destination_id) const {
  return this->peer(this->peer_index(destination_id));
}

bool EspIdfEspNowEncryptedRadio::send_frame(const char *destination_id,
                                             const uint8_t *data, size_t size) {
  return this->send_frame(this->peer_index(destination_id), data, size);
}

bool EspIdfEspNowEncryptedRadio::send_frame(PeerIndex peer_index,
                                             const uint8_t *data, size_t size) {
  const EspNowEncryptedPeer *selected = this->peer(peer_index);
  if (!initialized_ || selected == nullptr || data == nullptr || size == 0 ||
      size > EspNowRadioFrame::MAX_SIZE)
    return false;
  if (esp_now_send(selected->address, data, size) != ESP_OK) {
    failed_send_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

PeerIndex EspIdfEspNowEncryptedRadio::peer_index(
    const char *destination_id) const {
  return peers_.index_for_id(destination_id);
}

void EspIdfEspNowEncryptedRadio::loop(uint32_t now_ms) {
  if (!configured_ || initialized_ || peers_.size() == 0 ||
      now_ms - last_channel_poll_ms_ < CHANNEL_POLL_INTERVAL_MS)
    return;
  last_channel_poll_ms_ = now_ms;

  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  uint8_t primary = 0;
  if (esp_wifi_get_channel(&primary, &secondary) != ESP_OK) {
    channel_matches_ = false;
    channel_stable_ = false;
    return;
  }
  current_channel_ = primary;
  channel_matches_ = primary == expected_channel_;
  if (!channel_matches_) {
    channel_stable_ = false;
    ESP_LOGW(TAG, "Waiting for configured Wi-Fi channel expected=%u current=%u",
             static_cast<unsigned>(expected_channel_),
             static_cast<unsigned>(current_channel_));
    return;
  }
  if (!channel_stable_) {
    channel_stable_ = true;
    channel_stable_since_ms_ = now_ms;
    ESP_LOGD(TAG, "Wi-Fi channel matched; waiting %u ms before ESP-NOW init",
             static_cast<unsigned>(CHANNEL_STABILIZATION_MS));
    return;
  }
  if (now_ms - channel_stable_since_ms_ < CHANNEL_STABILIZATION_MS) return;
  if (last_initialization_attempt_ms_ != 0 &&
      now_ms - last_initialization_attempt_ms_ < INITIALIZATION_RETRY_MS)
    return;

  last_initialization_attempt_ms_ = now_ms;
  if (!initialize_()) {
    ESP_LOGW(TAG, "Encrypted ESP-NOW initialization deferred error=%s (0x%08x)",
             esp_err_to_name(static_cast<esp_err_t>(last_initialization_error_)),
             static_cast<unsigned>(last_initialization_error_));
  }
}

bool EspIdfEspNowEncryptedRadio::initialize_() {
  if (instance_ != nullptr && instance_ != this) {
    last_initialization_error_ = ESP_ERR_INVALID_STATE;
    return false;
  }
  esp_err_t result = esp_now_init();
  last_initialization_error_ = result;
  if (result != ESP_OK) return false;
  instance_ = this;

  result = esp_now_set_pmk(pmk_);
  last_initialization_error_ = result;
  if (result != ESP_OK) {
    rollback_initialization_();
    return false;
  }
  result = esp_now_register_recv_cb(receive_callback_);
  last_initialization_error_ = result;
  if (result != ESP_OK) {
    rollback_initialization_();
    return false;
  }
  result = esp_now_register_send_cb(send_callback_);
  last_initialization_error_ = result;
  if (result != ESP_OK) {
    esp_now_unregister_recv_cb();
    rollback_initialization_();
    return false;
  }

  for (size_t index = 0; index < peers_.size(); index++) {
    const PeerIdentity *configured_peer = peers_.peer(index);
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, configured_peer->address,
                EspNowEncryptedPeer::MAC_SIZE);
    std::memcpy(peer.lmk, configured_peer->lmk, EspNowEncryptedPeer::KEY_SIZE);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    result = esp_now_add_peer(&peer);
    last_initialization_error_ = result;
    if (result != ESP_OK) {
      esp_now_unregister_send_cb();
      esp_now_unregister_recv_cb();
      rollback_initialization_();
      return false;
    }
  }

  initialized_ = true;
  last_initialization_error_ = ESP_OK;
  ESP_LOGI(TAG, "Encrypted ESP-NOW ready channel=%u peers=%u",
           static_cast<unsigned>(current_channel_),
           static_cast<unsigned>(peers_.size()));
  return true;
}

void EspIdfEspNowEncryptedRadio::rollback_initialization_() {
  esp_now_deinit();
  if (instance_ == this) instance_ = nullptr;
  initialized_ = false;
}

void EspIdfEspNowEncryptedRadio::receive_callback_(
    const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  EspIdfEspNowEncryptedRadio *radio = instance_;
  static_assert(EspNowRadioFrame::MAX_SIZE == 250,
                "ESP-NOW receive limit must match the legacy frame size");
  if (radio == nullptr || info == nullptr || data == nullptr || data_len <= 0 ||
      data_len > 250) {
    if (radio != nullptr)
      radio->dropped_frame_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const int peer_index = radio->find_peer_index_by_address_(info->src_addr);
  EspNowReceivedFrame received{};
  if (peer_index < 0 ||
      !received.frame.data.assign(data, static_cast<size_t>(data_len))) {
    radio->dropped_frame_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  received.peer_index = static_cast<uint8_t>(peer_index);
  if (!radio->received_frames_.push(received)) {
    radio->dropped_frame_count_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  radio->received_frame_count_.fetch_add(1, std::memory_order_relaxed);
}

void EspIdfEspNowEncryptedRadio::send_callback_(
    const esp_now_send_info_t *info, esp_now_send_status_t status) {
  EspIdfEspNowEncryptedRadio *radio = instance_;
  if (radio == nullptr || info == nullptr) return;
  const int peer_index = radio->find_peer_index_by_address_(info->des_addr);
  if (peer_index >= 0) {
    EspNowSendCompletion completion{};
    completion.peer_index = static_cast<uint8_t>(peer_index);
    completion.succeeded = status == ESP_NOW_SEND_SUCCESS;
    if (!radio->send_completions_.push(completion))
      radio->dropped_completion_count_.fetch_add(1, std::memory_order_relaxed);
  }
  if (status == ESP_NOW_SEND_SUCCESS)
    radio->sent_frame_count_.fetch_add(1, std::memory_order_relaxed);
  else
    radio->failed_send_count_.fetch_add(1, std::memory_order_relaxed);
}

bool EspIdfEspNowEncryptedRadio::known_address_(const uint8_t *address) const {
  return this->find_peer_index_by_address_(address) >= 0;
}

int EspIdfEspNowEncryptedRadio::find_peer_index_by_address_(
    const uint8_t *address) const {
  const PeerIndex index = peers_.index_for_address(address);
  return index == INVALID_PEER_INDEX ? -1 : static_cast<int>(index);
}

bool EspIdfEspNowEncryptedRadio::parse_key_(const char *hex, uint8_t *out) {
  if (hex == nullptr || out == nullptr || std::strlen(hex) != KEY_SIZE * 2U)
    return false;
  for (size_t index = 0; index < KEY_SIZE; index++) {
    const auto nibble = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    const int high = nibble(hex[index * 2U]);
    const int low = nibble(hex[index * 2U + 1U]);
    if (high < 0 || low < 0) return false;
    out[index] = static_cast<uint8_t>((high << 4U) | low);
  }
  return true;
}

bool EspIdfEspNowEncryptedRadio::parse_address_(const char *value,
                                                 uint8_t *out) {
  if (value == nullptr || out == nullptr || std::strlen(value) != 17U)
    return false;
  for (size_t index = 0; index < EspNowEncryptedPeer::MAC_SIZE; index++) {
    if (index != 0 && value[index * 3U - 1U] != ':') return false;
    char pair[3]{value[index * 3U], value[index * 3U + 1U], '\0'};
    uint8_t decoded[KEY_SIZE]{};
    char expanded[KEY_SIZE * 2U + 1U]{};
    expanded[0] = pair[0];
    expanded[1] = pair[1];
    for (size_t fill = 2; fill < KEY_SIZE * 2U; fill++) expanded[fill] = '0';
    if (!parse_key_(expanded, decoded)) return false;
    out[index] = decoded[0];
  }
  return true;
}

}  // namespace espnow_net_protocol
}  // namespace esphome

#endif  // USE_ESPNOW_NET_PROTOCOL_RADIO
