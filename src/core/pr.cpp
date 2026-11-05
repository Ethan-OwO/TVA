#include "core/pr.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/checkout.h"
#include "core/commit.h"
#include "core/error.h"
#include "core/trailers.h"
#include "core/tree.h"

namespace tva::core {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_slug_char(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool is_blank(std::string_view line) {
  for (char c : line) {
    if (!is_space(c)) return false;
  }
  return true;
}

// Splits off the first line of `s` (without its '\n').
std::string_view take_line(std::string_view& s) {
  std::size_t nl = s.find('\n');
  std::string_view line = s.substr(0, nl);
  s.remove_prefix(nl == std::string_view::npos ? s.size() : nl + 1);
  return line;
}

std::string_view drop_leading_blank_lines(std::string_view s) {
  for (std::string_view rest = s; !rest.empty();) {
    std::string_view before = rest;
    if (!is_blank(take_line(rest))) return before;
  }
  return {};
}

std::string_view trim_right(std::string_view s) {
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

// Everything after the first paragraph, without surrounding blank lines.
std::string description_of(std::string_view body) {
  std::string_view rest = drop_leading_blank_lines(body);
  while (!rest.empty() && !is_blank(take_line(rest))) {
  }
  return std::string(trim_right(drop_leading_blank_lines(rest)));
}

std::optional<ObjectId> first_parent(const Commit& c) {
  if (c.parents.empty()) return std::nullopt;
  return c.parents.front();
}

}  // namespace

std::string pr_ref(std::string_view slug) { return std::string(kPrRefPrefix) + std::string(slug); }

bool is_valid_slug(std::string_view slug) {
  if (slug.empty() || slug.size() > kMaxSlugLength) return false;
  if (slug.front() == '-' || slug.back() == '-' || slug.find("--") != std::string_view::npos) {
    return false;
  }
  for (char c : slug) {
    if (!is_slug_char(c) && c != '-') return false;
  }
  return true;
}

std::string slug_from_title(std::string_view title) {
  std::string slug;
  bool gap = false;  // saw something to replace since the last kept character
  for (char raw : title) {
    char c = raw >= 'A' && raw <= 'Z' ? static_cast<char>(raw - 'A' + 'a') : raw;
    if (!is_slug_char(c)) {
      gap = true;
      continue;
    }
    if (gap && !slug.empty()) slug += '-';
    gap = false;
    slug += c;
  }
  if (slug.size() > kMaxGeneratedSlugLength) {
    // A '-' at index 40 means the first 40 characters end on a word boundary.
    std::size_t dash = slug.rfind('-', kMaxGeneratedSlugLength);
    slug.resize(dash == std::string::npos ? kMaxGeneratedSlugLength : dash);
  }
  return slug.empty() ? "pr" : slug;
}

bool slug_taken(const RefStore& refs, std::string_view slug) {
  if (slug == kMainName) return true;
  for (std::string_view prefix : {kPrRefPrefix, kMergedPrRefPrefix, kPrunedPrRefPrefix}) {
    if (refs.read_raw(std::string(prefix) + std::string(slug))) return true;
  }
  return false;
}

std::string unique_slug(const RefStore& refs, std::string_view title) {
  const std::string base = slug_from_title(title);
  if (!slug_taken(refs, base)) return base;
  for (int n = 2;; ++n) {
    std::string candidate = base + "-" + std::to_string(n);
    if (!slug_taken(refs, candidate)) return candidate;
  }
}

std::optional<std::string> current_pr(const RefStore& refs) {
  auto target = refs.symbolic_target("HEAD");
  if (!target || !target->starts_with(kPrRefPrefix)) return std::nullopt;
  return target->substr(kPrRefPrefix.size());
}

std::optional<PrInfo> read_pr(const Repository& repo, std::string_view slug) {
  const std::string ref = pr_ref(slug);
  if (slug.empty() || !is_valid_ref_name(ref)) return std::nullopt;
  auto head = repo.refs().resolve(ref);
  if (!head) return std::nullopt;

  PrInfo info;
  info.slug = std::string(slug);
  info.head = *head;
  std::vector<ObjectId> walked;
  for (std::optional<ObjectId> id = *head; id;) {
    Commit c = repo.odb().read_commit(*id);
    MessageParts parts = split_trailers(c.message);
    if (trailer_value(parts.trailers, kPrOpenTrailer) == slug) {
      info.open_commit = *id;
      info.base = first_parent(c);
      info.title = commit_subject(parts.body);
      info.description = description_of(parts.body);
      info.moments = std::move(walked);
      break;
    }
    walked.push_back(*id);
    id = first_parent(c);
  }
  return info;
}

std::vector<PrInfo> list_prs(const Repository& repo) {
  std::vector<PrInfo> out;
  for (const auto& [ref, id] : repo.refs().list(kPrRefPrefix)) {
    if (auto info = read_pr(repo, std::string_view(ref).substr(kPrRefPrefix.size()))) {
      out.push_back(std::move(*info));
    }
  }
  return out;  // RefStore::list is sorted by ref name, hence by slug
}

OpenedPr open_pr(Repository& repo, std::string_view intent, const std::optional<std::string>& slug,
                 bool force) {
  const std::string body(trim_right(drop_leading_blank_lines(intent)));
  if (body.empty()) throw Error("a PR needs an intent (its first paragraph becomes the title)");
  const std::string title = commit_subject(body);

  std::string chosen;
  if (slug) {
    if (!is_valid_slug(*slug)) {
      throw Error("invalid slug '" + *slug +
                  "': use lowercase letters and digits separated by single '-' (at most " +
                  std::to_string(kMaxSlugLength) + " characters)");
    }
    if (slug_taken(repo.refs(), *slug)) throw Error("slug '" + *slug + "' is already taken");
    chosen = *slug;
  } else {
    chosen = unique_slug(repo.refs(), title);
  }

  Commit commit;
  commit.author = repo.current_signature();  // fail on a missing identity before any write
  commit.committer = repo.current_committer();
  const std::optional<ObjectId> base = repo.refs().resolve(kMainRef);
  commit.tree = base ? repo.odb().read_commit(*base).tree : repo.odb().write_tree(Tree{});
  if (base) commit.parents.push_back(*base);
  commit.message = append_trailers(body, {{std::string(kPrOpenTrailer), chosen}});

  const CheckoutPlan plan = plan_checkout(repo, commit.tree, force);  // throws before any ref moves
  const ObjectId id = repo.odb().write_commit(commit);
  const std::string ref = pr_ref(chosen);
  repo.refs().update(ref, id, std::nullopt);
  try {
    apply_checkout(repo, plan, ref);
  } catch (...) {
    try {
      repo.refs().remove(ref, id);  // leave no half-opened PR behind
    } catch (...) {
    }
    throw;
  }
  return {chosen, title, id};
}

}  // namespace tva::core
