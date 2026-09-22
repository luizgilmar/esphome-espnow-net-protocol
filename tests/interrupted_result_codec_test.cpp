#include <cassert>

#include "components/espnow_net_protocol/result_codec.h"

using namespace esphome::espnow_net_protocol;

int main() {
  NetResult input{};
  input.transaction_id = 41;
  input.status = NetResultStatus::FAILED;
  input.execution.started = true;
  input.error.code = NetErrorCode::INTERRUPTED;
  assert(input.error.message.assign("operation interrupted by command"));

  EspNowResultPayload encoded{};
  EspNowResultCodec codec{};
  assert(codec.encode(input, encoded));

  NetResult decoded{};
  assert(codec.decode(input.transaction_id, encoded.data.data(),
                      encoded.data.size(), 25, decoded));
  assert(decoded.status == NetResultStatus::FAILED);
  assert(decoded.execution.started);
  assert(decoded.error.code == NetErrorCode::INTERRUPTED);
}
