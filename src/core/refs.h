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

  // Compare-and-swap: takes <ref>.lock, re-reads the current value while
  // holding it, and only then writes. `expected_old == nullopt` means the
  // ref must not exist yet. Throws Error("ref <name> changed concurrently")
  // on a mismatch and Error("unable to lock ref <name>") if the lock is held.
  // `name` itself is written (a symbolic ref is not followed).
  void update(std::string_view name, const ObjectId& new_id, std::optional<ObjectId> expected_old);
  // Compare-and-delete under the same lock: throws Error("ref <name> changed
  // concurrently") unless `name` currently resolves to `expected_old`. Like
  // git, directories below refs/<category>/ that become empty are removed.
  void remove(std::string_view name, const ObjectId& expected_old);
  void set_symbolic(std::string_view name, std::string_view target);
  // Every ref under refs/ whose name starts with `prefix`, resolved, sorted.
  std::vector<std::pair<std::string, ObjectId>> list(std::string_view prefix) const;

 private:
  std::filesystem::path ref_path(std::string_view name) const;

  std::filesystem::path git_dir_;
};

}  // namespace tva::core
