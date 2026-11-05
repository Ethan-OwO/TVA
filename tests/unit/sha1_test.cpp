#include "core/sha1.h"

#include <gtest/gtest.h>

#include <random>
#include <string>

namespace tva::core {
namespace {

std::string hex(const std::array<std::uint8_t, 20>& d) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (auto b : d) {
    out += digits[b >> 4];
    out += digits[b & 0xF];
  }
  return out;
}

TEST(Sha1, KnownVectors) {
  EXPECT_EQ(hex(sha1(as_bytes(""))), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  EXPECT_EQ(hex(sha1(as_bytes("abc"))), "a9993e364706816aba3e25717850c26c9cd0d89d");
  // 448-bit message: padding must spill into a second block.
  EXPECT_EQ(hex(sha1(as_bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(Sha1, OneMillionA) {
  const std::string a(1'000'000, 'a');
  EXPECT_EQ(hex(sha1(as_bytes(a))), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(Sha1, ByteByByteMatchesOneShot) {
  // Lengths 0..200 cross every block boundary (55/56/63/64/119/120/128...),
  // which is where buffering and padding bugs live.
  std::mt19937 rng(12345);
  for (std::size_t len = 0; len <= 200; ++len) {
    Bytes data(len);
    for (auto& b : data) b = static_cast<std::uint8_t>(rng());
    Sha1 incremental;
    for (std::size_t i = 0; i < len; ++i) incremental.update(ByteView(&data[i], 1));
    EXPECT_EQ(incremental.finish(), sha1(data)) << "length " << len;
  }
}

TEST(Sha1, FinishResetsState) {
  Sha1 s;
  s.update(as_bytes("garbage"));
  (void)s.finish();
  s.update(as_bytes("abc"));
  EXPECT_EQ(hex(s.finish()), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

}  // namespace
}  // namespace tva::core
