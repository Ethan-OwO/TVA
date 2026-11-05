#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/rev_parse.h"

namespace tva::cli {

void register_rev_parse(CLI::App& app) {
  auto rev = std::make_shared<std::string>();
  auto* cmd = app.add_subcommand("rev-parse", "Print the full object id of a revision");
  cmd->add_option("rev", *rev, "Revision, e.g. HEAD~2, main, a1b2c3d")->required();
  cmd->callback([rev] { std::cout << core::rev_parse(open_repo(), *rev).hex() << '\n'; });
}

}  // namespace tva::cli
