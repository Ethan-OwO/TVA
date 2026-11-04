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
  if (spec.empty()) throw Error("empty revision");
  return resolve_base(repo, spec);
}

}  // namespace tva::core
