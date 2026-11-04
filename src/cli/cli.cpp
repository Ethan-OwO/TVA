#include "cli/cli.h"

#include <CLI/CLI.hpp>

namespace tva::cli {

int run(int argc, char** argv) {
  CLI::App app{"TVA: version control for the AI-agent era", "tva"};
  app.set_version_flag("--version", "tva " TVA_VERSION);
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e) == 0 ? 0 : 129;
  }
  return 0;
}

}  // namespace tva::cli
