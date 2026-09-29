#include "esphome/core/defines.h"

#include "espidf_espnow_encrypted_radio.h"

#ifdef USE_ESPNOW_NET_PROTOCOL_RADIO

#include <cstring>

#include <esp_err.h>
#include <esp_wifi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esphome/core/log.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/wifi/radio_diagnostics.h"

namespace esphome {
namespace espnow_net_protocol {

static const char *const TAG = "espnow_net_protocol.radio";

static void record_diagnostic(wifi::RadioDiagnosticEventKind kind, int32_t result = 0) {
#ifdef USE_WIFI_RADIO_DIAGNOSTICS
  if (wifi::global_wifi_component != nullptr)
    wifi::global_wifi_component->record_radio_diagnostic_event(kind, result);
#else
  (void) kind;
  (void) result;
#endif
}

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

bool EspIdfEspNowEncryptedRadio::request_peer_refresh(
    PeerIndex peer_index) {
  if (peer_index >= peers_.size() || peer_index >= 16) return false;
  peer_refresh_pending_mask_ |= static_cast<uint16_t>(1U << peer_index);
  return true;
}

bool EspIdfEspNowEncryptedRadio::request_radio_recovery(uint32_t now_ms) {
  if (!configured_ || peers_.size() == 0) return false;
  this->schedule_radio_recovery_(now_ms, "delivery_timeout");
  return true;
}

void EspIdfEspNowEncryptedRadio::loop(uint32_t now_ms) {
  if (!configured_ || peers_.size() == 0 ||
      now_ms - last_channel_poll_ms_ < CHANNEL_POLL_INTERVAL_MS)
    return;
  last_channel_poll_ms_ = now_ms;

  this->observe_wifi_association_(now_ms);
  this->process_wifi_arbitration_(now_ms);

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
    if (initialized_)
      this->schedule_radio_recovery_(now_ms, "channel_changed");
    if (!channel_mismatch_reported_) {
      channel_mismatch_reported_ = true;
      ESP_LOGW(TAG,
               "Configured Wi-Fi channel lost expected=%u current=%u; "
               "ESP-NOW recovery paused",
               static_cast<unsigned>(expected_channel_),
               static_cast<unsigned>(current_channel_));
    }
    return;
  }
  channel_mismatch_reported_ = false;
  if (!channel_stable_) {
    channel_stable_ = true;
    channel_stable_since_ms_ = now_ms;
    ESP_LOGD(TAG, "Wi-Fi channel matched; waiting %u ms before ESP-NOW init",
             static_cast<unsigned>(CHANNEL_STABILIZATION_MS));
    return;
  }
  if (now_ms - channel_stable_since_ms_ < CHANNEL_STABILIZATION_MS) return;
  if (this->process_radio_recovery_(now_ms)) return;
  if (initialized_) {
    this->process_peer_refresh_(now_ms);
    return;
  }
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
  record_diagnostic(wifi::RadioDiagnosticEventKind::ESPNOW_INIT_RESULT, result);
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
    if (!this->add_configured_peer_(static_cast<PeerIndex>(index))) {
      esp_now_unregister_send_cb();
      esp_now_unregister_recv_cb();
      rollback_initialization_();
      return false;
    }
  }

  initialized_ = true;
  last_initialization_error_ = ESP_OK;
  if (radio_recovery_in_progress_) {
    radio_recovery_in_progress_ = false;
    radio_recovery_success_count_++;
  }
  ESP_LOGI(TAG,
           "Encrypted ESP-NOW ready channel=%u peers=%u recoveries=%u failures=%u",
           static_cast<unsigned>(current_channel_),
           static_cast<unsigned>(peers_.size()),
           static_cast<unsigned>(radio_recovery_success_count_),
           static_cast<unsigned>(radio_recovery_failure_count_));
  return true;
}

bool EspIdfEspNowEncryptedRadio::add_configured_peer_(
    PeerIndex peer_index) {
  const PeerIdentity *configured_peer = peers_.peer(peer_index);
  if (configured_peer == nullptr) return false;
  esp_now_peer_info_t peer{};
  std::memcpy(peer.peer_addr, configured_peer->address,
              EspNowEncryptedPeer::MAC_SIZE);
  std::memcpy(peer.lmk, configured_peer->lmk, EspNowEncryptedPeer::KEY_SIZE);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  const esp_err_t result = esp_now_add_peer(&peer);
  last_initialization_error_ = result;
  return result == ESP_OK;
}

bool EspIdfEspNowEncryptedRadio::refresh_peer_(PeerIndex peer_index,
                                                uint32_t now_ms) {
  const PeerIdentity *configured_peer = peers_.peer(peer_index);
  if (!initialized_ || configured_peer == nullptr) return false;
  peer_refresh_last_ms_[peer_index] = now_ms;
  peer_refresh_attempted_mask_ |= static_cast<uint16_t>(1U << peer_index);
  const esp_err_t removed = esp_now_del_peer(configured_peer->address);
  if (removed != ESP_OK && removed != ESP_ERR_ESPNOW_NOT_FOUND) {
    last_initialization_error_ = removed;
    return false;
  }
  return this->add_configured_peer_(peer_index);
}

void EspIdfEspNowEncryptedRadio::process_peer_refresh_(uint32_t now_ms) {
  for (PeerIndex peer_index = 0; peer_index < peers_.size(); ++peer_index) {
    const uint16_t bit = static_cast<uint16_t>(1U << peer_index);
    if ((peer_refresh_pending_mask_ & bit) == 0) continue;
    if ((peer_refresh_attempted_mask_ & bit) != 0 &&
        now_ms - peer_refresh_last_ms_[peer_index] <
            PEER_REFRESH_COOLDOWN_MS)
      continue;
    peer_refresh_pending_mask_ &= static_cast<uint16_t>(~bit);
    const bool succeeded = this->refresh_peer_(peer_index, now_ms);
    if (succeeded) {
      peer_refresh_success_count_++;
      ESP_LOGI(TAG, "Encrypted peer refreshed peer=%u successes=%u failures=%u",
               static_cast<unsigned>(peer_index),
               static_cast<unsigned>(peer_refresh_success_count_),
               static_cast<unsigned>(peer_refresh_failure_count_));
    } else {
      peer_refresh_failure_count_++;
      peer_refresh_pending_mask_ |= bit;
      ESP_LOGW(TAG,
               "Encrypted peer refresh deferred peer=%u error=%s (0x%08x)",
               static_cast<unsigned>(peer_index),
               esp_err_to_name(static_cast<esp_err_t>(last_initialization_error_)),
               static_cast<unsigned>(last_initialization_error_));
    }
    return;
  }
}

void EspIdfEspNowEncryptedRadio::observe_wifi_association_(uint32_t now_ms) {
  wifi_ap_record_t access_point{};
  const bool associated = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
  if (!wifi_association_known_) {
    wifi_association_known_ = true;
    wifi_associated_ = associated;
    if (!associated) this->schedule_wifi_arbitration_grace_(now_ms);
    return;
  }
  if (associated == wifi_associated_) return;
  wifi_associated_ = associated;
  if (associated) {
    this->release_wifi_reconnect_suppression_();
    wifi_arbitration_state_ = WiFiArbitrationState::MONITORING;
    wifi_arbitration_due_ms_ = 0;
  } else {
    this->schedule_wifi_arbitration_grace_(now_ms);
  }
  this->schedule_radio_recovery_(
      now_ms, associated ? "wifi_reconnected" : "wifi_disconnected");
}

void EspIdfEspNowEncryptedRadio::schedule_wifi_arbitration_grace_(
    uint32_t now_ms) {
  this->release_wifi_reconnect_suppression_();
  wifi_arbitration_state_ = WiFiArbitrationState::GRACE;
  wifi_arbitration_due_ms_ =
      now_ms + WIFI_RECONNECT_GRACE_BASE_MS +
      this->wifi_arbitration_jitter_ms_(
          WIFI_RECONNECT_GRACE_JITTER_MS, 0x47524143UL);
  ESP_LOGI(TAG, "WiFi reconnect grace scheduled delay=%u channel=%u",
           static_cast<unsigned>(wifi_arbitration_due_ms_ - now_ms),
           static_cast<unsigned>(expected_channel_));
}

void EspIdfEspNowEncryptedRadio::process_wifi_arbitration_(uint32_t now_ms) {
  if (!wifi_association_known_ || wifi_associated_ ||
      wifi::global_wifi_component == nullptr)
    return;

  if (wifi_arbitration_state_ == WiFiArbitrationState::REQUESTING &&
      wifi::global_wifi_component->is_reconnect_suppression_active()) {
    wifi_arbitration_state_ = WiFiArbitrationState::SUPPRESSED;
    wifi_arbitration_due_ms_ =
        now_ms + WIFI_RECONNECT_HOLD_BASE_MS +
        this->wifi_arbitration_jitter_ms_(
            WIFI_RECONNECT_HOLD_JITTER_MS, 0x484F4C44UL);
    // A channel observed during a scan is not proof of a stable radio.
    channel_stable_ = false;
    ESP_LOGI(TAG, "WiFi reconnect suppression confirmed channel=%u hold=%u",
             static_cast<unsigned>(expected_channel_),
             static_cast<unsigned>(wifi_arbitration_due_ms_ - now_ms));
    return;
  }
  if (static_cast<int32_t>(now_ms - wifi_arbitration_due_ms_) < 0) return;

  switch (wifi_arbitration_state_) {
    case WiFiArbitrationState::MONITORING:
      this->schedule_wifi_arbitration_grace_(now_ms);
      break;
    case WiFiArbitrationState::GRACE:
    case WiFiArbitrationState::RECONNECT_WINDOW:
      if (wifi::global_wifi_component->request_reconnect_suppression(
              expected_channel_)) {
        wifi_reconnect_suppression_held_ = true;
        wifi_arbitration_state_ = WiFiArbitrationState::REQUESTING;
        wifi_arbitration_due_ms_ = now_ms + WIFI_RECONNECT_REQUEST_RETRY_MS;
        ESP_LOGI(TAG,
                 "WiFi reconnect suppression requested channel=%u confirmation_timeout=%u",
                 static_cast<unsigned>(expected_channel_),
                 static_cast<unsigned>(wifi_arbitration_due_ms_ - now_ms));
      } else {
        wifi_arbitration_due_ms_ =
            now_ms + WIFI_RECONNECT_REQUEST_RETRY_MS;
        ESP_LOGW(TAG,
                 "WiFi reconnect suppression deferred channel=%u retry=%u",
                 static_cast<unsigned>(expected_channel_),
                 static_cast<unsigned>(WIFI_RECONNECT_REQUEST_RETRY_MS));
      }
      break;
    case WiFiArbitrationState::REQUESTING:
      ESP_LOGW(TAG, "WiFi reconnect suppression not confirmed; reopening reconnect window");
      this->release_wifi_reconnect_suppression_();
      wifi_arbitration_state_ = WiFiArbitrationState::RECONNECT_WINDOW;
      wifi_arbitration_due_ms_ = now_ms + WIFI_RECONNECT_WINDOW_MS;
      break;
    case WiFiArbitrationState::SUPPRESSED:
      this->release_wifi_reconnect_suppression_();
      wifi_arbitration_state_ = WiFiArbitrationState::RECONNECT_WINDOW;
      wifi_arbitration_due_ms_ = now_ms + WIFI_RECONNECT_WINDOW_MS;
      ESP_LOGI(TAG, "WiFi reconnect window opened duration=%u",
               static_cast<unsigned>(WIFI_RECONNECT_WINDOW_MS));
      break;
  }
}

void EspIdfEspNowEncryptedRadio::release_wifi_reconnect_suppression_() {
  if (!wifi_reconnect_suppression_held_) return;
  if (wifi::global_wifi_component != nullptr)
    wifi::global_wifi_component->release_reconnect_suppression();
  wifi_reconnect_suppression_held_ = false;
}

void EspIdfEspNowEncryptedRadio::schedule_radio_recovery_(
    uint32_t now_ms, const char *reason) {
  if (radio_recovery_pending_ || radio_recovery_in_progress_) return;
  radio_recovery_due_ms_ =
      now_ms + RADIO_RECOVERY_BASE_DELAY_MS + this->recovery_jitter_ms_();
  radio_recovery_pending_ = true;
  ESP_LOGW(TAG,
           "ESP-NOW radio recovery scheduled reason=%s delay=%u associated=%s",
           reason == nullptr ? "unknown" : reason,
           static_cast<unsigned>(radio_recovery_due_ms_ - now_ms),
           wifi_associated_ ? "YES" : "NO");
}

bool EspIdfEspNowEncryptedRadio::process_radio_recovery_(uint32_t now_ms) {
  if (!radio_recovery_pending_ || !recovery_permitted_ ||
      static_cast<int32_t>(now_ms - radio_recovery_due_ms_) < 0)
    return false;
  if (radio_recovery_last_ms_ != 0 &&
      now_ms - radio_recovery_last_ms_ < RADIO_RECOVERY_COOLDOWN_MS)
    return false;

  radio_recovery_pending_ = false;
  radio_recovery_in_progress_ = true;
  radio_recovery_last_ms_ = now_ms;
  peer_refresh_pending_mask_ = 0;
  peer_refresh_attempted_mask_ = 0;
  this->shutdown_radio_();
  channel_stable_ = false;
  channel_stable_since_ms_ = now_ms;
  last_initialization_attempt_ms_ = 0;
  ESP_LOGW(TAG, "ESP-NOW radio epoch restarted; awaiting stable channel");
  return true;
}

void EspIdfEspNowEncryptedRadio::shutdown_radio_() {
  esp_err_t result = ESP_OK;
  if (initialized_ || instance_ == this) {
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
    record_diagnostic(wifi::RadioDiagnosticEventKind::ESPNOW_DEINIT_CALL);
    result = esp_now_deinit();
    record_diagnostic(wifi::RadioDiagnosticEventKind::ESPNOW_DEINIT_RESULT, result);
  }
  if (result != ESP_OK) {
    last_initialization_error_ = result;
    radio_recovery_failure_count_++;
  }
  if (instance_ == this) instance_ = nullptr;
  initialized_ = false;
  EspNowReceivedFrame received{};
  while (received_frames_.pop(received)) {}
  EspNowSendCompletion completion{};
  while (send_completions_.pop(completion)) {}
}

uint32_t EspIdfEspNowEncryptedRadio::recovery_jitter_ms_() const {
  uint8_t address[EspNowEncryptedPeer::MAC_SIZE]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, address) != ESP_OK)
    return RADIO_RECOVERY_JITTER_MS / 2U;
  uint32_t hash = 2166136261UL;
  for (uint8_t value : address) {
    hash ^= value;
    hash *= 16777619UL;
  }
  return RADIO_RECOVERY_JITTER_MS == 0
             ? 0
             : hash % (RADIO_RECOVERY_JITTER_MS + 1U);
}

uint32_t EspIdfEspNowEncryptedRadio::wifi_arbitration_jitter_ms_(
    uint32_t span_ms, uint32_t salt) const {
  if (span_ms == 0) return 0;
  uint8_t address[EspNowEncryptedPeer::MAC_SIZE]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, address) != ESP_OK) return span_ms / 2U;
  uint32_t hash = 2166136261UL ^ salt;
  for (uint8_t value : address) {
    hash ^= value;
    hash *= 16777619UL;
  }
  return hash % (span_ms + 1U);
}

void EspIdfEspNowEncryptedRadio::rollback_initialization_() {
  record_diagnostic(wifi::RadioDiagnosticEventKind::ESPNOW_DEINIT_CALL);
  const esp_err_t result = esp_now_deinit();
  record_diagnostic(wifi::RadioDiagnosticEventKind::ESPNOW_DEINIT_RESULT, result);
  if (instance_ == this) instance_ = nullptr;
  initialized_ = false;
}

void EspIdfEspNowEncryptedRadio::receive_callback_(
    const esp_now_recv_info_t *info, const uint8_t *data, int data_len) {
  EspIdfEspNowEncryptedRadio *radio = instance_;
  if (radio != nullptr) {
    radio->receive_callback_stack_free_bytes_.store(
        uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
    radio->receive_callback_core_.store(xPortGetCoreID(),
                                        std::memory_order_relaxed);
  }
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
  radio->send_callback_stack_free_bytes_.store(
      uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
  radio->send_callback_core_.store(xPortGetCoreID(),
                                   std::memory_order_relaxed);
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
