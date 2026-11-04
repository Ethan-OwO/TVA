#include <CLI/CLI.hpp>
#include <iostream>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/snapshot.h"

namespace tva::cli {

void register_write_tree(CLI::App& app) {
  auto* cmd = app.add_subcommand("write-tree", "Snapshot the worktree and print its tree id");
  cmd->callback([] {
    auto repo = open_repo();
    std::cout << core::snapshot_worktree(repo).hex() << '\n';
  });
}

}  // namespace tva::cli
