#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/error.h"
#include "core/rev_parse.h"

namespace tva::cli {

void register_commit_tree(CLI::App& app) {
  struct Options {
    std::string tree;
    std::vector<std::string> parents;
    std::vector<std::string> messages;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = app.add_subcommand("commit-tree", "Create a commit object (no ref is updated)");
  cmd->add_option("tree", opt->tree, "Tree for the commit")->required();
  cmd->add_option("-p", opt->parents, "Parent commit (repeatable)")->allow_extra_args(false);
  cmd->add_option("-m", opt->messages, "Message paragraph (repeatable)")
      ->required()
      ->allow_extra_args(false);
  cmd->callback([opt] {
    auto repo = open_repo();
    auto require_type = [&](const std::string& spec, core::ObjectType type) {
      core::ObjectId id = core::rev_parse(repo, spec);
      auto header = repo.odb().read_header(id);
      if (!header || header->first != type) {
        throw core::Error(spec + " is not a valid '" + std::string(core::to_string(type)) +
                          "' object");
      }
      return id;
    };
    core::Commit commit;
    commit.tree = require_type(opt->tree, core::ObjectType::Tree);
    for (const auto& p : opt->parents) {
      commit.parents.push_back(require_type(p, core::ObjectType::Commit));
    }
    commit.message = join_message_paragraphs(opt->messages);
    commit.author = repo.current_signature();
    commit.committer = repo.current_committer();
    std::cout << repo.odb().write_commit(commit).hex() << '\n';
  });
}

}  // namespace tva::cli
