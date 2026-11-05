#pragma once

#include "core/object_id.h"
#include "core/repository.h"

namespace tva::core {

// Rewrites the worktree from `current_tree` (which must describe what is on
// disk, e.g. a fresh snapshot) to `target_tree`, touching only paths that
// differ:
//   - deletions first, each followed by removing parent directories that
//     became empty (never the worktree root);
//   - then additions/modifications: atomic file writes with the right exec
//     bit, symlinks, chmod for pure mode changes; missing parents are created.
// Ignored paths are never touched. Before changing anything, every target
// path is checked: an untracked (ignored) file or a non-empty directory in
// the way aborts the whole operation with an Error.
void materialize_tree(Repository& repo, const ObjectId& target_tree, const ObjectId& current_tree);

}  // namespace tva::core
