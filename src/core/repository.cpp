#include "core/repository.h"

#include <cstdlib>
#include <optional>
#include <string>

#include "core/bytes.h"
#include "core/error.h"
#include "platform/clock.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

namespace {

std::optional<std::string> env(const char* name) {
  const char* v = std::getenv(name);
  if (v == nullptr) return std::nullopt;
  return std::string(v);
}

std::vector<fs::path> config_files(const fs::path& git_dir) {
  std::vector<fs::path> files;
  auto home = env("HOME");
  if (auto xdg = env("XDG_CONFIG_HOME"); xdg && !xdg->empty()) {
    files.push_back(fs::path(*xdg) / "git" / "config");
  } else if (home) {
    files.push_back(fs::path(*home) / ".config" / "git" / "config");
  }
  if (home) files.push_back(fs::path(*home) / ".gitconfig");
  files.push_back(git_dir / "config");
  return files;
}

// git's strbuf_addstr_without_crud(): trim "crud" (whitespace and .,:;<>"\')
// from both ends and drop the delimiters <, > and newline from the middle,
// so identities serialize exactly as git would serialize them.
std::string sanitize_ident(std::string_view s) {
  auto crud = [](unsigned char c) {
    return c <= 32 || c == '.' || c == ',' || c == ':' || c == ';' || c == '<' || c == '>' ||
           c == '"' || c == '\\' || c == '\'';
  };
  while (!s.empty() && crud(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && crud(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  std::string out;
  for (char c : s) {
    if (c != '\n' && c != '<' && c != '>') out += c;
  }
  return out;
}

// "<unix seconds> <+HHMM>", optionally with git's "@" prefix.
void parse_env_date(const std::string& value, const char* var, Signature& sig) {
  try {
    std::string_view v = value;
    if (v.starts_with('@')) v.remove_prefix(1);
    Signature parsed = parse_signature("x <x> " + std::string(v));
    sig.when_unix = parsed.when_unix;
    sig.tz_offset_minutes = parsed.tz_offset_minutes;
  } catch (const Error&) {
    throw Error(std::string("invalid ") + var + " '" + value +
                "' (expected \"<unix seconds> <+HHMM>\")");
  }
}

}  // namespace

Repository::Repository(fs::path worktree_root)
    : worktree_root_(std::move(worktree_root)),
      git_dir_(worktree_root_ / ".git"),
      tva_dir_(worktree_root_ / ".tva"),
      odb_(git_dir_ / "objects"),
      refs_(git_dir_),
      config_(Config::load_files(config_files(git_dir_))) {}

Signature Repository::current_signature() const {
  Signature sig;
  sig.name = sanitize_ident(env("GIT_AUTHOR_NAME").value_or(config_.get("user", "name").value_or("")));
  sig.email =
      sanitize_ident(env("GIT_AUTHOR_EMAIL").value_or(config_.get("user", "email").value_or("")));
  if (sig.name.empty() || sig.email.empty()) {
    throw Error("author identity unknown; set user.name and user.email");
  }
  if (auto date = env("GIT_AUTHOR_DATE")) {
    parse_env_date(*date, "GIT_AUTHOR_DATE", sig);
  } else {
    sig.when_unix = platform::unix_now();
    sig.tz_offset_minutes = platform::local_utc_offset_minutes(sig.when_unix);
  }
  return sig;
}

Signature Repository::current_committer() const {
  Signature sig = current_signature();
  if (auto name = env("GIT_COMMITTER_NAME")) sig.name = sanitize_ident(*name);
  if (auto email = env("GIT_COMMITTER_EMAIL")) sig.email = sanitize_ident(*email);
  if (sig.name.empty() || sig.email.empty()) {
    throw Error("committer identity unknown; set user.name and user.email");
  }
  if (auto date = env("GIT_COMMITTER_DATE")) parse_env_date(*date, "GIT_COMMITTER_DATE", sig);
  return sig;
}

Repository Repository::init(const fs::path& worktree_root) {
  fs::create_directories(worktree_root);
  fs::path root = fs::canonical(worktree_root);
  fs::path git = root / ".git";
  std::error_code ec;
  if (fs::exists(fs::symlink_status(git, ec))) {
    throw Error("'" + git.string() + "' already exists");
  }
  for (const char* dir : {"objects", "refs/heads", "refs/tags", "info"}) {
    fs::create_directories(git / dir);
  }
  platform::write_file_atomic(git / "HEAD", as_bytes("ref: refs/heads/main\n"));
  platform::write_file_atomic(git / "config", as_bytes("[core]\n"
                                                       "\trepositoryformatversion = 0\n"
                                                       "\tfilemode = true\n"
                                                       "\tbare = false\n"));
  platform::write_file_atomic(git / "info" / "exclude", as_bytes(".tva/\n"));
  fs::create_directories(root / ".tva");
  return Repository(root);
}

Repository Repository::discover(const fs::path& start) {
  std::error_code ec;
  fs::path dir = fs::weakly_canonical(fs::absolute(start), ec);
  if (ec) dir = fs::absolute(start);
  for (;;) {
    if (fs::is_directory(dir / ".git", ec)) return Repository(dir);
    fs::path parent = dir.parent_path();
    if (parent == dir) break;
    dir = parent;
  }
  throw Error("not a tva repository (or any of the parent directories)");
}

}  // namespace tva::core
