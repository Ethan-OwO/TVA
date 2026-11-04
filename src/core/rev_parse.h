#pragma once

#include <string_view>

#include "core/object_id.h"
#include "core/repository.h"

namespace tva::core {

// Resolves a revision, trying in order: a full 40-hex id, HEAD, a full
// "refs/..." name, refs/heads/<spec>, refs/tags/<spec>, then a unique hex
// prefix of at least 4 digits. Then applies suffixes left to right:
// "~<n>" (n-th first-parent ancestor, n defaults to 1), "^" / "^<n>" (n-th
// parent; "^0" is the commit itself).
ObjectId rev_parse(const Repository& repo, std::string_view spec);

}  // namespace tva::core
