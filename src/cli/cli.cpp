#include "cli/cli.h"

#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>

#include "core/error.h"

namespace tva::cli {

int run(int argc, char** argv) {
  std::ios::sync_with_stdio(false);

  CLI::App app{"TVA: version control for the AI-agent era", "tva"};
  app.set_version_flag("--version", "tva " TVA_VERSION);
  app.require_subcommand(1);

  register_init(app);
  register_hash_object(app);
  register_cat_file(app);
  register_write_tree(app);

  // Subcommand callbacks run inside parse(), so their exceptions land here.
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    // --help and --version are "errors" with exit code 0 in CLI11.
    return app.exit(e) == 0 ? 0 : 129;
  } catch (const core::Error& e) {
    std::cout.flush();
    std::cerr << "fatal: " << e.what() << '\n';
    return 128;
  } catch (const std::exception& e) {  // filesystem_error, bad_alloc, ...
    std::cout.flush();
    std::cerr << "fatal: " << e.what() << '\n';
    return 128;
  }
  std::cout.flush();
  return 0;
}

}  // namespace tva::cli
