#pragma once

#include <filesystem>

#include "core/commit.h"
#include "core/config.h"
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
  // Global git config (XDG then ~/.gitconfig), then <git_dir>/config.
  const Config& config() const { return config_; }

  // Author identity: GIT_AUTHOR_NAME / GIT_AUTHOR_EMAIL / GIT_AUTHOR_DATE,
  // falling back to user.name / user.email and the current local time.
  // GIT_*_DATE must look like "<unix seconds> <+HHMM>" (optionally "@<secs>").
  Signature current_signature() const;
  // Committer identity from GIT_COMMITTER_*; each unset part equals the
  // author's.
  Signature current_committer() const;

 private:
  explicit Repository(std::filesystem::path worktree_root);

  std::filesystem::path worktree_root_;
  std::filesystem::path git_dir_;
  std::filesystem::path tva_dir_;
  ObjectDatabase odb_;
  RefStore refs_;
  Config config_;
};

}  // namespace tva::core
