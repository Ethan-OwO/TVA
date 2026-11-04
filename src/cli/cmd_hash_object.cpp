#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/error.h"
#include "core/object.h"
#include "platform/fs.h"

namespace tva::cli {

void register_hash_object(CLI::App& app) {
  struct Options {
    bool write = false;
    std::string type = "blob";
    std::string file;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = app.add_subcommand("hash-object", "Compute an object id, optionally storing it");
  cmd->add_flag("-w", opt->write, "Write the object into the object database");
  cmd->add_option("-t", opt->type, "Object type (blob, tree, commit)");
  cmd->add_option("file", opt->file, "File to hash")->required();
  cmd->callback([opt] {
    auto type = core::parse_object_type(opt->type);
    if (!type) throw core::Error("invalid object type \"" + opt->type + "\"");
    core::Bytes data = platform::read_file(opt->file);
    core::ObjectId id = opt->write ? open_repo().odb().write(*type, data)
                                   : core::hash_object(*type, data);
    std::cout << id.hex() << '\n';
  });
}

}  // namespace tva::cli
