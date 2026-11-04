#include "core/sha1.h"

#include <algorithm>
#include <cstring>

namespace tva::core {
namespace {

constexpr std::uint32_t rotl(std::uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

}  // namespace

Sha1::Sha1() : h_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u} {}

void Sha1::update(ByteView data) {
  // memcpy with a null source is UB even for size 0, and empty spans may
  // carry a null data().
  if (data.empty()) return;
  total_len_ += data.size();

  std::size_t pos = 0;
  if (buffer_len_ > 0) {
    std::size_t take = std::min(buffer_.size() - buffer_len_, data.size());
    std::memcpy(buffer_.data() + buffer_len_, data.data(), take);
    buffer_len_ += take;
    pos = take;
    if (buffer_len_ < buffer_.size()) return;
    process_block(buffer_.data());
    buffer_len_ = 0;
  }
  for (; pos + 64 <= data.size(); pos += 64) process_block(data.data() + pos);
  if (pos < data.size()) {
    buffer_len_ = data.size() - pos;
    std::memcpy(buffer_.data(), data.data() + pos, buffer_len_);
  }
}

std::array<std::uint8_t, 20> Sha1::finish() {
  // Padding: a single 1 bit (0x80), zeros up to 56 mod 64, then the message
  // length in bits as a 64-bit big-endian integer.
  const std::uint64_t bit_len = total_len_ * 8;
  buffer_[buffer_len_++] = 0x80;
  if (buffer_len_ > 56) {
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_len_), buffer_.end(), 0);
    process_block(buffer_.data());
    buffer_len_ = 0;
  }
  std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_len_), buffer_.begin() + 56, 0);
  for (int i = 0; i < 8; ++i) {
    buffer_[56 + i] = static_cast<std::uint8_t>(bit_len >> (56 - 8 * i));
  }
  process_block(buffer_.data());

  std::array<std::uint8_t, 20> digest{};
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 4; ++j) {
      digest[4 * i + j] = static_cast<std::uint8_t>(h_[i] >> (24 - 8 * j));
    }
  }
  *this = Sha1();
  return digest;
}

void Sha1::process_block(const std::uint8_t* p) {
  std::uint32_t w[80];
  for (int t = 0; t < 16; ++t) {
    // Widen before shifting: uint8_t promotes to int, and 0xFF << 24 would
    // overflow a signed int.
    w[t] = (std::uint32_t{p[4 * t]} << 24) | (std::uint32_t{p[4 * t + 1]} << 16) |
           (std::uint32_t{p[4 * t + 2]} << 8) | std::uint32_t{p[4 * t + 3]};
  }
  for (int t = 16; t < 80; ++t) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);

  std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
  for (int t = 0; t < 80; ++t) {
    std::uint32_t f, k;
    if (t < 20) {
      f = (b & c) | (~b & d);  // Ch
      k = 0x5A827999u;
    } else if (t < 40) {
      f = b ^ c ^ d;  // Parity
      k = 0x6ED9EBA1u;
    } else if (t < 60) {
      f = (b & c) | (b & d) | (c & d);  // Maj
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;  // Parity
      k = 0xCA62C1D6u;
    }
    std::uint32_t temp = rotl(a, 5) + f + e + k + w[t];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = temp;
  }
  h_[0] += a;
  h_[1] += b;
  h_[2] += c;
  h_[3] += d;
  h_[4] += e;
}

std::array<std::uint8_t, 20> sha1(ByteView data) {
  Sha1 s;
  s.update(data);
  return s.finish();
}

}  // namespace tva::core
