#include "core/materialize.h"

#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "core/error.h"
#include "core/ignore.h"
#include "core/tree_diff.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

namespace {

bool is_file_mode(FileMode m) { return m == FileMode::Regular || m == FileMode::Executable; }

fs::file_status status_of(const fs::path& p) {
  std::error_code ec;
  return fs::symlink_status(p, ec);  // never follows symlinks
}

// Removes a directory tree that holds nothing but (empty) directories.
// Returns false, touching nothing further, as soon as it meets anything else.
bool remove_empty_dir_tree(const fs::path& dir) {
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!fs::is_directory(entry.symlink_status()) || !remove_empty_dir_tree(entry.path())) {
      return false;
    }
  }
  fs::remove(dir);
  return true;
}

class Materializer {
 public:
  explicit Materializer(Repository& repo) : repo_(repo), root_(repo.worktree_root()) {}

  void run(const ObjectId& target_tree, const ObjectId& current_tree) {
    std::vector<PathChange> changes = diff_trees(repo_.odb(), current_tree, target_tree);
    IgnoreMatcher ignore = IgnoreMatcher::load(root_);
    std::vector<const PathChange*> deletions, writes;
    for (const PathChange& c : changes) {
      if (ignore.is_ignored(c.path, false)) continue;
      if (c.kind == ChangeKind::Deleted) {
        deletions.push_back(&c);
        deleted_.insert(c.path);
      } else {
        writes.push_back(&c);
      }
    }
    for (const PathChange* c : writes) check_writable(c->path);

    // Deepest paths first, so emptied directories can be pruned bottom-up.
    for (auto it = deletions.rbegin(); it != deletions.rend(); ++it) {
      remove_file((*it)->path);
      prune_empty_parents((*it)->path);
    }
    // Sorted by path, so parents are created before their children.
    for (const PathChange* c : writes) write(*c);
  }

 private:
  // Anything occupying the path or one of its parents must be tracked
  // content that is about to be deleted, or an empty directory.
  void check_writable(const std::string& path) {
    for (std::size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
      const std::string parent = path.substr(0, slash);
      fs::file_status st = status_of(root_ / parent);
      if (fs::exists(st) && !fs::is_directory(st) && !deleted_.count(parent)) {
        throw Error("cannot restore '" + path + "': '" + parent + "' is in the way");
      }
    }
    if (!fs::is_directory(status_of(root_ / path))) return;
    for (auto it = fs::recursive_directory_iterator(root_ / path); it != fs::recursive_directory_iterator(); ++it) {
      if (fs::is_directory(it->symlink_status())) continue;
      std::string rel = it->path().lexically_relative(root_).generic_string();
      if (!deleted_.count(rel)) {
        throw Error("cannot restore '" + path + "': directory contains '" + rel + "'");
      }
    }
  }

  void remove_file(const std::string& path) {
    fs::path abs = root_ / path;
    fs::file_status st = status_of(abs);
    if (!fs::exists(st)) return;
    if (fs::is_directory(st)) throw Error("cannot remove '" + path + "': it is a directory");
    fs::remove(abs);
  }

  void prune_empty_parents(std::string path) {
    for (std::size_t slash = path.rfind('/'); slash != std::string::npos; slash = path.rfind('/')) {
      path.resize(slash);
      fs::path dir = root_ / path;
      std::error_code ec;
      if (!fs::is_directory(status_of(dir)) || !fs::is_empty(dir, ec) || ec) return;
      fs::remove(dir);
    }
  }

  void ensure_parent_dirs(const std::string& path) {
    fs::path dir = root_;
    for (std::size_t start = 0, slash; (slash = path.find('/', start)) != std::string::npos;
         start = slash + 1) {
      dir /= path.substr(start, slash - start);
      fs::file_status st = status_of(dir);
      if (!fs::exists(st)) {
        fs::create_directory(dir);
      } else if (!fs::is_directory(st)) {
        throw Error("cannot create directory '" + dir.string() + "': a file is in the way");
      }
    }
  }

  void write(const PathChange& c) {
    const TreeEntry& entry = *c.new_entry;
    const fs::path abs = root_ / c.path;
    ensure_parent_dirs(c.path);

    if (c.kind == ChangeKind::ModeChanged && is_file_mode(c.old_entry->mode) &&
        is_file_mode(entry.mode)) {
      platform::set_executable(abs, entry.mode == FileMode::Executable);
      return;
    }
    fs::file_status st = status_of(abs);
    if (fs::is_directory(st) && !remove_empty_dir_tree(abs)) {
      throw Error("cannot restore '" + c.path + "': a non-empty directory is in the way");
    }
    const Bytes data = repo_.odb().read_blob(entry.id);
    if (entry.mode == FileMode::Symlink) {
      if (fs::exists(status_of(abs))) fs::remove(abs);
      platform::create_symlink(std::string(as_chars(data)), abs);
    } else {
      // rename() replaces a file or symlink at `abs` atomically.
      platform::write_file_atomic(abs, data,
                                  entry.mode == FileMode::Executable ? platform::kExecutablePerms
                                                                     : platform::kRegularPerms);
    }
  }

  Repository& repo_;
  const fs::path root_;
  std::unordered_set<std::string> deleted_;
};

}  // namespace

void materialize_tree(Repository& repo, const ObjectId& target_tree, const ObjectId& current_tree) {
  Materializer(repo).run(target_tree, current_tree);
}

}  // namespace tva::core
