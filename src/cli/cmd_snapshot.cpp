#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>
#include <optional>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/object.h"
#include "core/snapshot.h"

namespace tva::cli {

void register_snapshot(CLI::App& app) {
  auto message = std::make_shared<std::string>();
  auto* cmd = app.add_subcommand(
      "snapshot", "Commit the whole worktree on the current branch (testing porcelain)");
  cmd->add_option("-m", *message, "Commit message")->required();
  cmd->callback([message] {
    auto repo = open_repo();
    const core::ObjectId tree = core::snapshot_worktree(repo);

    // HEAD normally points at a branch; a detached HEAD is updated itself.
    const std::optional<std::string> branch = repo.refs().symbolic_target("HEAD");
    const std::string ref = branch.value_or("HEAD");
    const std::optional<core::ObjectId> parent = repo.refs().resolve(ref);
    const core::ObjectId parent_tree =
        parent ? repo.odb().read_commit(*parent).tree : core::hash_object(core::ObjectType::Tree, {});
    if (parent_tree == tree) {
      std::cout << "nothing changed\n";
      return;
    }

    core::Commit commit;
    commit.tree = tree;
    if (parent) commit.parents.push_back(*parent);
    commit.author = repo.current_signature();
    commit.committer = repo.current_committer();
    commit.message = *message;
    const core::ObjectId id = repo.odb().write_commit(commit);
    repo.refs().update(ref, id, parent);  // CAS: fails if someone moved the branch meanwhile

    std::string label = "detached HEAD";
    if (branch) label = branch->starts_with("refs/heads/") ? branch->substr(11) : *branch;
    std::cout << '[' << label << ' ' << short_id(id) << "] " << core::commit_subject(*message)
              << '\n';
  });
}

}  // namespace tva::cli
