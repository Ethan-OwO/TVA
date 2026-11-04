#!/usr/bin/env bash
# write-tree: snapshotting a directory gives the same tree id as
# `git add -A && git write-tree` on an identical copy.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

cd "$TEST_TMP"
step "build fixture"
mkdir -p fixture && cd fixture
mkdir -p src/core src/sub docs/guide bin a empty/nested build/obj x/y/tmp onlyignored
echo 'int main() {}' >src/main.c
echo 'core' >src/core/core.c
echo 'sub' >src/sub/local.txt   # not matched by src/.gitignore's anchored /local.txt
echo 'local' >src/local.txt     # ignored by src/.gitignore
echo 'gen' >src/foo.gen         # ignored by src/.gitignore
echo 'special' >src/special.gen # re-included by src/.gitignore
echo 'guide' >docs/guide/intro.md
echo 'bak' >docs/guide/old.bak  # docs/**/*.bak
echo 'bak' >docs/old.bak        # docs/**/*.bak (** matches zero dirs)
printf '#!/bin/sh\necho run\n' >bin/run.sh
chmod +x bin/run.sh
echo 'file' >a-b                # sort order: a-b < a.txt < a/
echo 'file' >a.txt
echo 'in dir' >a/c
echo 'spaces' >'with space.txt'
echo 'hash' >'#hash'            # ignored via \#hash
echo 'log' >debug.log           # *.log
echo 'keep' >keep.log           # !keep.log
echo 'root' >rootonly.txt       # /rootonly.txt
echo 'nested' >src/rootonly.txt # anchored pattern does not apply here
echo 'obj' >build/out.o
echo 'obj' >build/obj/x.o
echo 'tmp' >x/y/tmp/t           # **/tmp
echo 'log' >onlyignored/a.log   # directory with only ignored files: no entry
ln -s src/main.c link-to-file
ln -s src link-to-dir
ln -s does/not/exist dangling
printf '%s\n' '*.log' '!keep.log' 'build/' '**/tmp' 'docs/**/*.bak' '/rootonly.txt' '\#hash' >.gitignore
printf '%s\n' '/local.txt' '*.gen' '!special.gen' >src/.gitignore
cd ..
cp -a fixture A
cp -a fixture B

step "tva write-tree"
(cd A && tva init >/dev/null)
tva_tree="$(cd A && tva write-tree)"

step "git write-tree"
git_tree="$(cd B && git init -q && git add -A && git write-tree)"

step "tree ids match"
assert_eq "$git_tree" "$tva_tree"

step "git can read tva's trees"
(cd A && git ls-tree -r "$tva_tree" >/dev/null) || fail "git ls-tree failed on tva repo"
assert_eq "$(cd B && git cat-file -p "$git_tree")" "$(cd A && tva cat-file -p "$tva_tree")"

step "ignored paths are absent"
listing="$(cd A && git ls-tree -r --name-only "$tva_tree")"
for p in build/out.o debug.log src/local.txt src/foo.gen docs/old.bak x/y/tmp/t '#hash' \
  rootonly.txt onlyignored/a.log .tva; do
  if grep -qxF "$p" <<<"$listing"; then fail "$p should be ignored"; fi
done
for p in keep.log src/special.gen src/sub/local.txt src/rootonly.txt link-to-dir bin/run.sh; do
  grep -qxF "$p" <<<"$listing" || fail "$p should be tracked"
done

step "write-tree is stable"
assert_eq "$tva_tree" "$(cd A && tva write-tree)"

step "empty worktree gives the empty tree"
mkdir empty-repo && cd empty-repo && tva init >/dev/null
assert_eq "4b825dc642cb6eb9a060e54bf8d69288fbee4904" "$(tva write-tree)"

echo "PASS"
