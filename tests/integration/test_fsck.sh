#!/usr/bin/env bash
# git fsck --full --strict accepts everything tva writes: blobs of every
# kind, nested trees, executables, symlinks, merges and multi-line messages.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

export GIT_AUTHOR_NAME="Ada Lovelace" GIT_AUTHOR_EMAIL="ada@example.com"
export GIT_AUTHOR_DATE="1700000000 +0800"
export GIT_COMMITTER_NAME="Charles Babbage" GIT_COMMITTER_EMAIL="charles@example.com"
export GIT_COMMITTER_DATE="1700000500 -0530"

cd "$TEST_TMP"
tva init repo >/dev/null
cd repo

step "build history"
mkdir -p src/core docs/deep/er empty
echo 'int main() {}' >src/main.c
echo 'core' >src/core/x.c
: >empty-file
head -c 65536 /dev/urandom >binary.bin
printf 'caf\xc3\xa9\n' >"$(printf 'utf8-\xc3\xa9.txt')"
echo 'spaces' >'name with spaces'
printf '#!/bin/sh\n' >run.sh && chmod +x run.sh
ln -s src/main.c link
ln -s ../.. up-link
echo '*.o' >.gitignore && echo obj >ignored.o
tva snapshot -m "initial" >/dev/null
echo doc >docs/deep/er/notes.md && rm src/core/x.c
tva snapshot -m $'second\n\nA longer body\nwith several lines.\n' >/dev/null
echo v3 >src/main.c
tva snapshot -m "third" >/dev/null
tree="$(tva write-tree)"
merge="$(tva commit-tree "$tree" -p HEAD -p HEAD~1 -m "merge-shaped commit")"
echo "$merge" >.git/refs/heads/merged

step "git fsck --full --strict"
code=0
output="$(git fsck --full --strict 2>&1)" || code=$?
echo "$output"
assert_eq "0" "$code"
if grep -Eiq 'error|warning|fatal|missing|broken|bad' <<<"$output"; then
  fail "fsck reported problems"
fi

step "git reads the history"
assert_eq "4" "$(git rev-list --count merged)"
assert_eq "merge-shaped commit" "$(git log -1 --format=%s merged)"
git log --stat >/dev/null

echo "PASS"
