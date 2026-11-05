#include <CLI/CLI.hpp>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cli/cli.h"
#include "cli/common.h"
#include "core/checkout.h"
#include "core/commit.h"
#include "core/error.h"
#include "core/pr.h"
#include "core/refs.h"

namespace tva::cli {
namespace {

// Accepts both "<slug>" and "pr/<slug>".
std::string strip_pr_prefix(std::string name) {
  if (name.starts_with("pr/")) name.erase(0, 3);
  return name;
}

std::string plural(std::size_t n, const char* word) {
  return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

void register_open(CLI::App& pr) {
  struct Options {
    std::string intent;
    std::string slug;
    bool force = false;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = pr.add_subcommand("open", "Open a PR on top of main and switch to it");
  cmd->add_option("intent", opt->intent, "What the PR is for; its first paragraph is the title")
      ->required();
  auto* slug = cmd->add_option("--slug", opt->slug, "Use this slug instead of deriving one from the title");
  cmd->add_flag("--force", opt->force, "Discard unsnapshotted changes if the worktree must change");
  cmd->callback([opt, slug] {
    auto repo = open_repo();
    std::optional<std::string> wanted;
    if (slug->count() > 0) wanted = opt->slug;
    const core::OpenedPr opened = core::open_pr(repo, opt->intent, wanted, opt->force);
    std::cout << "Opened PR " << opened.slug << ": " << opened.title << '\n';
  });
}

void register_list(CLI::App& pr) {
  auto* cmd = pr.add_subcommand("list", "List open PRs (* marks the current one)");
  cmd->callback([] {
    auto repo = open_repo();
    const auto prs = core::list_prs(repo);
    if (prs.empty()) {
      std::cout << "no open PRs\n";
      return;
    }
    const auto current = core::current_pr(repo.refs());
    std::size_t width = 0;
    for (const auto& p : prs) width = std::max(width, p.slug.size());
    for (const auto& p : prs) {
      std::cout << (p.slug == current ? '*' : ' ') << ' ' << p.slug
                << std::string(width - p.slug.size(), ' ') << "  ";
      if (p.open_commit) {
        std::cout << p.title << " (" << plural(p.moments.size(), "moment") << ")\n";
      } else {
        std::cout << "(not opened by tva)\n";
      }
    }
  });
}

void register_switch(CLI::App& pr) {
  struct Options {
    std::string target;
    bool force = false;
  };
  auto opt = std::make_shared<Options>();
  auto* cmd = pr.add_subcommand("switch", "Switch to a PR, or back to main, rewriting the worktree");
  cmd->add_option("target", opt->target, "PR slug, or main for the Sacred Timeline")->required();
  cmd->add_flag("--force", opt->force, "Discard unsnapshotted changes");
  cmd->callback([opt] {
    auto repo = open_repo();
    const std::string target = strip_pr_prefix(opt->target);
    std::string ref;
    std::string label;
    if (target == core::kMainName) {
      ref = core::kMainRef;
      label = "main";
    } else {
      ref = core::pr_ref(target);
      if (target.empty() || !core::is_valid_ref_name(ref) || !repo.refs().resolve(ref)) {
        throw core::Error("no such PR '" + target + "'");
      }
      label = "PR " + target;
    }
    if (repo.refs().symbolic_target("HEAD") == ref) {
      std::cout << "Already on " << label << '\n';
      return;
    }
    core::switch_branch(repo, ref, opt->force);
    std::cout << "Switched to " << label << '\n';
  });
}

void register_show(CLI::App& pr) {
  auto slug = std::make_shared<std::string>();
  auto* cmd = pr.add_subcommand("show", "Show a PR's intent, base, head and moments");
  cmd->add_option("slug", *slug, "PR to show (default: the current one)");
  cmd->callback([slug] {
    auto repo = open_repo();
    const auto current = core::current_pr(repo.refs());
    std::string name = strip_pr_prefix(*slug);
    if (name.empty()) {
      if (!current) throw core::Error("not on a PR; name one: tva pr show <slug>");
      name = *current;
    }
    const auto info = core::read_pr(repo, name);
    if (!info) throw core::Error("no such PR '" + name + "'");

    std::cout << "PR " << info->slug << (info->slug == current ? " (current)" : "") << '\n';
    if (!info->open_commit) {
      std::cout << "Head:  " << short_id(info->head) << '\n'
                << "(not opened by tva: no " << core::kPrOpenTrailer << " commit found)\n";
      return;
    }
    std::cout << "Title: " << info->title << '\n'
              << "Base:  " << (info->base ? short_id(*info->base) : "(empty main)") << '\n'
              << "Head:  " << short_id(info->head) << '\n';
    if (!info->description.empty()) {
      std::cout << '\n';
      std::string_view rest = info->description;
      while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view line = rest.substr(0, nl);
        std::cout << (line.empty() ? "" : "    ") << line << '\n';
        rest.remove_prefix(nl == std::string_view::npos ? rest.size() : nl + 1);
      }
    }
    std::cout << "\nMoments: " << info->moments.size() << '\n';
    for (const auto& id : info->moments) {
      std::cout << short_id(id) << ' ' << core::commit_subject(repo.odb().read_commit(id).message)
                << '\n';
    }
  });
}

}  // namespace

void register_pr(CLI::App& app) {
  auto* pr = app.add_subcommand("pr", "Open, list, switch between and inspect PRs");
  pr->require_subcommand(1);
  register_open(*pr);
  register_list(*pr);
  register_switch(*pr);
  register_show(*pr);
}

}  // namespace tva::cli
