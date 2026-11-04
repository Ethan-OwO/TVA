#!/usr/bin/env bash
# status / restore: moving the worktree between snapshots touches only what
# differs, keeps ignored files, prunes emptied directories, and refuses to
# discard unsnapshotted work.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

cd "$TEST_TMP"
tva init repo >/dev/null
cd repo

step "snapshot A"
echo 'build/' >.gitignore
echo 'original' >modified.txt
echo 'doomed' >deleted.txt
printf '#!/bin/sh\n' >run.sh
mkdir -p src && echo 'int main() {}' >src/main.c
ln -s src/main.c link
tva snapshot -m A >/dev/null
commit_a="$(tva rev-parse HEAD)"
tree_a="$(tva write-tree)"
assert_eq "clean" "$(tva status)"

step "edit the worktree"
echo 'changed' >modified.txt
rm deleted.txt
mkdir -p nested/deeper && echo 'new' >nested/deeper/file.txt
chmod +x run.sh
mkdir -p build && echo 'object' >build/out.o
expected_status="$(printf '%s\n' 'D deleted.txt' 'M modified.txt' 'A nested/deeper/file.txt' 'T run.sh')"
assert_eq "$expected_status" "$(tva status)"

step "snapshot B"
tva snapshot -m B >/dev/null
tree_b="$(tva write-tree)"
assert_eq "clean" "$(tva status)"

step "restore A"
tva restore "$commit_a"
assert_eq "$tree_a" "$(tva write-tree)"
assert_eq "object" "$(cat build/out.o)"
[[ ! -e nested ]] || fail "emptied directory nested/ was not removed"
[[ ! -x run.sh ]] || fail "run.sh is still executable"
assert_eq "original" "$(cat modified.txt)"
assert_eq "doomed" "$(cat deleted.txt)"
assert_eq "src/main.c" "$(readlink link)"
assert_eq "$(tva rev-parse HEAD~1)" "$commit_a"  # restore does not move HEAD

step "restore B by tree id"
tva restore --force "$tree_b"  # HEAD is B; the worktree is A, so --force
assert_eq "$tree_b" "$(tva write-tree)"
assert_eq "clean" "$(tva status)"

step "unsnapshotted changes block restore"
echo 'precious work' >modified.txt
before="$(worktree_fingerprint)"
assert_exit 128 tva restore "$commit_a"
assert_eq "$before" "$(worktree_fingerprint)"
assert_eq "precious work" "$(cat modified.txt)"

step "--force discards them"
tva restore --force "$commit_a"
assert_eq "$tree_a" "$(tva write-tree)"

step "git agrees with the history"
assert_eq "$tree_a" "$(git rev-parse "$commit_a^{tree}")"
assert_eq "$tree_b" "$(git rev-parse "HEAD^{tree}")"

echo "PASS"
