#pragma once

#include "radio_diagnostic_history.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace espnow_net_protocol {
class EspIdfEspNowEncryptedRadio;

class RadioDiagnostics {
 public:
  void setup();
  void loop(uint32_t now, const EspIdfEspNowEncryptedRadio &radio);
  void request_dump();
  void save_before_shutdown(const EspIdfEspNowEncryptedRadio &radio);

 protected:
  RadioDiagnosticSample capture_(uint32_t now, const EspIdfEspNowEncryptedRadio &radio);
  void log_sample_(const char *source, const RadioDiagnosticSample &sample) const;
  void replay_one_(uint32_t now);
  void begin_current_replay_();
  RadioDiagnosticHistory live_{};
  RadioDiagnosticHistory replay_{};
  RadioDiagnosticSample last_{};
  ESPPreferenceObject preference_{};
  uint32_t last_poll_{0}, last_sample_{0}, last_loop_{0}, max_loop_gap_{0};
  uint32_t replay_due_{0};
  uint32_t last_probe_log_{0};
  uint16_t replay_index_{0};
  uint8_t replay_phase_{0}; // 0 idle, 1 saved boot, 2 current boot
  bool initialized_{false}, sampled_{false}, dump_pending_{false}, saved_{false};
};
}  // namespace espnow_net_protocol
}  // namespace esphome
