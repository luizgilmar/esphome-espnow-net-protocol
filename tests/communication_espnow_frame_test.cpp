#include <cassert>
#include <cstdint>

#include "components/espnow_net_protocol/frame.h"

using namespace esphome::espnow_net_protocol;

int main() {
  EspNowFrameCodec codec{};
  uint8_t payload[500]{};
  for (size_t index = 0; index < sizeof(payload); index++)
    payload[index] = static_cast<uint8_t>(index & 0xFFU);

  EspNowFrameEnvelope envelope{};
  envelope.kind = EspNowFrameKind::COMMAND;
  envelope.flags = EspNowFrameCodec::FLAG_ACK_REQUIRED;
  envelope.source_boot_id = 0x1122334455667788ULL;
  envelope.transaction_id = 9001;
  envelope.sequence = 17;

  assert(codec.fragment_count(sizeof(payload)) == 3);
  EspNowRadioFrame frames[EspNowFrameCodec::MAX_FRAGMENTS]{};
  for (uint8_t index = 0; index < 3; index++)
    assert(codec.encode_fragment(envelope, payload, sizeof(payload), index,
                                 frames[index]));

  EspNowFrameReassembler reassembler{};
  assert(reassembler.accept(frames[2].data.data(), frames[2].data.size()));
  assert(reassembler.accept(frames[0].data.data(), frames[0].data.size()));
  assert(reassembler.accept(frames[0].data.data(), frames[0].data.size()));
  assert(!reassembler.complete());
  assert(reassembler.accept(frames[1].data.data(), frames[1].data.size()));
  assert(reassembler.complete());

  EspNowReassembledMessage message{};
  assert(reassembler.take(message));
  assert(message.envelope.transaction_id == 9001);
  assert(message.envelope.source_boot_id == envelope.source_boot_id);
  assert(message.data.size() == sizeof(payload));
  for (size_t index = 0; index < sizeof(payload); index++)
    assert(message.data.data()[index] == payload[index]);

  assert(codec.fragment_count(641) == 0);
  assert(!codec.encode_fragment(envelope, payload, 641, 0, frames[0]));
  assert(codec.last_error() == EspNowFrameError::MESSAGE_TOO_LARGE);

  assert(codec.encode_fragment(envelope, payload, 10, 0, frames[0]));
  uint8_t corrupted[EspNowRadioFrame::MAX_SIZE]{};
  const size_t frame_size = frames[0].data.size();
  for (size_t index = 0; index < frame_size; index++)
    corrupted[index] = frames[0].data.data()[index];
  corrupted[frame_size - 1] ^= 0x01;
  assert(reassembler.accept(corrupted, frame_size));
  assert(!reassembler.take(message));
  assert(reassembler.last_error() == EspNowFrameError::CHECKSUM_MISMATCH);
  return 0;
}
