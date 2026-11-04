#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>

#include "cli/cli.h"
#include "core/repository.h"

namespace tva::cli {

void register_init(CLI::App& app) {
  auto* cmd = app.add_subcommand("init", "Create an empty TVA repository");
  auto dir = std::make_shared<std::string>(".");
  cmd->add_option("dir", *dir, "Directory to initialize (created if missing)");
  cmd->callback([dir] {
    auto repo = core::Repository::init(*dir);
    std::cout << "Initialized empty TVA repository in " << repo.git_dir().string() << "/\n";
  });
}

}  // namespace tva::cli
