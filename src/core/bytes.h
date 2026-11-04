#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tva::core {

// File contents, object payloads, hashes: everything is raw bytes. No newline
// or encoding conversion ever happens on these.
using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

inline ByteView as_bytes(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

inline std::string_view as_chars(ByteView b) {
  return {reinterpret_cast<const char*>(b.data()), b.size()};
}

inline Bytes to_bytes(std::string_view s) { return Bytes(s.begin(), s.end()); }

}  // namespace tva::core
