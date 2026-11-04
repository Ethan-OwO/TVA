#include "core/object_database.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "core/error.h"
#include "core/zlib_codec.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

ObjectDatabase::ObjectDatabase(fs::path objects_dir) : objects_dir_(std::move(objects_dir)) {}

fs::path ObjectDatabase::object_path(const ObjectId& id) const {
  std::string hex = id.hex();
  return objects_dir_ / hex.substr(0, 2) / hex.substr(2);
}

ObjectId ObjectDatabase::write(ObjectType type, ByteView payload) {
  ObjectId id = hash_object(type, payload);
  if (exists(id)) return id;
  fs::path path = object_path(id);
  fs::create_directories(path.parent_path());
  platform::write_file_atomic(path, zlib_deflate(encode_loose(type, payload)),
                              fs::perms(0444));
  return id;
}

std::optional<RawObject> ObjectDatabase::read(const ObjectId& id) const {
  if (!exists(id)) return std::nullopt;
  Bytes compressed = platform::read_file(object_path(id));
  RawObject obj;
  try {
    obj = decode_loose(zlib_inflate(compressed));
  } catch (const Error& e) {
    throw Error("corrupt object " + id.hex() + ": " + e.what());
  }
  if (hash_object(obj.type, obj.payload) != id) throw Error("corrupt object " + id.hex());
  return obj;
}

std::optional<std::pair<ObjectType, std::size_t>> ObjectDatabase::read_header(
    const ObjectId& id) const {
  if (!exists(id)) return std::nullopt;
  Bytes compressed = platform::read_file(object_path(id));
  try {
    // "commit 18446744073709551615\0" is 28 bytes; 64 is plenty.
    auto [prefix, consumed] = zlib_inflate_prefix(compressed, 64);
    LooseHeader h = parse_loose_header(prefix);
    return std::pair{h.type, h.size};
  } catch (const Error& e) {
    throw Error("corrupt object " + id.hex() + ": " + e.what());
  }
}

bool ObjectDatabase::exists(const ObjectId& id) const {
  std::error_code ec;
  return fs::is_regular_file(object_path(id), ec);
}

std::vector<ObjectId> ObjectDatabase::resolve_prefix(std::string_view hex_prefix) const {
  if (hex_prefix.size() < 4 || hex_prefix.size() > 40 || !is_hex(hex_prefix)) {
    throw Error("invalid object id prefix '" + std::string(hex_prefix) + "'");
  }
  std::string prefix(hex_prefix);
  std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::vector<ObjectId> out;
  fs::path dir = objects_dir_ / prefix.substr(0, 2);
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return out;
  const std::string rest = prefix.substr(2);
  for (const auto& entry : fs::directory_iterator(dir)) {
    std::string name = entry.path().filename().string();
    if (name.size() != 38 || name.compare(0, rest.size(), rest) != 0) continue;
    if (auto id = ObjectId::from_hex(prefix.substr(0, 2) + name)) out.push_back(*id);
  }
  std::sort(out.begin(), out.end());
  return out;
}

RawObject ObjectDatabase::read_typed(const ObjectId& id, ObjectType expected) const {
  auto obj = read(id);
  if (!obj) throw Error("object " + id.hex() + " not found");
  if (obj->type != expected) {
    throw Error("object " + id.hex() + " is a " + std::string(to_string(obj->type)) + ", not a " +
                std::string(to_string(expected)));
  }
  return std::move(*obj);
}

Bytes ObjectDatabase::read_blob(const ObjectId& id) const {
  return read_typed(id, ObjectType::Blob).payload;
}

Tree ObjectDatabase::read_tree(const ObjectId& id) const {
  return parse_tree(read_typed(id, ObjectType::Tree).payload);
}

Commit ObjectDatabase::read_commit(const ObjectId& id) const {
  return parse_commit(read_typed(id, ObjectType::Commit).payload);
}

}  // namespace tva::core
