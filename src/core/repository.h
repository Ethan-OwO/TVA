#pragma once

#include <filesystem>

#include "core/object_database.h"
#include "core/refs.h"

namespace tva::core {

// A worktree plus its .git/ (objects, refs, config) and .tva/ (TVA-private
// local state, excluded from snapshots and listed in .git/info/exclude).
class Repository {
 public:
  // Creates .git/ (HEAD -> refs/heads/main, objects/, refs/heads/,
  // refs/tags/, config, info/exclude) and .tva/. Throws if .git exists.
  static Repository init(const std::filesystem::path& worktree_root);
  // Walks up from `start` to the first directory containing a .git/ dir.
  static Repository discover(const std::filesystem::path& start);

  const std::filesystem::path& worktree_root() const { return worktree_root_; }
  const std::filesystem::path& git_dir() const { return git_dir_; }
  const std::filesystem::path& tva_dir() const { return tva_dir_; }

  ObjectDatabase& odb() { return odb_; }
  const ObjectDatabase& odb() const { return odb_; }
  RefStore& refs() { return refs_; }
  const RefStore& refs() const { return refs_; }

 private:
  explicit Repository(std::filesystem::path worktree_root);

  std::filesystem::path worktree_root_;
  std::filesystem::path git_dir_;
  std::filesystem::path tva_dir_;
  ObjectDatabase odb_;
  RefStore refs_;
};

}  // namespace tva::core
