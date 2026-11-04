#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/error.h"
#include "core/rev_parse.h"

namespace tva::cli {

void register_cat_file(CLI::App& app) {
  struct Options {
    bool type = false;
    bool size = false;
    bool pretty = false;
    std::string rev;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = app.add_subcommand("cat-file", "Show an object's type, size or content");
  auto* mode = cmd->add_option_group("mode", "exactly one of -t, -s, -p");
  mode->add_flag("-t", opt->type, "Show the object type");
  mode->add_flag("-s", opt->size, "Show the payload size");
  mode->add_flag("-p", opt->pretty, "Show the content");
  mode->require_option(1);
  cmd->add_option("rev", opt->rev, "Object to show")->required();
  cmd->callback([opt] {
    auto repo = open_repo();
    core::ObjectId id = core::rev_parse(repo, opt->rev);
    if (opt->type || opt->size) {
      auto header = repo.odb().read_header(id);
      if (!header) throw core::Error("not a valid object name " + opt->rev);
      if (opt->type) {
        std::cout << core::to_string(header->first) << '\n';
      } else {
        std::cout << header->second << '\n';
      }
      return;
    }
    auto obj = repo.odb().read(id);
    if (!obj) throw core::Error("not a valid object name " + opt->rev);
    write_stdout(obj->payload);
  });
}

}  // namespace tva::cli
