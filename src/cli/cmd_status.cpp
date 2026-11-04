#include <CLI/CLI.hpp>
#include <iostream>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/snapshot.h"
#include "core/tree_diff.h"

namespace tva::cli {

void register_status(CLI::App& app) {
  auto* cmd = app.add_subcommand("status", "List paths that differ from HEAD (A/D/M/T)");
  cmd->callback([] {
    auto repo = open_repo();
    // A snapshot is the only way to see the worktree; its objects are
    // content-addressed, so writing them is harmless.
    const core::ObjectId current = core::snapshot_worktree(repo);
    const auto changes = core::diff_trees(repo.odb(), head_tree(repo), current);
    if (changes.empty()) {
      std::cout << "clean\n";
      return;
    }
    for (const auto& c : changes) {
      char letter = 'M';
      switch (c.kind) {
        case core::ChangeKind::Added: letter = 'A'; break;
        case core::ChangeKind::Deleted: letter = 'D'; break;
        case core::ChangeKind::Modified: letter = 'M'; break;
        case core::ChangeKind::ModeChanged: letter = 'T'; break;
      }
      std::cout << letter << ' ' << c.path << '\n';
    }
  });
}

}  // namespace tva::cli
