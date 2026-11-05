#include <CLI/CLI.hpp>
#include <memory>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/error.h"
#include "core/materialize.h"
#include "core/object.h"
#include "core/rev_parse.h"
#include "core/snapshot.h"

namespace tva::cli {

void register_restore(CLI::App& app) {
  struct Options {
    std::string rev;
    bool force = false;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = app.add_subcommand("restore", "Make the worktree match a commit or tree (HEAD stays)");
  cmd->add_option("rev", opt->rev, "Commit or tree to restore")->required();
  cmd->add_flag("--force", opt->force, "Discard changes that were not snapshotted");
  cmd->callback([opt] {
    auto repo = open_repo();
    const core::ObjectId target = core::rev_parse(repo, opt->rev);
    auto header = repo.odb().read_header(target);
    if (!header) throw core::Error("not a valid object name " + opt->rev);
    core::ObjectId target_tree = target;
    if (header->first == core::ObjectType::Commit) {
      target_tree = repo.odb().read_commit(target).tree;
    } else if (header->first != core::ObjectType::Tree) {
      throw core::Error(opt->rev + " is neither a commit nor a tree");
    }

    // The snapshot doubles as a safety net: with --force, the discarded
    // content is still in the object database.
    const core::ObjectId current = core::snapshot_worktree(repo);
    const core::ObjectId head = head_tree(repo).value_or(core::empty_tree_id());
    if (current != head && !opt->force) {
      throw core::Error("worktree has unsnapshotted changes; run tva snapshot or use --force");
    }
    core::materialize_tree(repo, target_tree, current);
  });
}

}  // namespace tva::cli
