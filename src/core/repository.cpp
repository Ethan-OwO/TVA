#include "core/repository.h"

#include <string>

#include "core/bytes.h"
#include "core/error.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

Repository::Repository(fs::path worktree_root)
    : worktree_root_(std::move(worktree_root)),
      git_dir_(worktree_root_ / ".git"),
      tva_dir_(worktree_root_ / ".tva"),
      odb_(git_dir_ / "objects"),
      refs_(git_dir_) {}

Repository Repository::init(const fs::path& worktree_root) {
  fs::create_directories(worktree_root);
  fs::path root = fs::canonical(worktree_root);
  fs::path git = root / ".git";
  std::error_code ec;
  if (fs::exists(fs::symlink_status(git, ec))) {
    throw Error("'" + git.string() + "' already exists");
  }
  for (const char* dir : {"objects", "refs/heads", "refs/tags", "info"}) {
    fs::create_directories(git / dir);
  }
  platform::write_file_atomic(git / "HEAD", as_bytes("ref: refs/heads/main\n"));
  platform::write_file_atomic(git / "config", as_bytes("[core]\n"
                                                       "\trepositoryformatversion = 0\n"
                                                       "\tfilemode = true\n"
                                                       "\tbare = false\n"));
  platform::write_file_atomic(git / "info" / "exclude", as_bytes(".tva/\n"));
  fs::create_directories(root / ".tva");
  return Repository(root);
}

Repository Repository::discover(const fs::path& start) {
  std::error_code ec;
  fs::path dir = fs::weakly_canonical(fs::absolute(start), ec);
  if (ec) dir = fs::absolute(start);
  for (;;) {
    if (fs::is_directory(dir / ".git", ec)) return Repository(dir);
    fs::path parent = dir.parent_path();
    if (parent == dir) break;
    dir = parent;
  }
  throw Error("not a tva repository (or any of the parent directories)");
}

}  // namespace tva::core
