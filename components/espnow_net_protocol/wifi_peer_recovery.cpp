#include "espnow_net_protocol.h"
#include "esphome/core/log.h"
#include <esp_wifi.h>
#include <cstring>

namespace esphome::espnow_net_protocol {
#ifdef USE_WIFI_FIXED_CHANNEL
void EspNowNetProtocolComponent::set_wifi_reconnect_recovery(wifi::WiFiComponent *wifi_component) {
  reconnect_wifi_ = wifi_component;
  wifi_component->set_skip_unchanged_sta_config(true);
  wifi_component->set_skip_disconnected_sta_disconnect(true);
  wifi_component->set_adaptive_fixed_retry(true);
  wifi_component->set_fixed_channel_recovery_listener(this, +[](void *context, const char *phase) {
    static_cast<EspNowNetProtocolComponent *>(context)->reconnect_peer_recovery_phase(phase);
  });
}
#endif

void EspNowNetProtocolComponent::reconnect_peer_recovery_phase(const char *phase) {
  if (std::strcmp(phase, "wifi_connected") == 0) {
    if (reconnect_peer_pending_) ESP_LOGI("espnow.peer", "RC2 recovery canceled: Wi-Fi connected");
    reconnect_peer_attempt_ = false;
    reconnect_peer_pending_ = false;
  } else if (std::strcmp(phase, "before_attempt") == 0) {
    if (reconnect_peer_pending_) ESP_LOGW("espnow.peer", "RC2 recovery canceled: no idle window before next attempt");
    reconnect_peer_pending_ = false;
    reconnect_peer_attempt_ = true;
  } else if (std::strcmp(phase, "retry_after_disconnect") == 0 && reconnect_peer_attempt_) {
    reconnect_peer_attempt_ = false;
    reconnect_peer_pending_ = true;
    ESP_LOGI("espnow.peer", "RC2 recovery queued; waiting for idle transmitter; peers=%u", unsigned(radio_.peer_count()));
  }
}

void EspNowNetProtocolComponent::reconnect_peer_recovery_tick_(bool idle) {
#ifdef USE_WIFI_FIXED_CHANNEL
  if (reconnect_wifi_ != nullptr &&
      (!reconnect_wifi_->fixed_channel_operation() || reconnect_wifi_->is_disabled())) {
    reconnect_peer_attempt_ = false;
    reconnect_peer_pending_ = false;
    return;
  }
#endif
  if (!runtime_enabled_ || !radio_.initialized() || is_failed()) return;
  if (reconnect_peer_pending_ && idle) {
    reconnect_peer_pending_ = false;
    this->log_peer_diagnostics("rc2_before_recovery");
    for (size_t i = 0; i < radio_.peer_count(); ++i) {
      const auto result = radio_.diagnostic_reapply_peer(static_cast<PeerIndex>(i));
      if (result == ESP_OK) {
        ESP_LOGI("espnow.peer", "RC2 peer_reapply peer=%u result=0", unsigned(i));
      } else {
        ESP_LOGW("espnow.peer", "RC2 peer_reapply peer=%u result=%d; retry after next failed association", unsigned(i), int(result));
      }
    }
    this->log_peer_diagnostics("rc2_after_recovery");
  }
}

void EspNowNetProtocolComponent::log_peer_diagnostics(const char *phase) {
  uint8_t channel = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  wifi_mode_t mode = WIFI_MODE_NULL;
  const auto channel_error = esp_wifi_get_channel(&channel, &secondary);
  const auto mode_error = esp_wifi_get_mode(&mode);
  ESP_LOGD("espnow.peer", "HB3 phase=%s radio_channel=%d channel_error=%d mode=%d mode_error=%d",
           phase, channel_error == ESP_OK ? int(channel) : -1, int(channel_error),
           mode_error == ESP_OK ? int(mode) : -1, int(mode_error));
  esp_now_peer_num_t count{};
  const auto count_error = esp_now_get_peer_num(&count);
  ESP_LOGD("espnow.peer", "HB3 phase=%s count_error=%d total=%d encrypted=%d rx=%u tx=%u",
           phase, int(count_error), count_error == ESP_OK ? int(count.total_num) : -1,
           count_error == ESP_OK ? int(count.encrypt_num) : -1,
           unsigned(radio_.received_frame_count()), unsigned(radio_.sent_frame_count()));
  for (size_t i = 0; i < radio_.peer_count(); ++i) {
    const auto *expected = radio_.peer(static_cast<PeerIndex>(i));
    if (expected == nullptr) continue;
    esp_now_peer_info_t actual{};
    const auto error = esp_now_get_peer(expected->address, &actual);
    if (error != ESP_OK) {
      ESP_LOGW("espnow.peer", "HB3 phase=%s peer=%u get_error=%d fields=unavailable", phase, unsigned(i), int(error));
      continue;
    }
    const bool same_lmk = std::memcmp(actual.lmk, expected->lmk, PeerIdentity::KEY_SIZE) == 0;
    ESP_LOGD("espnow.peer", "HB3 phase=%s peer=%u get_error=0 channel=%u ifidx=%d encrypt=%d lmk_match=%d",
             phase, unsigned(i), unsigned(actual.channel), int(actual.ifidx), int(actual.encrypt), int(same_lmk));
  }
}

}  // namespace esphome::espnow_net_protocol
