#pragma once

#include "core/object_id.h"
#include "core/repository.h"

namespace tva::core {

// Writes the whole worktree into the object database and returns the root
// tree id. Ignored directories are not entered; regular files become blobs
// (100644, or 100755 if owner-executable); symlinks become blobs holding the
// link target (120000, never followed); directories with nothing tracked
// produce no entry. Other file types (FIFOs, sockets...) are skipped, as git
// does. An empty worktree yields the empty tree.
ObjectId snapshot_worktree(Repository& repo);

}  // namespace tva::core
