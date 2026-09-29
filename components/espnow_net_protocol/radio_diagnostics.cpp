#include "esphome/core/defines.h"
#ifdef USE_ESPNOW_NET_PROTOCOL_DIAGNOSTICS
#include "radio_diagnostics.h"
#include "espidf_espnow_encrypted_radio.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif
#include <esp_system.h>
#include <esp_random.h>
#include <esp_wifi.h>

namespace esphome {
namespace espnow_net_protocol {
static const char *const TAG = "radio_diag";
static constexpr uint32_t PREFERENCE_KEY = 0x52444731U;

static const char *event_name(wifi::RadioDiagnosticEventKind kind) {
  using E = wifi::RadioDiagnosticEventKind;
  switch (kind) {
    case E::WIFI_STOP_CALL: return "wifi_stop_call";
    case E::WIFI_STOP_RESULT: return "wifi_stop_result";
    case E::WIFI_START_RESULT: return "wifi_start_result";
    case E::WIFI_MODE_RESULT: return "wifi_mode_result";
    case E::STA_START: return "sta_start";
    case E::STA_STOP: return "sta_stop";
    case E::ASSOCIATED: return "associated";
    case E::DISCONNECTED: return "disconnected";
    case E::GOT_IP: return "got_ip";
    case E::LOST_IP: return "lost_ip";
    case E::SCAN_START_RESULT: return "scan_start_result";
    case E::SCAN_STOP_RESULT: return "scan_stop_result";
    case E::DISCONNECT_RESULT: return "disconnect_result";
    case E::SET_CHANNEL_RESULT: return "set_channel_result";
    case E::ESPNOW_INIT_RESULT: return "espnow_init_result";
    case E::ESPNOW_DEINIT_CALL: return "espnow_deinit_call";
    case E::ESPNOW_DEINIT_RESULT: return "espnow_deinit_result";
    case E::STATE_CHANGE: return "state_change_flags_arb";
  }
  return "unknown";
}

void RadioDiagnostics::setup() {
  live_.boot_id = esp_random();
  live_.reset_reason = static_cast<uint32_t>(esp_reset_reason());
  if (global_preferences != nullptr)
    preference_ = global_preferences->make_preference<RadioDiagnosticHistory>(PREFERENCE_KEY, true);
  initialized_ = true;
  ESP_LOGI(TAG, "RDG1 boot=%u reset_reason=%u periodic=5000ms samples=32 events=64",
           static_cast<unsigned>(live_.boot_id), static_cast<unsigned>(live_.reset_reason));
}

RadioDiagnosticSample RadioDiagnostics::capture_(uint32_t now, const EspIdfEspNowEncryptedRadio &radio) {
  const bool trace = now - last_probe_log_ >= 5000U;
  if (trace) last_probe_log_ = now;
  RadioDiagnosticSample s{};
  s.uptime_ms = now;
  s.max_loop_gap_ms = max_loop_gap_;
  auto *w = wifi::global_wifi_component;
  if (w != nullptr) {
    const auto state = w->radio_diagnostic_state();
    s.wifi_state = state.state;
    s.disconnect_reason = state.last_disconnect_reason;
    s.stops = state.stop_calls;
    if (state.driver_started) s.flags |= DRIVER_STARTED;
    if (state.sta_started) s.flags |= STA_STARTED;
    if (state.connecting) s.flags |= CONNECTING;
    if (w->is_connected()) s.flags |= WIFI_CONNECTED;
    if (w->is_reconnect_suppression_requested()) s.flags |= SUPPRESSION_REQUESTED;
    if (w->is_reconnect_suppression_active()) s.flags |= SUPPRESSION_ACTIVE;
  }
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (trace) ESP_LOGI(TAG, "probe before get_mode up=%u", unsigned(now));
  if (esp_wifi_get_mode(&mode) == ESP_OK) {
    s.mode = static_cast<uint8_t>(mode);
    s.flags |= MODE_VALID;
  }
  wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  if (trace) ESP_LOGI(TAG, "probe before get_channel");
  s.channel_error = esp_wifi_get_channel(&s.channel, &secondary);
  if (s.channel_error != ESP_OK) s.channel = 0;
  wifi_ap_record_t ap{};
  if (trace) ESP_LOGI(TAG, "probe before get_ap_info");
  s.association_error = esp_wifi_sta_get_ap_info(&ap);
  if (trace) ESP_LOGI(TAG, "probe driver queries complete");
#ifdef USE_WIFI_FIXED_CHANNEL
  if (trace) {
    wifi_country_t country{};
    ESP_LOGI(TAG, "probe before get_country");
    const esp_err_t country_error = esp_wifi_get_country(&country);
    ESP_LOGI(TAG, "FC1 range start=%u count=%u manual=%u error=%d configuration=%u",
             unsigned(country.schan), unsigned(country.nchan),
             unsigned(country.policy == WIFI_COUNTRY_POLICY_MANUAL), int(country_error),
             unsigned(w != nullptr && w->network_configuration_mode()));
  }
#endif
  if (s.association_error == ESP_OK) s.flags |= ASSOCIATED;
#ifdef USE_API
  if (api::global_api_server != nullptr && api::global_api_server->is_connected()) s.flags |= API_CONNECTED;
#endif
#ifdef USE_MQTT
  if (mqtt::global_mqtt_client != nullptr && mqtt::global_mqtt_client->is_connected()) s.flags |= MQTT_CONNECTED;
#endif
  if (radio.initialized()) s.flags |= ESPNOW_INITIALIZED;
  if (radio.diagnostic_recovery_pending()) s.flags |= RECOVERY_PENDING;
  if (radio.diagnostic_recovery_in_progress()) s.flags |= RECOVERY_RUNNING;
  if (radio.diagnostic_recovery_permitted()) s.flags |= RECOVERY_PERMITTED;
  s.arbitration = radio.diagnostic_arbitration_state();
  s.init_error = radio.diagnostic_initialization_error();
  s.rx = radio.received_frame_count();
  s.tx = radio.sent_frame_count();
  s.failed = radio.failed_send_count();
  return s;
}

void RadioDiagnostics::log_sample_(const char *source, const RadioDiagnosticSample &s) const {
  auto flag_value = [&](uint16_t flag) -> unsigned { return (s.flags & flag) != 0; };
  ESP_LOGI(TAG,
      "%s up=%u drv=%u sta=%u assoc=%u net=%u api=%u mqtt=%u mode=%u ch=%u/%d "
      "wifi=%u arb=%u req=%u held=%u now=%u rec=%u/%u/%u reason=%u "
      "rx=%u tx=%u fail=%u stops=%u gap=%u init_err=%d ap_err=%d",
      source, static_cast<unsigned>(s.uptime_ms), flag_value(DRIVER_STARTED), flag_value(STA_STARTED),
      flag_value(ASSOCIATED), flag_value(WIFI_CONNECTED), flag_value(API_CONNECTED), flag_value(MQTT_CONNECTED),
      static_cast<unsigned>(s.mode), static_cast<unsigned>(s.channel), static_cast<int>(s.channel_error),
      static_cast<unsigned>(s.wifi_state), static_cast<unsigned>(s.arbitration),
      flag_value(SUPPRESSION_REQUESTED), flag_value(SUPPRESSION_ACTIVE), flag_value(ESPNOW_INITIALIZED),
      flag_value(RECOVERY_PENDING), flag_value(RECOVERY_RUNNING), flag_value(RECOVERY_PERMITTED),
      static_cast<unsigned>(s.disconnect_reason), static_cast<unsigned>(s.rx),
      static_cast<unsigned>(s.tx), static_cast<unsigned>(s.failed), static_cast<unsigned>(s.stops),
      static_cast<unsigned>(s.max_loop_gap_ms), static_cast<int>(s.init_error), static_cast<int>(s.association_error));
}

void RadioDiagnostics::request_dump() {
  if (dump_pending_ || replay_phase_ != 0) return;
  dump_pending_ = true;
  replay_due_ = millis() + 5000U; // allow log subscription/config dump to settle
}

void RadioDiagnostics::loop(uint32_t now, const EspIdfEspNowEncryptedRadio &radio) {
  if (!initialized_) return;
  if (last_loop_ != 0 && now - last_loop_ > max_loop_gap_) max_loop_gap_ = now - last_loop_;
  last_loop_ = now;
  if (!sampled_ || now - last_poll_ >= 250U) {
    last_poll_ = now;
    const auto s = capture_(now, radio);
    const bool changed = !sampled_ || s.flags != last_.flags || s.channel != last_.channel ||
        s.mode != last_.mode || s.wifi_state != last_.wifi_state || s.arbitration != last_.arbitration;
    if (sampled_) {
      if ((last_.flags & WIFI_CONNECTED) && !(s.flags & WIFI_CONNECTED) && live_.first_wifi_loss_ms == 0)
        live_.first_wifi_loss_ms = now;
      if ((last_.flags & API_CONNECTED) && !(s.flags & API_CONNECTED) && live_.first_api_loss_ms == 0)
        live_.first_api_loss_ms = now;
    }
    if (!(last_.flags & API_CONNECTED) && (s.flags & API_CONNECTED)) request_dump();
    if (changed) {
      if (wifi::global_wifi_component != nullptr)
        wifi::global_wifi_component->record_radio_diagnostic_event(
            wifi::RadioDiagnosticEventKind::STATE_CHANGE, s.flags, s.arbitration);
      log_sample_("change", s);
    }
    if (!sampled_ || now - last_sample_ >= 5000U) {
      live_.samples.push(s);
      last_sample_ = now;
      ESP_LOGI(TAG, "RDG1 boot=%u reset_reason=%u", static_cast<unsigned>(live_.boot_id),
               static_cast<unsigned>(live_.reset_reason));
      log_sample_("live", s);
    }
    last_ = s;
    sampled_ = true;
  }
  replay_one_(now);
}

void RadioDiagnostics::begin_current_replay_() {
  if (wifi::global_wifi_component != nullptr) live_.events = wifi::global_wifi_component->radio_diagnostic_events();
  replay_ = live_; // immutable during paced replay; live capture continues
  replay_phase_ = 2;
  replay_index_ = 0;
  ESP_LOGI(TAG, "history CURRENT boot=%u first_wifi_loss=%u first_api_loss=%u samples=%u/%u events=%u/%u",
      static_cast<unsigned>(replay_.boot_id), static_cast<unsigned>(replay_.first_wifi_loss_ms),
      static_cast<unsigned>(replay_.first_api_loss_ms), static_cast<unsigned>(replay_.samples.count),
      static_cast<unsigned>(replay_.samples.total), static_cast<unsigned>(replay_.events.count),
      static_cast<unsigned>(replay_.events.total));
}

void RadioDiagnostics::replay_one_(uint32_t now) {
  if ((!dump_pending_ && replay_phase_ == 0) || static_cast<int32_t>(now - replay_due_) < 0) return;
  // Never drain a history into a disconnected API; wait for the collector to return.
#ifdef USE_API
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected()) return;
#endif
  replay_due_ = now + 250U;
  if (dump_pending_) {
    dump_pending_ = false;
    if (preference_.load(&replay_) && replay_.valid() && replay_.boot_id != live_.boot_id) {
      replay_phase_ = 1;
      replay_index_ = 0;
      ESP_LOGI(TAG, "history SAVED boot=%u saved_up=%u current_boot=%u current_reset=%u samples=%u/%u events=%u/%u",
          static_cast<unsigned>(replay_.boot_id), static_cast<unsigned>(replay_.saved_at_ms),
          static_cast<unsigned>(live_.boot_id), static_cast<unsigned>(live_.reset_reason),
          static_cast<unsigned>(replay_.samples.count), static_cast<unsigned>(replay_.samples.total),
          static_cast<unsigned>(replay_.events.count), static_cast<unsigned>(replay_.events.total));
    } else {
      ESP_LOGI(TAG, "No valid saved shutdown history for an earlier boot");
      begin_current_replay_();
    }
    return;
  }
  if (replay_index_ < replay_.samples.count) {
    log_sample_(replay_phase_ == 1 ? "saved" : "history", replay_.samples.at(replay_index_++));
    return;
  }
  const size_t event_index = replay_index_ - replay_.samples.count;
  if (event_index < replay_.events.count) {
    const auto &e = replay_.events.at(event_index);
    ESP_LOGI(TAG, "%s event up=%u kind=%s result=%d detail=%u",
        replay_phase_ == 1 ? "saved" : "history", static_cast<unsigned>(e.uptime_ms),
        event_name(e.kind), static_cast<int>(e.result), static_cast<unsigned>(e.detail));
    ++replay_index_;
    return;
  }
  if (replay_phase_ == 1) begin_current_replay_();
  else {
    replay_phase_ = 0;
    ESP_LOGI(TAG, "history END");
  }
}

void RadioDiagnostics::save_before_shutdown(const EspIdfEspNowEncryptedRadio &radio) {
  if (!initialized_ || saved_ || global_preferences == nullptr) return;
  saved_ = true;
  live_.saved_at_ms = millis();
  live_.samples.push(capture_(live_.saved_at_ms, radio));
  if (wifi::global_wifi_component != nullptr) live_.events = wifi::global_wifi_component->radio_diagnostic_events();
  const bool queued = preference_.save(&live_);
  const bool synced = queued && global_preferences->sync();
  ESP_LOGI(TAG, "shutdown history saved=%s boot=%u up=%u", synced ? "YES" : "NO",
      static_cast<unsigned>(live_.boot_id), static_cast<unsigned>(live_.saved_at_ms));
}
}  // namespace espnow_net_protocol
}  // namespace esphome
#endif
