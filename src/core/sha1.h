#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/bytes.h"

namespace tva::core {

// SHA-1 per FIPS 180-4, written from scratch (no external hash library).
// Streaming: feed any number of update() calls, then finish() once. finish()
// resets the object, so it can be reused for a new message.
class Sha1 {
 public:
  Sha1();
  void update(ByteView data);
  std::array<std::uint8_t, 20> finish();

 private:
  void process_block(const std::uint8_t* block);

  std::array<std::uint32_t, 5> h_;
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_len_ = 0;
  std::uint64_t total_len_ = 0;  // message length in bytes
};

std::array<std::uint8_t, 20> sha1(ByteView data);

}  // namespace tva::core
