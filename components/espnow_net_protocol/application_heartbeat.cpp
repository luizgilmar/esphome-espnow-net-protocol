#include "esphome/core/defines.h"
#ifdef USE_ESPNOW_APPLICATION_HEARTBEAT
#include "espnow_net_protocol.h"
#include "esphome/core/log.h"
#include <esp_wifi.h>
#include <cstring>

namespace esphome::espnow_net_protocol {
static const char *const HB_TAG = "espnow.hb";

void EspNowNetProtocolComponent::diagnostic_peer_trial_phase(const char *phase) {
  this->log_peer_diagnostics(phase);
  if (std::strcmp(phase, "wifi_connected") == 0) {
    peer_trial_attempt_ = false;
    if (peer_trial_pending_) ESP_LOGI("espnow.peer", "HB5 trial canceled: Wi-Fi recovered");
    peer_trial_pending_ = false;
  } else if (std::strcmp(phase, "before_attempt") == 0) {
    peer_trial_attempt_ = true;
  } else if (std::strcmp(phase, "retry_after_disconnect") == 0 && peer_trial_attempt_) {
    peer_trial_attempt_ = false;
    if (!peer_trial_done_ && !peer_trial_pending_) {
      peer_trial_pending_ = true;
      peer_trial_started_ = millis();
      ESP_LOGI("espnow.peer", "HB5 peer-only trial scheduled in 7000ms; once per boot");
    }
  }
}

void EspNowNetProtocolComponent::diagnostic_callback_trial_phase(const char *phase) {
  this->log_peer_diagnostics(phase);
  if (std::strcmp(phase, "wifi_connected") == 0) {
    callback_trial_attempt_ = false;
    if (callback_trial_pending_) ESP_LOGI("espnow.peer", "HB4 trial canceled: Wi-Fi recovered");
    callback_trial_pending_ = false;
  } else if (std::strcmp(phase, "before_attempt") == 0) {
    callback_trial_attempt_ = true;
  } else if (std::strcmp(phase, "retry_after_disconnect") == 0 && callback_trial_attempt_) {
    callback_trial_attempt_ = false;
    if (!callback_trial_done_ && !callback_trial_pending_) {
      callback_trial_pending_ = true;
      callback_trial_started_ = millis();
      ESP_LOGI("espnow.peer", "HB4 callback-only trial scheduled in 7000ms; once per boot");
    }
  }
}

void EspNowNetProtocolComponent::log_peer_diagnostics(const char *phase) {
  uint8_t channel = 0;
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  wifi_mode_t mode = WIFI_MODE_NULL;
  const auto channel_error = esp_wifi_get_channel(&channel, &secondary);
  const auto mode_error = esp_wifi_get_mode(&mode);
  ESP_LOGI("espnow.peer", "HB3 phase=%s radio_channel=%d channel_error=%d mode=%d mode_error=%d",
           phase, channel_error == ESP_OK ? int(channel) : -1, int(channel_error),
           mode_error == ESP_OK ? int(mode) : -1, int(mode_error));
  esp_now_peer_num_t count{};
  const auto count_error = esp_now_get_peer_num(&count);
  ESP_LOGI("espnow.peer", "HB3 phase=%s count_error=%d total=%d encrypted=%d rx=%u tx=%u",
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
    ESP_LOGI("espnow.peer", "HB3 phase=%s peer=%u get_error=0 channel=%u ifidx=%d encrypt=%d lmk_match=%d",
             phase, unsigned(i), unsigned(actual.channel), int(actual.ifidx), int(actual.encrypt), int(same_lmk));
  }
}

bool EspNowNetProtocolComponent::receive_heartbeat_(const EspNowReceivedFrame &received, uint32_t now) {
  const auto &bytes = received.frame.data;
  if (!HeartbeatPacket::has_magic(bytes.data(), bytes.size())) return false;
  // A heartbeat may only use the explicitly configured peer. No command handler,
  // relay, result observer, or production transaction sees these packets.
  HeartbeatPacket packet{};
  if (received.peer_index != heartbeat_peer_ || !HeartbeatPacket::decode(bytes.data(), bytes.size(), packet))
    return true;
  ESP_LOGD(HB_TAG, "HB2 RX kind=%s seq=%u rssi_dbm=%d channel=%u",
           packet.kind == 1 ? "PING" : "PONG", unsigned(packet.sequence),
           int(received.rssi), unsigned(received.channel));
  if (packet.kind == 1) {
    // One bounded reply slot; later requests never overwrite a pending response.
    if (!heartbeat_reply_pending_) {
      heartbeat_reply_ = packet;
      heartbeat_reply_.kind = 2;
      heartbeat_reply_.responder_boot = local_boot_id_;
      heartbeat_reply_received_ = now;
      heartbeat_reply_pending_ = true;
      ESP_LOGD(HB_TAG, "HB1 received PING seq=%u peer=%u", unsigned(packet.sequence), unsigned(received.peer_index));
    }
  } else {
    uint32_t rtt = 0;
    if (heartbeat_exchange_.accept(packet, now, rtt)) {
      ++heartbeat_ok_;
      ESP_LOGI(HB_TAG, "HB1 PONG seq=%u rtt_ms=%u sent=%u ok=%u timeout=%u send_error=%u responder_boot=%llu",
               unsigned(packet.sequence), unsigned(rtt), unsigned(heartbeat_sent_), unsigned(heartbeat_ok_),
               unsigned(heartbeat_lost_), unsigned(heartbeat_send_errors_),
               static_cast<unsigned long long>(packet.responder_boot));
    } else {
      ESP_LOGD(HB_TAG, "HB1 ignored stale/duplicate/unmatched PONG seq=%u", unsigned(packet.sequence));
    }
  }
  return true;
}

void EspNowNetProtocolComponent::heartbeat_tick_(uint32_t now) {
  if (peer_trial_pending_ && uint32_t(now - peer_trial_started_) >= 15000U) {
    peer_trial_pending_ = false;
    peer_trial_done_ = true;
    ESP_LOGW("espnow.peer", "HB5 trial canceled: missed idle window; reset bench to repeat");
  }
  if (peer_trial_pending_ && uint32_t(now - peer_trial_started_) >= 7000U &&
      radio_transmission_owner_ == RadioTransmissionOwner::NONE &&
      reliable_message_owner_ == ReliableMessageOwner::NONE && sender_.state() == ReliableSenderState::IDLE) {
    peer_trial_pending_ = false;
    peer_trial_done_ = true;
    this->log_peer_diagnostics("before_peer_mod");
    const auto result = radio_.diagnostic_reapply_peer(heartbeat_peer_);
    ESP_LOGI("espnow.peer", "HB5 peer_reapply result=%d peer=%u; same LMK; no callback registration or deinit",
             int(result), unsigned(heartbeat_peer_));
    this->log_peer_diagnostics("after_peer_mod");
  }
  if (callback_trial_pending_ && uint32_t(now - callback_trial_started_) >= 7000U) {
    callback_trial_pending_ = false;
    callback_trial_done_ = true;
    this->log_peer_diagnostics("before_callback_register");
    const auto result = radio_.diagnostic_reregister_receive_callback();
    ESP_LOGI("espnow.peer", "HB4 register_recv_cb result=%d; peers and keys unchanged; no deinit", int(result));
    this->log_peer_diagnostics("after_callback_register");
  }
  if (uint32_t(now - heartbeat_rf_logged_) >= 10000U) {
    heartbeat_rf_logged_ = now;
    int8_t power = 0;
    wifi_ps_type_t ps = WIFI_PS_NONE;
    const esp_err_t power_error = esp_wifi_get_max_tx_power(&power);
    const esp_err_t ps_error = esp_wifi_get_ps(&ps);
    ESP_LOGI(HB_TAG,
             "HB2 RF tx_limit_qdbm=%d tx_limit_dbm=%.2f power_error=%d ps=%d ps_error=%d "
             "rx=%u tx=%u fail=%u dropped_rx=%u dropped_completion=%u rx_queue=%u completion_queue=%u",
             int(power), double(power) / 4.0, int(power_error), int(ps), int(ps_error),
             unsigned(radio_.received_frame_count()), unsigned(radio_.sent_frame_count()),
             unsigned(radio_.failed_send_count()), unsigned(radio_.dropped_frame_count()),
             unsigned(radio_.dropped_completion_count()), unsigned(radio_.received_queue_depth()),
             unsigned(radio_.completion_queue_depth()));
  }
  if (heartbeat_exchange_.expired(now)) {
    heartbeat_exchange_.reset();
    ++heartbeat_lost_;
    ESP_LOGW(HB_TAG, "HB1 TIMEOUT seq=%u sent=%u ok=%u timeout=%u send_error=%u",
             unsigned(heartbeat_sequence_), unsigned(heartbeat_sent_), unsigned(heartbeat_ok_),
             unsigned(heartbeat_lost_), unsigned(heartbeat_send_errors_));
  }
  if (heartbeat_reply_pending_ && uint32_t(now - heartbeat_reply_received_) >= heartbeat_timeout_) {
    heartbeat_reply_pending_ = false;
    ESP_LOGW(HB_TAG, "HB1 reply expired while transmitter busy");
  }
}

void EspNowNetProtocolComponent::dispatch_heartbeat_(uint32_t now) {
  if (heartbeat_peer_ == INVALID_PEER_INDEX || radio_transmission_owner_ != RadioTransmissionOwner::NONE ||
      reliable_message_owner_ != ReliableMessageOwner::NONE || sender_.state() != ReliableSenderState::IDLE)
    return; // Production messages have priority; heartbeat never takes their ownership.
  if (heartbeat_reply_pending_) {
    uint8_t payload[HeartbeatPacket::SIZE];
    heartbeat_reply_.encode(payload);
    heartbeat_reply_pending_ = false;
    if (radio_.send_frame(heartbeat_peer_, payload, sizeof(payload))) {
      radio_transmission_owner_ = RadioTransmissionOwner::HEARTBEAT;
      radio_transmission_peer_ = heartbeat_peer_;
      heartbeat_inflight_sequence_ = heartbeat_reply_.sequence;
      heartbeat_inflight_kind_ = 2;
      ESP_LOGI(HB_TAG, "HB1 sent PONG seq=%u", unsigned(heartbeat_reply_.sequence));
    } else {
      ESP_LOGW(HB_TAG, "HB1 reply send rejected seq=%u", unsigned(heartbeat_reply_.sequence));
    }
    return;
  }
  if (!heartbeat_interval_ || heartbeat_exchange_.pending() ||
      uint32_t(now - heartbeat_last_attempt_) < heartbeat_interval_) return;
  heartbeat_last_attempt_ = now;
  ++heartbeat_sequence_;
  if (!heartbeat_sequence_) ++heartbeat_sequence_;
  HeartbeatPacket ping{};
  ping.requester_boot = local_boot_id_;
  ping.sequence = heartbeat_sequence_;
  uint8_t payload[HeartbeatPacket::SIZE];
  ping.encode(payload);
  ++heartbeat_sent_;
  if (!radio_.send_frame(heartbeat_peer_, payload, sizeof(payload))) {
    ++heartbeat_send_errors_;
    ESP_LOGW(HB_TAG, "HB1 SEND_ERROR seq=%u sent=%u ok=%u timeout=%u send_error=%u",
             unsigned(heartbeat_sequence_), unsigned(heartbeat_sent_), unsigned(heartbeat_ok_),
             unsigned(heartbeat_lost_), unsigned(heartbeat_send_errors_));
    return;
  }
  heartbeat_exchange_.start(ping, now, heartbeat_timeout_);
  radio_transmission_owner_ = RadioTransmissionOwner::HEARTBEAT;
  radio_transmission_peer_ = heartbeat_peer_;
  heartbeat_inflight_sequence_ = heartbeat_sequence_;
  heartbeat_inflight_kind_ = 1;
  ESP_LOGI(HB_TAG, "HB1 sent PING seq=%u peer=%u", unsigned(heartbeat_sequence_), unsigned(heartbeat_peer_));
}
} // namespace esphome::espnow_net_protocol
#endif
