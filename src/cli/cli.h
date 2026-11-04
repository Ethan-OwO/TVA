#pragma once

namespace tva::cli {

// Parses argv, runs the selected subcommand and returns the process exit
// code: 0 on success, 128 for tva errors (printed as "fatal: <msg>"), 129 for
// usage errors -- the same convention git uses.
int run(int argc, char** argv);

}  // namespace tva::cli
