#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.h"
#include "core/refs.h"
#include "core/repository.h"

namespace tva::core {

// The Sacred Timeline. PRs branch off it and (from M5) are squashed back in.
inline constexpr std::string_view kMainRef = "refs/heads/main";
inline constexpr std::string_view kMainName = "main";
inline constexpr std::string_view kPrRefPrefix = "refs/heads/pr/";
// Where M5 will archive merged and pruned PRs; their slugs stay reserved.
inline constexpr std::string_view kMergedPrRefPrefix = "refs/tva/merged/pr/";
inline constexpr std::string_view kPrunedPrRefPrefix = "refs/tva/pruned/pr/";
// Trailer key that marks a PR's open commit; the value is the PR's slug.
inline constexpr std::string_view kPrOpenTrailer = "TVA-PR-Open";

inline constexpr std::size_t kMaxGeneratedSlugLength = 40;
inline constexpr std::size_t kMaxSlugLength = 64;

std::string pr_ref(std::string_view slug);  // "refs/heads/pr/<slug>"

// ^[a-z0-9]+(-[a-z0-9]+)*$ and at most kMaxSlugLength characters.
bool is_valid_slug(std::string_view slug);
// ASCII letters (lowercased) and digits are kept; every run of anything else,
// non-ASCII text included, becomes one '-'. Longer than 40 characters is cut
// at the last '-' within the first 40 when there is one. "pr" if nothing is
// left, e.g. for an all-Chinese title.
std::string slug_from_title(std::string_view title);
// "main" (reserved for `tva pr switch main`) or the slug of an open, merged
// or pruned PR.
bool slug_taken(const RefStore& refs, std::string_view slug);
// slug_from_title(title), or the first of <slug>-2, <slug>-3, ... not taken.
std::string unique_slug(const RefStore& refs, std::string_view title);

// The slug if HEAD is a symbolic ref to refs/heads/pr/<slug>.
std::optional<std::string> current_pr(const RefStore& refs);

struct PrInfo {
  std::string slug;
  ObjectId head;
  // The commit that carries "TVA-PR-Open: <slug>". nullopt for a pr/ branch
  // TVA did not open; base, title, description and moments are then empty.
  std::optional<ObjectId> open_commit;
  std::optional<ObjectId> base;  // the open commit's parent; nullopt if main was empty
  std::string title;             // git subject of the intent
  std::string description;       // the intent's paragraphs after the first
  std::vector<ObjectId> moments;  // first-parent commits after the open commit, newest first
};

// nullopt if refs/heads/pr/<slug> does not exist. The open commit is found by
// walking first parents from the head.
std::optional<PrInfo> read_pr(const Repository& repo, std::string_view slug);
// Every refs/heads/pr/<slug>, sorted by slug.
std::vector<PrInfo> list_prs(const Repository& repo);

struct OpenedPr {
  std::string slug;
  std::string title;
  ObjectId open_commit;
};

// Opens a PR on top of main and switches HEAD to it. The open commit has
// main's tree, main's head as its only parent (no parent while main is
// unborn) and the intent followed by a "TVA-PR-Open: <slug>" trailer as its
// message. `slug` overrides the generated one and must be valid and free.
// The worktree is handled as plan_checkout() describes; it is checked before
// the PR ref exists, and the ref is deleted again if rewriting it fails.
OpenedPr open_pr(Repository& repo, std::string_view intent, const std::optional<std::string>& slug,
                 bool force);

}  // namespace tva::core
