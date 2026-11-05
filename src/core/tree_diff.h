#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/object_database.h"
#include "core/object_id.h"
#include "core/tree.h"

namespace tva::core {

enum class ChangeKind {
  Added,
  Deleted,
  Modified,     // content changed (the mode may have changed too)
  ModeChanged,  // same content, different mode (e.g. chmod +x)
};

struct PathChange {
  ChangeKind kind;
  std::string path;  // '/'-separated, relative to the tree root
  std::optional<TreeEntry> old_entry;
  std::optional<TreeEntry> new_entry;

  bool operator==(const PathChange&) const = default;
};

using TreeReader = std::function<Tree(const ObjectId&)>;

// File-level (non-directory) differences, sorted by path. Walks both trees in
// tree order at once (a merge join); subtrees with equal ids are skipped
// without being read, so the cost is proportional to what changed. A file
// turning into a directory (or back) is a Deleted plus Added(s). nullopt is
// the empty tree.
std::vector<PathChange> diff_trees(const TreeReader& read_tree, std::optional<ObjectId> old_tree,
                                   std::optional<ObjectId> new_tree);
std::vector<PathChange> diff_trees(const ObjectDatabase& odb, std::optional<ObjectId> old_tree,
                                   std::optional<ObjectId> new_tree);

}  // namespace tva::core
