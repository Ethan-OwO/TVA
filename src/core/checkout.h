#pragma once

#include <optional>
#include <string_view>

#include "core/object_id.h"
#include "core/repository.h"

namespace tva::core {

// Tree of the commit `ref` resolves to (symbolic refs such as HEAD are
// followed); nullopt if the ref does not exist, e.g. an unborn branch.
std::optional<ObjectId> ref_tree(const Repository& repo, std::string_view ref);

// What moving HEAD to a branch whose tree is `target_tree` does to the
// worktree.
struct CheckoutPlan {
  ObjectId target_tree;
  // The worktree as it is now when it has to be rewritten; nullopt leaves
  // the worktree alone.
  std::optional<ObjectId> current_tree;
};

// When `target_tree` equals HEAD's tree the worktree is left alone, so
// unsnapshotted changes carry over (like `git switch`). Otherwise the
// worktree is snapshotted and must equal HEAD's tree: unsnapshotted changes
// throw Error("worktree has unsnapshotted changes; ...") before anything is
// touched, unless `force`, which discards them (their objects stay in the
// object database). `force` also rewrites a dirty worktree whose target tree
// equals HEAD's.
CheckoutPlan plan_checkout(Repository& repo, const ObjectId& target_tree, bool force);

// Rewrites the worktree as planned, then points HEAD at `branch_ref`
// (worktree first, HEAD second, as git does).
void apply_checkout(Repository& repo, const CheckoutPlan& plan, std::string_view branch_ref);

// plan_checkout + apply_checkout for an existing or unborn branch.
void switch_branch(Repository& repo, std::string_view branch_ref, bool force);

}  // namespace tva::core
