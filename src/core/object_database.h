#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/bytes.h"
#include "core/object.h"
#include "core/object_id.h"
#include "core/tree.h"

namespace tva::core {

// Loose-object store: objects/<first 2 hex>/<remaining 38 hex>, each file the
// zlib-compressed "<type> <size>\0<payload>".
class ObjectDatabase {
 public:
  explicit ObjectDatabase(std::filesystem::path objects_dir);

  const std::filesystem::path& objects_dir() const { return objects_dir_; }
  std::filesystem::path object_path(const ObjectId& id) const;

  // Content-addressed, so writing an existing object is a no-op. New objects
  // are written atomically (temp file + rename) with mode 0444.
  ObjectId write(ObjectType type, ByteView payload);
  // Re-hashes what it read; a mismatch throws Error("corrupt object <hex>").
  std::optional<RawObject> read(const ObjectId& id) const;
  // Decompresses only the header: {type, payload size}.
  std::optional<std::pair<ObjectType, std::size_t>> read_header(const ObjectId& id) const;
  bool exists(const ObjectId& id) const;
  // All ids starting with `hex_prefix` (4..40 hex digits), sorted.
  std::vector<ObjectId> resolve_prefix(std::string_view hex_prefix) const;

  ObjectId write_blob(ByteView data) { return write(ObjectType::Blob, data); }
  ObjectId write_tree(const Tree& tree) { return write(ObjectType::Tree, serialize_tree(tree)); }
  // Typed reads throw if the object is missing or has another type.
  Bytes read_blob(const ObjectId& id) const;
  Tree read_tree(const ObjectId& id) const;

 private:
  RawObject read_typed(const ObjectId& id, ObjectType expected) const;

  std::filesystem::path objects_dir_;
};

}  // namespace tva::core
