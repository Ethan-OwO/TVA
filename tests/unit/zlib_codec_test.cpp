#include "core/zlib_codec.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "core/error.h"

namespace tva::core {
namespace {

Bytes random_bytes(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  Bytes b(n);
  for (auto& x : b) x = static_cast<std::uint8_t>(rng());
  return b;
}

TEST(ZlibCodec, RoundTripEmpty) {
  Bytes packed = zlib_deflate({});
  EXPECT_EQ(packed[0], 0x78);  // zlib header, not raw deflate / gzip
  EXPECT_TRUE(zlib_inflate(packed).empty());
}

TEST(ZlibCodec, RoundTripOneByte) {
  Bytes one{0x42};
  EXPECT_EQ(zlib_inflate(zlib_deflate(one)), one);
}

TEST(ZlibCodec, RoundTripOneMiBRandom) {
  Bytes data = random_bytes(1 << 20, 7);
  EXPECT_EQ(zlib_inflate(zlib_deflate(data)), data);
}

TEST(ZlibCodec, CorruptDataThrows) {
  Bytes packed = zlib_deflate(to_bytes(std::string(1000, 'x') + "tail"));
  Bytes flipped = packed;
  flipped[flipped.size() / 2] ^= 0xFF;
  EXPECT_THROW(zlib_inflate(flipped), Error);

  Bytes bad_checksum = packed;
  bad_checksum.back() ^= 0x01;  // adler32 mismatch
  EXPECT_THROW(zlib_inflate(bad_checksum), Error);

  Bytes truncated(packed.begin(), packed.end() - 3);
  EXPECT_THROW(zlib_inflate(truncated), Error);

  Bytes trailing = packed;
  trailing.push_back(0);
  EXPECT_THROW(zlib_inflate(trailing), Error);

  EXPECT_THROW(zlib_inflate(to_bytes("definitely not zlib")), Error);
  EXPECT_THROW(zlib_inflate({}), Error);
}

TEST(ZlibCodec, InflatePrefixStopsEarly) {
  Bytes data = random_bytes(200'000, 3);
  Bytes packed = zlib_deflate(data);
  auto [head, consumed] = zlib_inflate_prefix(packed, 32);
  ASSERT_EQ(head.size(), 32u);
  EXPECT_TRUE(std::equal(head.begin(), head.end(), data.begin()));
  EXPECT_LT(consumed, packed.size());

  auto [all, used] = zlib_inflate_prefix(packed, 10'000'000);
  EXPECT_EQ(all, data);
  EXPECT_EQ(used, packed.size());
}

}  // namespace
}  // namespace tva::core
