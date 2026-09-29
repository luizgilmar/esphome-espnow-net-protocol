#pragma once

#include "esphome/components/wifi/radio_diagnostics.h"

namespace esphome {
namespace espnow_net_protocol {

// Persisted representation contains no pointers, keys, SSIDs or packet payloads.
struct RadioDiagnosticSample {
  uint32_t uptime_ms{0};
  uint32_t rx{0}, tx{0}, failed{0}, stops{0};
  uint32_t max_loop_gap_ms{0};
  int32_t channel_error{0}, association_error{0}, init_error{0};
  uint16_t flags{0}, disconnect_reason{0};
  uint8_t channel{0}, mode{255}, wifi_state{0}, arbitration{0};
};

enum RadioDiagnosticFlag : uint16_t {
  DRIVER_STARTED = 1, STA_STARTED = 2, ASSOCIATED = 4, WIFI_CONNECTED = 8,
  SUPPRESSION_REQUESTED = 16, SUPPRESSION_ACTIVE = 32, ESPNOW_INITIALIZED = 64,
  API_CONNECTED = 128, MQTT_CONNECTED = 256, CONNECTING = 512,
  RECOVERY_PENDING = 1024, RECOVERY_RUNNING = 2048, RECOVERY_PERMITTED = 4096,
  MODE_VALID = 8192,
};

struct RadioDiagnosticHistory {
  static constexpr uint32_t MAGIC = 0x52444731U;  // RDG1; change for incompatible layouts
  uint32_t magic{MAGIC};
  uint32_t boot_id{0}, saved_at_ms{0}, reset_reason{0};
  uint32_t first_wifi_loss_ms{0}, first_api_loss_ms{0};
  wifi::RadioDiagnosticRing<RadioDiagnosticSample, 32> samples{};
  wifi::RadioDiagnosticEvents events{};
  bool valid() const { return magic == MAGIC && samples.valid() && events.valid(); }
};
static_assert(sizeof(RadioDiagnosticHistory) <= 2304, "Keep diagnostic RAM/flash bounded");
static_assert(std::is_trivially_copyable<RadioDiagnosticHistory>::value, "Persistence needs a plain value type");

}  // namespace espnow_net_protocol
}  // namespace esphome
