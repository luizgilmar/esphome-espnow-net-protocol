#include <cassert>
#include <cstring>

#include "../components/espnow_net_protocol/command_codec.h"

using namespace esphome::espnow_net_protocol;

int main() {
  EspNowCommandCodec codec;
  NetCommand command{};
  command.transaction_id = 123;
  command.timeout_ms = 2000;
  assert(command.device_id.assign("hub"));
  assert(command.resource.assign("light/example"));
  assert(command.name.assign("toggle"));
  EspNowCommandPayload legacy{};
  assert(codec.encode(command, legacy));
  assert(legacy.data.data()[0] == EspNowCommandCodec::VERSION);
  NetCommand decoded{};
  assert(codec.decode(123, legacy.data.data(), legacy.data.size(), decoded));
  assert(decoded.source_device_id.empty() && decoded.source_boot_id == 0);

  assert(command.source_device_id.assign("tx"));
  assert(!command.valid());  // Incomplete application identity is invalid.
  command.source_boot_id = 17710912490599128940ULL;
  EspNowCommandPayload identified{};
  assert(codec.encode(command, identified));
  assert(identified.data.data()[0] == EspNowCommandCodec::IDENTITY_VERSION);
  assert(codec.decode(123, identified.data.data(), identified.data.size(), decoded));
  assert(std::strcmp(decoded.source_device_id.c_str(), "tx") == 0);
  assert(decoded.source_boot_id == command.source_boot_id);
  assert(decoded.transaction_id == 123 && decoded.timeout_ms == 2000);
  assert(!codec.decode(123, identified.data.data(), identified.data.size() - 1, decoded));
  assert(!codec.decode(123, identified.data.data(), legacy.data.size(), decoded));

  NetCommand old_command = command;
  old_command.source_device_id.clear();
  assert(!old_command.valid());
  old_command.source_boot_id = 0;
  assert(codec.encode(old_command, legacy));
  assert(legacy.data.data()[0] == EspNowCommandCodec::VERSION);
}
