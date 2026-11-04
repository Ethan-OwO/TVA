#include <CLI/CLI.hpp>
#include <iostream>
#include <memory>
#include <optional>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/error.h"
#include "core/rev_parse.h"

namespace tva::cli {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// git's "medium" format: headers, a blank line, the message indented by four
// spaces with trailing whitespace stripped, leading blank lines dropped and
// trailing blank lines trimmed.
std::string format_medium(const core::ObjectId& id, const core::Commit& c) {
  std::string out = "commit " + id.hex() + "\n";
  out += "Author: " + c.author.name + " <" + c.author.email + ">\n";
  out += "Date:   " + core::format_git_date(c.author.when_unix, c.author.tz_offset_minutes) + "\n";
  out += "\n";
  std::string_view msg = c.message;
  bool first = true;
  while (!msg.empty()) {
    std::size_t nl = msg.find('\n');
    std::string_view line = msg.substr(0, nl);
    msg.remove_prefix(nl == std::string_view::npos ? msg.size() : nl + 1);
    while (!line.empty() && is_space(line.back())) line.remove_suffix(1);
    if (line.empty() && first) continue;
    first = false;
    out += "    ";
    out += line;
    out += '\n';
  }
  while (!out.empty() && is_space(out.back())) out.pop_back();
  out += '\n';
  return out;
}

}  // namespace

void register_log(CLI::App& app) {
  struct Options {
    std::string rev;
    bool oneline = false;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = app.add_subcommand("log", "Show the first-parent history of a revision");
  cmd->add_option("rev", opt->rev, "Revision to start from (default HEAD)");
  cmd->add_flag("--oneline", opt->oneline, "One line per commit: <short id> <subject>");
  cmd->callback([opt] {
    auto repo = open_repo();
    std::optional<core::ObjectId> id;
    if (opt->rev.empty()) {
      id = repo.refs().resolve("HEAD");
      if (!id) {
        std::string branch = repo.refs().symbolic_target("HEAD").value_or("HEAD");
        if (branch.starts_with("refs/heads/")) branch = branch.substr(11);
        throw core::Error("your current branch '" + branch + "' does not have any commits yet");
      }
    } else {
      id = core::rev_parse(repo, opt->rev);
    }
    for (bool first = true; id; first = false) {
      core::Commit c = repo.odb().read_commit(*id);
      if (opt->oneline) {
        std::cout << short_id(*id) << ' ' << core::commit_subject(c.message) << '\n';
      } else {
        if (!first) std::cout << '\n';
        std::cout << format_medium(*id, c);
      }
      id = c.parents.empty() ? std::nullopt : std::optional(c.parents.front());
    }
  });
}

}  // namespace tva::cli
