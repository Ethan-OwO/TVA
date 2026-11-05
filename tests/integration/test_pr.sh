#!/usr/bin/env bash
# pr open / list / switch / show: a PR is the branch refs/heads/pr/<slug>
# whose first commit (the "open commit") stores the intent in git-native
# form, so real git -- and GitHub -- read PRs as ordinary branches.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

export GIT_AUTHOR_NAME="Ada Lovelace" GIT_AUTHOR_EMAIL="ada@example.com"
export GIT_AUTHOR_DATE="1700000000 +0800"
export GIT_COMMITTER_NAME="Charles Babbage" GIT_COMMITTER_EMAIL="charles@example.com"
export GIT_COMMITTER_DATE="1700000500 -0530"

short() { git rev-parse "$1" | cut -c1-7; }

cd "$TEST_TMP"
tva init repo >/dev/null
cd repo

step "main gets a first commit"
echo 'v1' >app.txt
mkdir -p src && echo 'int main() {}' >src/main.c
tva snapshot -m "init" >/dev/null
main="$(git rev-parse main)"
assert_eq "no open PRs" "$(tva pr list)"

step "pr open writes the open commit and moves HEAD"
intent=$'Add login page\n\nUse OAuth for the login flow.\n\nTests included.'
assert_eq "Opened PR add-login-page: Add login page" "$(tva pr open "$intent")"
assert_eq "refs/heads/pr/add-login-page" "$(git symbolic-ref HEAD)"
assert_eq "$main" "$(git rev-parse HEAD^)"
assert_eq "$(git rev-parse 'main^{tree}')" "$(git rev-parse 'HEAD^{tree}')"
assert_eq "Add login page" "$(git log -1 --format=%s)"
assert_eq "TVA-PR-Open: add-login-page" "$(git log -1 --format=%B | git interpret-trailers --parse)"
assert_eq "$intent"$'\n\nTVA-PR-Open: add-login-page' "$(git log -1 --format=%B)"
assert_eq "clean" "$(tva status)"

step "moments land on the PR branch"
echo 'login' >login.txt
tva snapshot -m "first moment" >/dev/null
echo 'v2' >app.txt
tva snapshot -m "second moment" >/dev/null
login_tree="$(tva write-tree)"
assert_eq "$main" "$(git rev-parse main)"  # the Sacred Timeline did not move
assert_eq "3" "$(git rev-list --count main..pr/add-login-page)"

step "pr show"
expected="PR add-login-page (current)
Title: Add login page
Base:  $(short main)
Head:  $(short HEAD)

    Use OAuth for the login flow.

    Tests included.

Moments: 2
$(short HEAD) second moment
$(short HEAD~1) first moment"
assert_eq "$expected" "$(tva pr show)"
assert_eq "$expected" "$(tva pr show add-login-page)"
assert_eq "$expected" "$(tva pr show pr/add-login-page)"

step "slugs from Chinese titles; opening from a diverged PR rewrites the worktree"
assert_eq "Opened PR pr: 做登入功能" "$(tva pr open '做登入功能')"
assert_eq "$(git rev-parse 'main^{tree}')" "$(tva write-tree)"
[[ ! -e login.txt ]] || fail "login.txt should be gone on a PR based on main"
assert_eq "v1" "$(cat app.txt)"
assert_eq "Opened PR pr-2: 做登入功能" "$(tva pr open '做登入功能')"
assert_eq "Opened PR custom: whatever" "$(tva pr open whatever --slug custom)"
assert_exit 128 tva pr open again --slug custom      # taken
assert_exit 128 tva pr open again --slug Bad_Slug    # invalid
assert_exit 128 tva pr open again --slug main        # reserved
assert_exit 128 tva pr open $'  \n'                  # no intent

step "pr list"
expected="  add-login-page  Add login page (2 moments)
* custom          whatever (0 moments)
  pr              做登入功能 (0 moments)
  pr-2            做登入功能 (0 moments)"
assert_eq "$expected" "$(tva pr list)"

step "pr switch rewrites the worktree"
assert_eq "Switched to PR add-login-page" "$(tva pr switch add-login-page)"
assert_eq "refs/heads/pr/add-login-page" "$(git symbolic-ref HEAD)"
assert_eq "$login_tree" "$(tva write-tree)"
assert_eq "Already on PR add-login-page" "$(tva pr switch pr/add-login-page)"
assert_eq "Switched to main" "$(tva pr switch main)"
assert_eq "refs/heads/main" "$(git symbolic-ref HEAD)"
assert_eq "$(git rev-parse 'main^{tree}')" "$(tva write-tree)"
assert_eq "Already on main" "$(tva pr switch main)"
assert_exit 128 tva pr switch no-such-pr
assert_exit 128 tva pr show no-such-pr
assert_exit 128 tva pr show  # not on a PR
assert_exit 129 tva pr

step "unsnapshotted changes block a switch that rewrites the worktree"
echo 'precious' >app.txt
before="$(worktree_fingerprint)"
assert_exit 128 tva pr switch add-login-page
assert_eq "$before" "$(worktree_fingerprint)"
assert_eq "refs/heads/main" "$(git symbolic-ref HEAD)"

step "pr open on main carries the changes into the new PR"
assert_eq "Opened PR keep-my-edit: Keep my edit" "$(tva pr open 'Keep my edit')"
assert_eq "precious" "$(cat app.txt)"
assert_eq "M app.txt" "$(tva status)"
tva snapshot -m "manual edit" >/dev/null
assert_eq "precious" "$(git show pr/keep-my-edit:app.txt)"

step "--force discards unsnapshotted changes"
echo 'scratch' >app.txt
tva pr switch --force add-login-page >/dev/null
assert_eq "$login_tree" "$(tva write-tree)"

step "pr open from a diverged PR needs a clean worktree"
echo 'scratch' >login.txt
before="$(worktree_fingerprint)"
assert_exit 128 tva pr open "Blocked"
assert_eq "$before" "$(worktree_fingerprint)"
assert_eq "" "$(git for-each-ref refs/heads/pr/blocked)"  # no half-opened PR
assert_eq "refs/heads/pr/add-login-page" "$(git symbolic-ref HEAD)"
assert_eq "Opened PR blocked: Blocked" "$(tva pr open --force "Blocked")"
assert_eq "$(git rev-parse 'main^{tree}')" "$(tva write-tree)"

step "git reads everything"
code=0
output="$(git fsck --full --strict 2>&1)" || code=$?
assert_eq "0" "$code"
if grep -Eiq 'error|warning|fatal|missing|broken|bad' <<<"$output"; then
  echo "$output"
  fail "fsck reported problems"
fi
assert_eq "pr/add-login-page
pr/blocked
pr/custom
pr/keep-my-edit
pr/pr
pr/pr-2" "$(git for-each-ref --format='%(refname:short)' refs/heads/pr)"

step "a PR opened on an empty main is a root commit with the empty tree"
cd "$TEST_TMP" && tva init fresh >/dev/null && cd fresh
echo 'already here' >notes.txt
assert_eq "Opened PR first-steps: First steps" "$(tva pr open 'First steps')"
assert_eq "" "$(git log -1 --format=%P)"
assert_eq "4b825dc642cb6eb9a060e54bf8d69288fbee4904" "$(git rev-parse 'HEAD^{tree}')"
assert_eq "already here" "$(cat notes.txt)"
tva snapshot -m "notes" >/dev/null
assert_eq "Switched to main" "$(tva pr switch main)"
[[ ! -e notes.txt ]] || fail "main is empty, so notes.txt should be gone"
assert_eq "Switched to PR first-steps" "$(tva pr switch first-steps)"
assert_eq "already here" "$(cat notes.txt)"
git fsck --full --strict >/dev/null 2>&1 || fail "fsck failed on the fresh repo"

echo "PASS"
