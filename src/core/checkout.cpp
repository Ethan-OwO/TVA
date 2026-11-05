#include "core/checkout.h"

#include "core/error.h"
#include "core/materialize.h"
#include "core/object.h"
#include "core/snapshot.h"
#include "core/tree.h"

namespace tva::core {

std::optional<ObjectId> ref_tree(const Repository& repo, std::string_view ref) {
  auto id = repo.refs().resolve(ref);
  if (!id) return std::nullopt;
  return repo.odb().read_commit(*id).tree;
}

CheckoutPlan plan_checkout(Repository& repo, const ObjectId& target_tree, bool force) {
  const ObjectId head = ref_tree(repo, "HEAD").value_or(empty_tree_id());
  if (target_tree == head && !force) return {target_tree, std::nullopt};
  // The snapshot is also the safety net for --force: whatever gets discarded
  // is still in the object database afterwards.
  const ObjectId current = snapshot_worktree(repo);
  if (current != head && !force) {
    throw Error("worktree has unsnapshotted changes; run tva snapshot or use --force");
  }
  if (current == target_tree) return {target_tree, std::nullopt};
  return {target_tree, current};
}

void apply_checkout(Repository& repo, const CheckoutPlan& plan, std::string_view branch_ref) {
  if (plan.current_tree) {
    // An unborn target stands for the empty tree, which may not be stored yet.
    if (plan.target_tree == empty_tree_id()) repo.odb().write_tree(Tree{});
    materialize_tree(repo, plan.target_tree, *plan.current_tree);
  }
  repo.refs().set_symbolic("HEAD", branch_ref);
}

void switch_branch(Repository& repo, std::string_view branch_ref, bool force) {
  const ObjectId target = ref_tree(repo, branch_ref).value_or(empty_tree_id());
  apply_checkout(repo, plan_checkout(repo, target, force), branch_ref);
}

}  // namespace tva::core
