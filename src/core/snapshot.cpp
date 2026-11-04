#include "core/snapshot.h"

#include <optional>
#include <string>
#include <system_error>

#include "core/error.h"
#include "core/ignore.h"
#include "core/tree.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

namespace {

class Snapshotter {
 public:
  Snapshotter(ObjectDatabase& odb, const IgnoreMatcher& ignore) : odb_(odb), ignore_(ignore) {}

  // nullopt when nothing under `abs` is tracked (except for the root).
  std::optional<ObjectId> snapshot_dir(const fs::path& abs, const std::string& rel) {
    Tree tree;
    std::error_code ec;
    fs::directory_iterator it(abs, ec);
    if (ec) throw Error("cannot read directory '" + abs.string() + "': " + ec.message());
    for (const fs::directory_entry& entry : it) {
      const std::string name = entry.path().filename().string();
      const std::string child = rel.empty() ? name : rel + "/" + name;
      const fs::file_status st = entry.symlink_status();
      if (fs::is_symlink(st)) {
        if (ignore_.is_ignored(child, false)) continue;
        std::string target = platform::read_symlink(entry.path());
        tree.entries.push_back({FileMode::Symlink, name, odb_.write_blob(as_bytes(target))});
      } else if (fs::is_directory(st)) {
        if (ignore_.is_ignored(child, true)) continue;
        if (auto sub = snapshot_dir(entry.path(), child)) {
          tree.entries.push_back({FileMode::Directory, name, *sub});
        }
      } else if (fs::is_regular_file(st)) {
        if (ignore_.is_ignored(child, false)) continue;
        FileMode mode =
            platform::is_executable(entry.path()) ? FileMode::Executable : FileMode::Regular;
        tree.entries.push_back({mode, name, odb_.write_blob(platform::read_file(entry.path()))});
      }
    }
    if (tree.entries.empty() && !rel.empty()) return std::nullopt;
    return odb_.write_tree(tree);
  }

 private:
  ObjectDatabase& odb_;
  const IgnoreMatcher& ignore_;
};

}  // namespace

ObjectId snapshot_worktree(Repository& repo) {
  IgnoreMatcher ignore = IgnoreMatcher::load(repo.worktree_root());
  Snapshotter s(repo.odb(), ignore);
  return *s.snapshot_dir(repo.worktree_root(), "");
}

}  // namespace tva::core
