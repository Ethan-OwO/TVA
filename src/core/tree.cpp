#include "core/tree.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <utility>

#include "core/error.h"

namespace tva::core {

std::string mode_string(FileMode m) {
  switch (m) {
    case FileMode::Regular:
      return "100644";
    case FileMode::Executable:
      return "100755";
    case FileMode::Symlink:
      return "120000";
    case FileMode::Directory:
      return "40000";
  }
  throw Error("invalid file mode");
}

bool tree_entry_less(const TreeEntry& a, const TreeEntry& b) {
  // Same as git's base_name_compare(): memcmp the common prefix, then compare
  // the next byte, where the end of a directory name counts as '/'.
  const std::size_t n = std::min(a.name.size(), b.name.size());
  if (int c = std::memcmp(a.name.data(), b.name.data(), n); c != 0) return c < 0;
  auto next = [n](const TreeEntry& e) -> unsigned {
    if (n < e.name.size()) return static_cast<unsigned char>(e.name[n]);
    return is_directory(e.mode) ? '/' : 0;
  };
  return next(a) < next(b);
}

bool is_valid_entry_name(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find('/') == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

Bytes serialize_tree(const Tree& tree) {
  std::vector<TreeEntry> entries = tree.entries;
  std::unordered_set<std::string_view> seen;
  for (const auto& e : entries) {
    if (!is_valid_entry_name(e.name)) throw Error("invalid tree entry name '" + e.name + "'");
    // A file and a directory with the same name are duplicates too, even
    // though they do not sort next to each other.
    if (!seen.insert(e.name).second) throw Error("duplicate tree entry '" + e.name + "'");
  }
  std::sort(entries.begin(), entries.end(), tree_entry_less);

  Bytes out;
  for (const auto& e : entries) {
    std::string head = mode_string(e.mode) + ' ' + e.name;
    out.insert(out.end(), head.begin(), head.end());
    out.push_back(0);
    out.insert(out.end(), e.id.bytes.begin(), e.id.bytes.end());
  }
  return out;
}

namespace {

FileMode canonical_mode(std::uint32_t mode) {
  switch (mode & 0170000) {
    case 0100000:
      return (mode & 0100) ? FileMode::Executable : FileMode::Regular;
    case 0120000:
      return FileMode::Symlink;
    case 0040000:
      return FileMode::Directory;
    default:
      throw Error("unsupported file mode " + std::to_string(mode) + " in tree");
  }
}

}  // namespace

Tree parse_tree(ByteView payload) {
  std::string_view s = as_chars(payload);
  Tree tree;
  std::size_t pos = 0;
  while (pos < s.size()) {
    std::size_t space = s.find(' ', pos);
    if (space == std::string_view::npos || space == pos || space - pos > 7) {
      throw Error("malformed tree entry");
    }
    std::uint32_t mode = 0;
    for (std::size_t i = pos; i < space; ++i) {
      if (s[i] < '0' || s[i] > '7') throw Error("malformed tree entry mode");
      mode = mode * 8 + static_cast<std::uint32_t>(s[i] - '0');
    }
    std::size_t nul = s.find('\0', space + 1);
    if (nul == std::string_view::npos || s.size() - nul - 1 < 20) {
      throw Error("truncated tree entry");
    }
    std::string name(s.substr(space + 1, nul - space - 1));
    if (!is_valid_entry_name(name)) throw Error("invalid tree entry name '" + name + "'");
    ObjectId id;
    std::memcpy(id.bytes.data(), s.data() + nul + 1, 20);
    tree.entries.push_back({canonical_mode(mode), std::move(name), id});
    pos = nul + 21;
  }
  return tree;
}

}  // namespace tva::core
