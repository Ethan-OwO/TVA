#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/object_id.h"

namespace tva::core {

// A loose ref file holds either "<40 hex>\n" or "ref: <target>\n" (a symbolic
// ref such as HEAD -> refs/heads/main). M1 does not read packed-refs.
struct RefValue {
  std::variant<ObjectId, std::string> value;
};

// "HEAD", or "refs/..." without "..", "//", whitespace/control characters,
// any of ~^:?*[\, "@{", components starting with '.', or a trailing '/',
// '.' or ".lock". These are git's rules; they also keep a ref name from
// escaping .git/ when used as a path.
bool is_valid_ref_name(std::string_view name);

class RefStore {
 public:
  explicit RefStore(std::filesystem::path git_dir);

  std::optional<RefValue> read_raw(std::string_view name) const;
  // Follows symbolic refs (at most 5 hops). nullopt if the final ref does
  // not exist, e.g. HEAD on a branch with no commits yet.
  std::optional<ObjectId> resolve(std::string_view name) const;
  std::optional<std::string> symbolic_target(std::string_view name) const;
  // Every ref under refs/ whose name starts with `prefix`, resolved, sorted.
  std::vector<std::pair<std::string, ObjectId>> list(std::string_view prefix) const;

 private:
  std::filesystem::path ref_path(std::string_view name) const;

  std::filesystem::path git_dir_;
};

}  // namespace tva::core
