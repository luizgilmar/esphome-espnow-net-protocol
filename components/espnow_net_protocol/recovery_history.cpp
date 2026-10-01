#include "espnow_net_protocol.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#include <esp_wifi.h>

namespace esphome::espnow_net_protocol {
// Only called from the main loop. Event names must be static string literals.
void EspNowNetProtocolComponent::record_recovery_event_(const char *event, int result, int peer) {
#ifdef USE_ESPNOW_NET_PROTOCOL_DIAGNOSTICS
  uint8_t channel = 0;
  wifi_second_chan_t secondary;
  const auto err = esp_wifi_get_channel(&channel, &secondary);
  recovery_history_[recovery_next_] = {event, millis(), radio_.received_frame_count(),
      radio_.sent_frame_count(), result, peer, err == ESP_OK ? int(channel) : -1};
  recovery_next_ = (recovery_next_ + 1) % RECOVERY_HISTORY_SIZE;
  if (recovery_count_ < RECOVERY_HISTORY_SIZE) ++recovery_count_;
  ++recovery_total_;
#endif
}

void EspNowNetProtocolComponent::request_recovery_history() {
#ifdef USE_ESPNOW_NET_PROTOCOL_DIAGNOSTICS
  recovery_dump_requested_ = true;
  recovery_dump_due_ = millis() + 5000U; // Allow the log client to subscribe.
#endif
}

void EspNowNetProtocolComponent::recovery_history_tick_() {
#ifdef USE_ESPNOW_NET_PROTOCOL_DIAGNOSTICS
  if (!recovery_dump_requested_ && !recovery_dump_active_) return;
  const uint32_t now = millis();
  if (int32_t(now - recovery_dump_due_) < 0) return;
#ifdef USE_API
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected()) return;
#endif
  recovery_dump_due_ = now + 250U;
  if (recovery_dump_requested_) {
    recovery_dump_requested_ = false;
    recovery_dump_active_ = true;
    recovery_replay_count_ = recovery_count_;
    recovery_replay_index_ = 0;
    for (size_t i = 0; i < recovery_count_; ++i)
      recovery_replay_[i] = recovery_history_[(recovery_next_ + RECOVERY_HISTORY_SIZE - recovery_count_ + i) % RECOVERY_HISTORY_SIZE];
    ESP_LOGI("espnow.history", "RH1 BEGIN boot=%llu now_up_ms=%u count=%u overwritten=%u",
        static_cast<unsigned long long>(local_boot_id_), unsigned(now), unsigned(recovery_count_),
        unsigned(recovery_total_ - recovery_count_));
    return;
  }
  if (recovery_replay_index_ < recovery_replay_count_) {
    const auto &e = recovery_replay_[recovery_replay_index_++];
    ESP_LOGI("espnow.history", "RH1 event=%s up_ms=%u age_ms=%u peer=%d result=%d channel=%d rx=%u tx=%u",
        e.event, unsigned(e.uptime), unsigned(now - e.uptime), e.peer, e.result, e.channel,
        unsigned(e.rx), unsigned(e.tx));
    return;
  }
  recovery_dump_active_ = false;
  ESP_LOGI("espnow.history", "RH1 END");
#endif
}
}  // namespace esphome::espnow_net_protocol
