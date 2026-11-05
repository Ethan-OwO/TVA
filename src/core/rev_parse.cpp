#include "core/rev_parse.h"

#include <optional>
#include <string>

#include "core/error.h"

namespace tva::core {
namespace {

std::optional<ObjectId> resolve_ref(const RefStore& refs, const std::string& name) {
  if (!is_valid_ref_name(name)) return std::nullopt;
  return refs.resolve(name);
}

ObjectId resolve_base(const Repository& repo, std::string_view spec) {
  const std::string s(spec);
  if (auto id = ObjectId::from_hex(s)) return *id;
  if (s == "HEAD") {
    if (auto id = repo.refs().resolve("HEAD")) return *id;
    throw Error("unknown revision 'HEAD' (no commits yet)");
  }
  if (s.starts_with("refs/")) {
    if (auto id = resolve_ref(repo.refs(), s)) return *id;
  }
  for (const char* prefix : {"refs/heads/", "refs/tags/"}) {
    if (auto id = resolve_ref(repo.refs(), prefix + s)) return *id;
  }
  if (s.size() >= 4 && s.size() <= 40 && is_hex(s)) {
    auto matches = repo.odb().resolve_prefix(s);
    if (matches.size() == 1) return matches.front();
    if (matches.size() > 1) throw Error("short object ID " + s + " is ambiguous");
  }
  throw Error("unknown revision '" + s + "'");
}

}  // namespace

ObjectId rev_parse(const Repository& repo, std::string_view spec) {
  const std::size_t cut = spec.find_first_of("~^");
  const std::string_view base = spec.substr(0, cut);
  if (base.empty()) throw Error("unknown revision '" + std::string(spec) + "'");
  ObjectId id = resolve_base(repo, base);

  for (std::size_t pos = cut; pos < spec.size();) {
    const char op = spec[pos++];
    if (op != '~' && op != '^') throw Error("unknown revision '" + std::string(spec) + "'");
    std::size_t digits_end = pos;
    while (digits_end < spec.size() && spec[digits_end] >= '0' && spec[digits_end] <= '9') {
      ++digits_end;
    }
    std::size_t n = 1;
    if (digits_end > pos) {
      if (digits_end - pos > 9) throw Error("unknown revision '" + std::string(spec) + "'");
      n = std::stoul(std::string(spec.substr(pos, digits_end - pos)));
    }
    pos = digits_end;

    auto commit_of = [&](const ObjectId& c) {
      auto header = repo.odb().read_header(c);
      if (!header || header->first != ObjectType::Commit) {
        throw Error("revision '" + std::string(spec) + "' does not name a commit");
      }
      return repo.odb().read_commit(c);
    };
    if (op == '~') {
      for (std::size_t i = 0; i < n; ++i) {
        Commit c = commit_of(id);
        if (c.parents.empty()) throw Error("unknown revision '" + std::string(spec) + "'");
        id = c.parents.front();
      }
    } else {
      Commit c = commit_of(id);
      if (n == 0) continue;
      if (n > c.parents.size()) throw Error("unknown revision '" + std::string(spec) + "'");
      id = c.parents[n - 1];
    }
  }
  return id;
}

}  // namespace tva::core
