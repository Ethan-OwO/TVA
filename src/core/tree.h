#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.h"
#include "core/object_id.h"

namespace tva::core {

enum class FileMode : std::uint32_t {
  Regular = 0100644,
  Executable = 0100755,
  Symlink = 0120000,
  Directory = 040000,
};

inline bool is_directory(FileMode m) { return m == FileMode::Directory; }
// Octal without leading zeros, as stored in trees: "100644", "40000".
std::string mode_string(FileMode m);

struct TreeEntry {
  FileMode mode;
  std::string name;
  ObjectId id;

  bool operator==(const TreeEntry&) const = default;
};

struct Tree {
  std::vector<TreeEntry> entries;

  bool operator==(const Tree&) const = default;
};

// git's tree order: compare names bytewise, but a directory's name compares
// as if it ended in '/'. So "a-b" < "a.txt" < "a" (dir), because
// '-' (0x2D) < '.' (0x2E) < '/' (0x2F).
bool tree_entry_less(const TreeEntry& a, const TreeEntry& b);

// Non-empty, no '/' or NUL, not "." or "..".
bool is_valid_entry_name(std::string_view name);

// Sorts entries into tree order and encodes each one as
// "<mode> <name>\0<20 raw id bytes>". Throws on invalid or duplicate names.
Bytes serialize_tree(const Tree& tree);

// Rejects malformed entries and invalid names (a tree from elsewhere must not
// be able to name "../x"). Like git, non-canonical regular-file modes such as
// 100664 are read as 100644; submodules (160000) are not supported.
Tree parse_tree(ByteView payload);

}  // namespace tva::core
