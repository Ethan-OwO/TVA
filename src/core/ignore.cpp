#include "core/ignore.h"

#include <cctype>
#include <system_error>
#include <utility>

#include "core/bytes.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// wildmatch, ported from git's wildmatch.c (WM_PATHNAME, no case folding).
// Strings are treated as if NUL-terminated, exactly like the original, so the
// control flow can stay line-for-line comparable.
namespace {

enum : int { kMatch = 0, kNoMatch = 1, kAbortAll = -1, kAbortToStarStar = -2 };

bool is_glob_special(unsigned char c) { return c == '*' || c == '?' || c == '[' || c == '\\'; }

bool class_member(std::string_view cls, unsigned char c, bool& known) {
  known = true;
  const bool ascii = c < 0x80;
  if (cls == "alnum") return ascii && (std::isalnum(c) != 0);
  if (cls == "alpha") return ascii && (std::isalpha(c) != 0);
  if (cls == "blank") return c == ' ' || c == '\t';
  if (cls == "cntrl") return ascii && (std::iscntrl(c) != 0);
  if (cls == "digit") return c >= '0' && c <= '9';
  if (cls == "graph") return ascii && (std::isgraph(c) != 0);
  if (cls == "lower") return c >= 'a' && c <= 'z';
  if (cls == "print") return c >= 0x20 && c <= 0x7E;
  if (cls == "punct") return ascii && (std::ispunct(c) != 0);
  if (cls == "space") return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  if (cls == "upper") return c >= 'A' && c <= 'Z';
  if (cls == "xdigit") return ascii && (std::isxdigit(c) != 0);
  known = false;
  return false;
}

class WildMatcher {
 public:
  WildMatcher(std::string_view pattern, std::string_view text) : pat_(pattern), text_(text) {}
  int run() const { return dowild(0, 0); }

 private:
  unsigned char p_at(std::size_t i) const {
    return i < pat_.size() ? static_cast<unsigned char>(pat_[i]) : 0;
  }
  unsigned char t_at(std::size_t i) const {
    return i < text_.size() ? static_cast<unsigned char>(text_[i]) : 0;
  }
  bool slash_from(std::size_t t) const { return text_.find('/', t) != std::string_view::npos; }

  int dowild(std::size_t p, std::size_t t) const {
    for (; p_at(p) != 0; ++t, ++p) {
      unsigned char p_ch = p_at(p);
      unsigned char t_ch = t_at(t);
      if (t_ch == 0 && p_ch != '*') return kAbortAll;
      switch (p_ch) {
        case '\\':
          // Literal match with the following character.
          p_ch = p_at(++p);
          [[fallthrough]];
        default:
          if (t_ch != p_ch) return kNoMatch;
          continue;
        case '?':
          if (t_ch == '/') return kNoMatch;
          continue;
        case '*': {
          bool match_slash = false;
          if (p_at(++p) == '*') {
            const bool boundary_before = p < 2 || p_at(p - 2) == '/';
            while (p_at(++p) == '*') {
            }
            if (boundary_before &&
                (p_at(p) == 0 || p_at(p) == '/' || (p_at(p) == '\\' && p_at(p + 1) == '/'))) {
              // "**/" may also match zero directories: try the rest of the
              // pattern right here first.
              if (p_at(p) == '/' && dowild(p + 1, t) == kMatch) return kMatch;
              match_slash = true;
            }
            // Otherwise "**" is not a whole component and acts like "*".
          }
          if (p_at(p) == 0) {
            // Trailing "**" matches everything; trailing "*" only if no more
            // '/' follows.
            if (!match_slash && slash_from(t)) return kNoMatch;
            return kMatch;
          }
          if (!match_slash && p_at(p) == '/') {
            // A single '*' followed by '/' matches up to the next '/'.
            std::size_t slash = text_.find('/', t);
            if (slash == std::string_view::npos) return kNoMatch;
            t = slash;
            break;  // the loop increment consumes the '/' on both sides
          }
          for (;;) {
            if (t_ch == 0) break;
            // Fast-forward to the next occurrence of a literal that follows
            // the star; everything before it must belong to the star.
            if (!is_glob_special(p_at(p))) {
              p_ch = p_at(p);
              while ((t_ch = t_at(t)) != 0 && (match_slash || t_ch != '/')) {
                if (t_ch == p_ch) break;
                ++t;
              }
              if (t_ch != p_ch) return match_slash ? kAbortAll : kAbortToStarStar;
            }
            int matched = dowild(p, t);
            if (matched != kNoMatch) {
              if (!match_slash || matched != kAbortToStarStar) return matched;
            } else if (!match_slash && t_ch == '/') {
              return kAbortToStarStar;
            }
            t_ch = t_at(++t);
          }
          return kAbortAll;
        }
        case '[': {
          p_ch = p_at(++p);
          if (p_ch == '^') p_ch = '!';
          const bool negated = p_ch == '!';
          if (negated) p_ch = p_at(++p);
          unsigned char prev_ch = 0;
          bool matched = false;
          do {
            if (p_ch == 0) return kAbortAll;
            if (p_ch == '\\') {
              p_ch = p_at(++p);
              if (p_ch == 0) return kAbortAll;
              if (t_ch == p_ch) matched = true;
            } else if (p_ch == '-' && prev_ch != 0 && p_at(p + 1) != 0 && p_at(p + 1) != ']') {
              p_ch = p_at(++p);
              if (p_ch == '\\') {
                p_ch = p_at(++p);
                if (p_ch == 0) return kAbortAll;
              }
              if (t_ch <= p_ch && t_ch >= prev_ch) matched = true;
              p_ch = 0;  // a range end cannot start another range
            } else if (p_ch == '[' && p_at(p + 1) == ':') {
              const std::size_t start = p += 2;
              while ((p_ch = p_at(p)) != 0 && p_ch != ']') ++p;
              if (p_ch == 0) return kAbortAll;
              if (p == start || p_at(p - 1) != ':') {
                // No ":]" -- treat the '[' as an ordinary set member.
                p = start - 2;
                p_ch = '[';
                if (t_ch == p_ch) matched = true;
                continue;
              }
              bool known = false;
              if (class_member(pat_.substr(start, p - start - 1), t_ch, known)) matched = true;
              if (!known) return kAbortAll;  // malformed [:class:]
              p_ch = 0;
            } else if (t_ch == p_ch) {
              matched = true;
            }
          } while (prev_ch = p_ch, (p_ch = p_at(++p)) != ']');
          if (matched == negated || t_ch == '/') return kNoMatch;
          continue;
        }
      }
    }
    return t_at(t) != 0 ? kNoMatch : kMatch;
  }

  std::string_view pat_;
  std::string_view text_;
};

// git's trim_trailing_spaces(): drop trailing ' ' unless backslash-escaped.
std::string_view trim_trailing_spaces(std::string_view s) {
  std::size_t cut = std::string_view::npos;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == ' ') {
      if (cut == std::string_view::npos) cut = i;
    } else {
      if (s[i] == '\\' && ++i == s.size()) return s;
      cut = std::string_view::npos;
    }
  }
  return cut == std::string_view::npos ? s : s.substr(0, cut);
}

std::string_view basename_of(std::string_view path) {
  std::size_t slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

bool glob_match(std::string_view pattern, std::string_view path) {
  return WildMatcher(pattern, path).run() == kMatch;
}

// ---------------------------------------------------------------------------

IgnoreMatcher::PatternList IgnoreMatcher::parse(std::string_view text) {
  PatternList out;
  if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);  // UTF-8 BOM
  while (!text.empty()) {
    std::size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
    if (line.ends_with('\r')) line.remove_suffix(1);
    if (line.empty() || line.front() == '#') continue;  // "\#" is an escape, not a comment
    line = trim_trailing_spaces(line);
    Pattern p;
    if (line.starts_with('!')) {  // "\!" stays a literal '!' for the matcher
      p.negative = true;
      line.remove_prefix(1);
    }
    if (line.ends_with('/')) {
      p.must_be_dir = true;
      line.remove_suffix(1);
    }
    if (line.empty()) continue;
    p.basename_only = line.find('/') == std::string_view::npos;
    p.text = std::string(line);
    out.push_back(std::move(p));
  }
  return out;
}

IgnoreMatcher IgnoreMatcher::load(const fs::path& worktree_root) {
  IgnoreMatcher m;
  m.root_ = worktree_root;
  fs::path exclude = worktree_root / ".git" / "info" / "exclude";
  std::error_code ec;
  if (fs::is_regular_file(fs::symlink_status(exclude, ec))) {
    m.add_exclude_patterns(as_chars(platform::read_file(exclude)));
  }
  return m;
}

void IgnoreMatcher::add_patterns(std::string_view dir, std::string_view text) {
  while (dir.ends_with('/')) dir.remove_suffix(1);
  std::string key = dir.empty() ? "" : std::string(dir) + "/";
  PatternList& list = per_dir_[key];
  for (auto& p : parse(text)) list.push_back(std::move(p));
  dir_cache_.clear();
}

void IgnoreMatcher::add_exclude_patterns(std::string_view text) {
  for (auto& p : parse(text)) exclude_.push_back(std::move(p));
  dir_cache_.clear();
}

const IgnoreMatcher::PatternList& IgnoreMatcher::patterns_for(const std::string& dir_prefix) const {
  if (auto it = per_dir_.find(dir_prefix); it != per_dir_.end()) return it->second;
  PatternList list;
  if (root_) {
    fs::path file = *root_ / dir_prefix / ".gitignore";
    std::error_code ec;
    // Like git >= 2.32, a symlinked .gitignore is not followed.
    if (fs::is_regular_file(fs::symlink_status(file, ec))) {
      list = parse(as_chars(platform::read_file(file)));
    }
  }
  // unordered_map never moves its elements, so the reference stays valid.
  return per_dir_.emplace(dir_prefix, std::move(list)).first->second;
}

std::optional<bool> IgnoreMatcher::decide(std::string_view path, bool is_dir) const {
  const std::string_view base = basename_of(path);
  auto matches = [&](const Pattern& p, std::string_view rel) {
    if (p.must_be_dir && !is_dir) return false;
    if (p.basename_only) return glob_match(p.text, base);
    std::string_view pat = p.text;
    if (pat.starts_with('/')) pat.remove_prefix(1);  // anchoring is implied
    return glob_match(pat, rel);
  };

  // Deepest .gitignore first: "a/b/", "a/", "".
  for (std::size_t end = path.rfind('/');; end = path.rfind('/', end - 1)) {
    const std::size_t prefix_len = end == std::string_view::npos ? 0 : end + 1;
    const PatternList& list = patterns_for(std::string(path.substr(0, prefix_len)));
    const std::string_view rel = path.substr(prefix_len);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      if (matches(*it, rel)) return !it->negative;
    }
    if (end == std::string_view::npos || end == 0) break;
  }
  for (auto it = exclude_.rbegin(); it != exclude_.rend(); ++it) {
    if (matches(*it, path)) return !it->negative;
  }
  return std::nullopt;
}

bool IgnoreMatcher::dir_ignored(std::string_view dir) const {
  std::string key(dir);
  if (auto it = dir_cache_.find(key); it != dir_cache_.end()) return it->second;
  bool ignored = decide(dir, true).value_or(false);
  dir_cache_.emplace(std::move(key), ignored);
  return ignored;
}

bool IgnoreMatcher::is_ignored(std::string_view path, bool is_dir) const {
  if (path.empty()) return false;
  for (std::size_t start = 0;;) {
    std::size_t slash = path.find('/', start);
    std::string_view name = path.substr(start, slash - start);
    bool name_is_dir = slash != std::string_view::npos || is_dir;
    if (name == ".git" || (name == ".tva" && name_is_dir)) return true;
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  // Checking parents top-down also guarantees a directory's .gitignore is
  // only read once the directory itself is known not to be ignored.
  for (std::size_t slash = path.find('/'); slash != std::string_view::npos;
       slash = path.find('/', slash + 1)) {
    if (dir_ignored(path.substr(0, slash))) return true;
  }
  return decide(path, is_dir).value_or(false);
}

}  // namespace tva::core
