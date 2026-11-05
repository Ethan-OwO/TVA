#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "core/bytes.h"
#include "core/object_id.h"

namespace tva::core {

enum class ObjectType { Blob, Tree, Commit };

std::string_view to_string(ObjectType type);
// nullopt for unknown names. "tag" is a real git type that M1 does not
// support, so it throws Error("unsupported object type: tag") instead.
std::optional<ObjectType> parse_object_type(std::string_view name);

// A loose object before compression: "<type> <decimal size>\0<payload>".
Bytes encode_loose(ObjectType type, ByteView payload);

struct LooseHeader {
  ObjectType type;
  std::size_t size;        // payload size declared in the header
  std::size_t header_len;  // bytes up to and including the NUL
};
// Parses the header at the start of `data` (which may be just a prefix of the
// object). Size must be canonical decimal, like git requires ("010" is bad).
LooseHeader parse_loose_header(ByteView data);

struct RawObject {
  ObjectType type;
  Bytes payload;
};
// Validates the header and that the payload length matches it.
RawObject decode_loose(ByteView decompressed);

// sha1(encode_loose(type, payload)), computed without building the copy.
ObjectId hash_object(ObjectType type, ByteView payload);

}  // namespace tva::core
