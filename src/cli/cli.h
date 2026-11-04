#pragma once

namespace CLI {
class App;
}

namespace tva::cli {

// Parses argv, runs the selected subcommand and returns the process exit
// code: 0 on success, 128 for tva errors (printed as "fatal: <msg>"), 129 for
// usage errors -- the same convention git uses.
int run(int argc, char** argv);

// One per cmd_<name>.cpp; each adds its subcommand and callback to `app`.
void register_init(CLI::App& app);
void register_hash_object(CLI::App& app);
void register_cat_file(CLI::App& app);
void register_write_tree(CLI::App& app);
void register_commit_tree(CLI::App& app);
void register_snapshot(CLI::App& app);
void register_rev_parse(CLI::App& app);
void register_log(CLI::App& app);
void register_status(CLI::App& app);
void register_restore(CLI::App& app);

}  // namespace tva::cli
