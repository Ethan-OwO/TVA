#include "core/tree_diff.h"

#include <algorithm>

namespace tva::core {
namespace {

class Differ {
 public:
  Differ(const TreeReader& read, std::vector<PathChange>& out) : read_(read), out_(out) {}

  void diff(const std::optional<ObjectId>& old_id, const std::optional<ObjectId>& new_id,
            const std::string& prefix) {
    if (old_id == new_id) return;
    std::vector<TreeEntry> olds = entries(old_id);
    std::vector<TreeEntry> news = entries(new_id);
    std::size_t i = 0, j = 0;
    while (i < olds.size() || j < news.size()) {
      // tree_entry_less never calls a file and a directory of the same name
      // equal, so such a pair lands in the two one-sided branches below.
      if (j == news.size() || (i < olds.size() && tree_entry_less(olds[i], news[j]))) {
        one_side(olds[i++], prefix, ChangeKind::Deleted);
      } else if (i == olds.size() || tree_entry_less(news[j], olds[i])) {
        one_side(news[j++], prefix, ChangeKind::Added);
      } else {
        const TreeEntry& o = olds[i++];
        const TreeEntry& n = news[j++];
        if (is_directory(o.mode)) {
          diff(o.id, n.id, prefix + o.name + "/");  // returns at once if ids match
        } else if (o.id != n.id) {
          out_.push_back({ChangeKind::Modified, prefix + o.name, o, n});
        } else if (o.mode != n.mode) {
          out_.push_back({ChangeKind::ModeChanged, prefix + o.name, o, n});
        }
      }
    }
  }

 private:
  std::vector<TreeEntry> entries(const std::optional<ObjectId>& id) {
    if (!id) return {};
    std::vector<TreeEntry> e = read_(*id).entries;
    std::sort(e.begin(), e.end(), tree_entry_less);  // trees from disk are sorted; be safe
    return e;
  }

  // Everything under an entry present on only one side.
  void one_side(const TreeEntry& e, const std::string& prefix, ChangeKind kind) {
    if (!is_directory(e.mode)) {
      if (kind == ChangeKind::Added) {
        out_.push_back({kind, prefix + e.name, std::nullopt, e});
      } else {
        out_.push_back({kind, prefix + e.name, e, std::nullopt});
      }
      return;
    }
    for (const TreeEntry& child : entries(e.id)) one_side(child, prefix + e.name + "/", kind);
  }

  const TreeReader& read_;
  std::vector<PathChange>& out_;
};

}  // namespace

std::vector<PathChange> diff_trees(const TreeReader& read_tree, std::optional<ObjectId> old_tree,
                                   std::optional<ObjectId> new_tree) {
  // Emitting in tree order yields byte-sorted paths: inside a directory every
  // path shares the "<dir>/" prefix, which is exactly the key tree order uses.
  std::vector<PathChange> out;
  Differ(read_tree, out).diff(old_tree, new_tree, "");
  return out;
}

std::vector<PathChange> diff_trees(const ObjectDatabase& odb, std::optional<ObjectId> old_tree,
                                   std::optional<ObjectId> new_tree) {
  return diff_trees([&odb](const ObjectId& id) { return odb.read_tree(id); }, old_tree, new_tree);
}

}  // namespace tva::core
