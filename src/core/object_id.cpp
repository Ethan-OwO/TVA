#include "core/object_id.h"

namespace tva::core {
namespace {

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::string ObjectId::hex() const {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(40, '0');
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[2 * i] = kDigits[bytes[i] >> 4];
    out[2 * i + 1] = kDigits[bytes[i] & 0xF];
  }
  return out;
}

std::optional<ObjectId> ObjectId::from_hex(std::string_view hex40) {
  if (hex40.size() != 40) return std::nullopt;
  ObjectId id;
  for (std::size_t i = 0; i < 20; ++i) {
    int hi = hex_value(hex40[2 * i]);
    int lo = hex_value(hex40[2 * i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    id.bytes[i] = static_cast<std::uint8_t>(hi << 4 | lo);
  }
  return id;
}

bool is_hex(std::string_view s) {
  for (char c : s) {
    if (hex_value(c) < 0) return false;
  }
  return true;
}

}  // namespace tva::core
