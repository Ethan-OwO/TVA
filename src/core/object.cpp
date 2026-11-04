#include "core/object.h"

#include <algorithm>
#include <limits>
#include <string>

#include "core/error.h"
#include "core/sha1.h"

namespace tva::core {
namespace {

std::string header_for(ObjectType type, std::size_t size) {
  std::string h(to_string(type));
  h += ' ';
  h += std::to_string(size);
  h += '\0';
  return h;
}

}  // namespace

std::string_view to_string(ObjectType type) {
  switch (type) {
    case ObjectType::Blob:
      return "blob";
    case ObjectType::Tree:
      return "tree";
    case ObjectType::Commit:
      return "commit";
  }
  return "unknown";
}

std::optional<ObjectType> parse_object_type(std::string_view name) {
  if (name == "blob") return ObjectType::Blob;
  if (name == "tree") return ObjectType::Tree;
  if (name == "commit") return ObjectType::Commit;
  if (name == "tag") throw Error("unsupported object type: tag");
  return std::nullopt;
}

Bytes encode_loose(ObjectType type, ByteView payload) {
  std::string header = header_for(type, payload.size());
  Bytes out;
  out.reserve(header.size() + payload.size());
  out.insert(out.end(), header.begin(), header.end());
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

LooseHeader parse_loose_header(ByteView data) {
  std::string_view s = as_chars(data);
  std::size_t space = s.find(' ');
  std::size_t nul = s.find('\0');
  if (space == std::string_view::npos || nul == std::string_view::npos || space > nul) {
    throw Error("malformed object header");
  }
  auto type = parse_object_type(s.substr(0, space));
  if (!type) throw Error("invalid object type '" + std::string(s.substr(0, space)) + "'");

  std::string_view digits = s.substr(space + 1, nul - space - 1);
  if (digits.empty() || (digits.size() > 1 && digits[0] == '0') ||
      !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    throw Error("malformed object size '" + std::string(digits) + "'");
  }
  std::size_t size = 0;
  for (char c : digits) {
    auto d = static_cast<std::size_t>(c - '0');
    if (size > (std::numeric_limits<std::size_t>::max() - d) / 10) {
      throw Error("object size overflows");
    }
    size = size * 10 + d;
  }
  return {*type, size, nul + 1};
}

RawObject decode_loose(ByteView decompressed) {
  LooseHeader h = parse_loose_header(decompressed);
  if (decompressed.size() - h.header_len != h.size) {
    throw Error("object size mismatch: header says " + std::to_string(h.size) + ", payload has " +
                std::to_string(decompressed.size() - h.header_len));
  }
  auto payload = decompressed.subspan(h.header_len);
  return {h.type, Bytes(payload.begin(), payload.end())};
}

ObjectId hash_object(ObjectType type, ByteView payload) {
  Sha1 s;
  s.update(as_bytes(header_for(type, payload.size())));
  s.update(payload);
  return ObjectId{s.finish()};
}

}  // namespace tva::core
