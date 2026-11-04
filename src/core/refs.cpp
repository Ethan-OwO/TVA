#include "core/refs.h"

#include <algorithm>

#include "core/bytes.h"
#include "core/error.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

namespace {

constexpr int kMaxSymrefDepth = 5;

bool ends_with_space(std::string_view s) {
  return !s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t');
}

std::string_view trim_right(std::string_view s) {
  while (ends_with_space(s)) s.remove_suffix(1);
  return s;
}

}  // namespace

bool is_valid_ref_name(std::string_view name) {
  if (name == "HEAD") return true;
  if (!name.starts_with("refs/") || name.size() == 5) return false;
  if (name.ends_with("/") || name.ends_with(".") || name.ends_with(".lock")) return false;
  if (name.find("..") != std::string_view::npos || name.find("//") != std::string_view::npos ||
      name.find("@{") != std::string_view::npos || name.find("/.") != std::string_view::npos) {
    return false;
  }
  for (unsigned char c : name) {
    if (c <= 0x20 || c == 0x7F) return false;  // control characters and space
    switch (c) {
      case '~':
      case '^':
      case ':':
      case '?':
      case '*':
      case '[':
      case '\\':
        return false;
      default:
        break;
    }
  }
  return true;
}

RefStore::RefStore(fs::path git_dir) : git_dir_(std::move(git_dir)) {}

fs::path RefStore::ref_path(std::string_view name) const {
  if (!is_valid_ref_name(name)) throw Error("invalid ref name '" + std::string(name) + "'");
  return git_dir_ / std::string(name);
}

std::optional<RefValue> RefStore::read_raw(std::string_view name) const {
  fs::path path = ref_path(name);
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return std::nullopt;
  Bytes raw = platform::read_file(path);
  std::string_view content = trim_right(as_chars(raw));
  if (content.starts_with("ref:")) {
    std::string_view target = content.substr(4);
    while (!target.empty() && (target.front() == ' ' || target.front() == '\t')) {
      target.remove_prefix(1);
    }
    if (!is_valid_ref_name(target)) {
      throw Error("ref " + std::string(name) + " has an invalid target '" + std::string(target) + "'");
    }
    return RefValue{std::string(target)};
  }
  if (auto id = ObjectId::from_hex(content)) return RefValue{*id};
  throw Error("invalid ref " + std::string(name));
}

std::optional<ObjectId> RefStore::resolve(std::string_view name) const {
  std::string current(name);
  for (int hops = 0;; ++hops) {
    auto value = read_raw(current);
    if (!value) return std::nullopt;
    if (const auto* id = std::get_if<ObjectId>(&value->value)) return *id;
    if (hops == kMaxSymrefDepth) {
      throw Error("ref " + std::string(name) + ": too many levels of symbolic refs");
    }
    current = std::get<std::string>(value->value);
  }
}

std::optional<std::string> RefStore::symbolic_target(std::string_view name) const {
  auto value = read_raw(name);
  if (!value) return std::nullopt;
  if (const auto* target = std::get_if<std::string>(&value->value)) return *target;
  return std::nullopt;
}

std::vector<std::pair<std::string, ObjectId>> RefStore::list(std::string_view prefix) const {
  std::vector<std::pair<std::string, ObjectId>> out;
  std::error_code ec;
  fs::path refs_dir = git_dir_ / "refs";
  if (!fs::is_directory(refs_dir, ec)) return out;
  for (const auto& entry : fs::recursive_directory_iterator(refs_dir)) {
    if (!entry.is_regular_file()) continue;
    std::string name = entry.path().lexically_relative(git_dir_).generic_string();
    if (!is_valid_ref_name(name) || !name.starts_with(prefix)) continue;  // skips *.lock
    if (auto id = resolve(name)) out.emplace_back(std::move(name), *id);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace tva::core
