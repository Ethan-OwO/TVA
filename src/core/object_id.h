#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace tva::core {

// A 20-byte SHA-1 object name.
struct ObjectId {
  std::array<std::uint8_t, 20> bytes{};

  std::string hex() const;
  // Accepts exactly 40 hex digits (either case); anything else is nullopt.
  static std::optional<ObjectId> from_hex(std::string_view hex40);

  auto operator<=>(const ObjectId&) const = default;
};

bool is_hex(std::string_view s);

}  // namespace tva::core

template <>
struct std::hash<tva::core::ObjectId> {
  std::size_t operator()(const tva::core::ObjectId& id) const noexcept {
    // SHA-1 output is uniformly distributed, so any 8 bytes make a good hash.
    std::size_t h;
    std::memcpy(&h, id.bytes.data(), sizeof h);
    return h;
  }
};
