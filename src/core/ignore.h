#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tva::core {

// git's wildmatch() with WM_PATHNAME: '*', '?' and [...] never match '/';
// "**" spanning whole path components matches across directories.
bool glob_match(std::string_view pattern, std::string_view path);

// The .gitignore subset TVA honours: <dir>/.gitignore files plus
// .git/info/exclude. Paths are '/'-separated and relative to the worktree.
//
// Precedence follows git: patterns are consulted from the deepest
// .gitignore up to the root one, then info/exclude, and the last matching
// line of a file wins. Equivalently: concatenate exclude, root, ..., deepest
// and the last match decides. Anything inside an ignored directory is
// ignored and cannot be re-included with '!'.
class IgnoreMatcher {
 public:
  IgnoreMatcher() = default;  // empty, in-memory matcher (for tests)

  // Reads .git/info/exclude now; each directory's .gitignore is read on
  // first use, and only for directories that are not themselves ignored.
  static IgnoreMatcher load(const std::filesystem::path& worktree_root);

  // As if `text` were the content of <dir>/.gitignore ("" = worktree root).
  void add_patterns(std::string_view dir, std::string_view text);
  // As if `text` were .git/info/exclude.
  void add_exclude_patterns(std::string_view text);

  // .git (any type) and .tva directories are always ignored, at any depth.
  bool is_ignored(std::string_view rel_path, bool is_dir) const;

 private:
  struct Pattern {
    std::string text;  // without leading '!' and trailing '/'
    bool negative = false;
    bool must_be_dir = false;
    bool basename_only = false;  // no '/' in the pattern: match the basename
  };
  using PatternList = std::vector<Pattern>;

  static PatternList parse(std::string_view text);
  // Key: "" for the root, "a/b/" for a subdirectory.
  const PatternList& patterns_for(const std::string& dir_prefix) const;
  // Decision of the last matching pattern, or nullopt if none matches.
  std::optional<bool> decide(std::string_view path, bool is_dir) const;
  bool dir_ignored(std::string_view dir) const;

  std::optional<std::filesystem::path> root_;
  PatternList exclude_;
  mutable std::unordered_map<std::string, PatternList> per_dir_;
  mutable std::unordered_map<std::string, bool> dir_cache_;
};

}  // namespace tva::core
