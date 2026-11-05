#!/usr/bin/env bash
# snapshot / log / rev-parse / commit-tree: with the same identities and
# dates, tva and git produce identical commit ids, and git reads tva history.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

export GIT_AUTHOR_NAME="Ada Lovelace" GIT_AUTHOR_EMAIL="ada@example.com"
export GIT_COMMITTER_NAME="Charles Babbage" GIT_COMMITTER_EMAIL="charles@example.com"

# Applies the same edit for commit number $1 in the current directory.
edit() {
  case "$1" in
    1) echo "v1" >file.txt && mkdir -p src && echo 'int main() {}' >src/main.c ;;
    2) echo "v2" >file.txt && printf '#!/bin/sh\n' >src/run.sh && chmod +x src/run.sh ;;
    3) rm src/main.c && mkdir -p docs/deep && echo doc >docs/deep/a.md && ln -s file.txt link ;;
  esac
}
MESSAGES=("first" $'second\n\nwith a body' "third")

cd "$TEST_TMP"
mkdir tva-repo git-repo
(cd tva-repo && tva init >/dev/null)
(cd git-repo && git init -q)

step "three snapshots, mirrored with git commit"
for i in 1 2 3; do
  export GIT_AUTHOR_DATE="$((1700000000 + i * 1000)) +0800"
  export GIT_COMMITTER_DATE="$((1700000000 + i * 1000 + 7)) -0530"
  msg="${MESSAGES[$((i - 1))]}"
  out="$(cd tva-repo && edit "$i" && tva snapshot -m "$msg")"
  short="$(cd tva-repo && tva rev-parse HEAD | cut -c1-7)"
  assert_eq "[main $short] $(head -n1 <<<"$msg")" "$out"
  (cd git-repo && edit "$i" && git add -A && git commit -q -m "$msg")
done

step "HEAD ids match"
assert_eq "$(cd git-repo && git rev-parse HEAD)" "$(cd tva-repo && tva rev-parse HEAD)"

cd tva-repo
step "tva log --oneline matches git log --format=%h"
assert_eq "$(git log --format=%h)" "$(tva log --oneline | cut -d' ' -f1)"
assert_eq "$(git log --oneline)" "$(tva log --oneline)"

step "tva log matches git log"
assert_eq "$(git log)" "$(tva log)"
assert_eq "$(git log HEAD~1)" "$(tva log HEAD~1)"

step "rev-parse"
for rev in HEAD HEAD~1 HEAD^ HEAD~2 main refs/heads/main HEAD^^ "$(git rev-parse --short HEAD)"; do
  assert_eq "$(git rev-parse "$rev")" "$(tva rev-parse "$rev")"
done
assert_exit 128 tva rev-parse HEAD~3
assert_exit 128 tva rev-parse no-such-branch

step "cat-file on commits"
assert_eq "$(git cat-file -p HEAD)" "$(tva cat-file -p HEAD)"
assert_eq "commit" "$(tva cat-file -t HEAD)"
assert_eq "$(git cat-file -s HEAD)" "$(tva cat-file -s HEAD)"

step "nothing changed"
before="$(tva rev-parse HEAD)"
assert_eq "nothing changed" "$(tva snapshot -m noop)"
assert_eq "$before" "$(tva rev-parse HEAD)"

step "commit-tree matches git commit-tree"
tree="$(tva write-tree)"
assert_eq "$(git commit-tree "$tree" -p HEAD -p HEAD~1 -m one -m two)" \
  "$(tva commit-tree "$tree" -p HEAD -p HEAD~1 -m one -m two)"
assert_eq "$before" "$(tva rev-parse HEAD)"  # commit-tree moves no ref
assert_exit 128 tva commit-tree HEAD -m "not a tree"
assert_exit 129 tva commit-tree "$tree"

step "identity comes from config when env is unset"
unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL
id="$(tva commit-tree "$tree" -m cfg)"
assert_eq "TVA Test <tva-test@example.com>" "$(git log -1 --format='%an <%ae>' "$id")"
HOME="$TEST_TMP/nobody" assert_exit 128 tva commit-tree "$tree" -m "no identity"

step "log on an unborn branch fails"
cd "$TEST_TMP" && mkdir empty && cd empty && tva init >/dev/null
assert_exit 128 tva log

echo "PASS"
