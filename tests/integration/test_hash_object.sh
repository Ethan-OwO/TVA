#!/usr/bin/env bash
# hash-object / cat-file: tva computes git's ids and writes objects git reads.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

cd "$TEST_TMP"
step "init"
out="$(tva init repo)"
assert_eq "Initialized empty TVA repository in $TEST_TMP/repo/.git/" "$out"
cd repo
assert_eq "ref: refs/heads/main" "$(cat .git/HEAD)"
assert_eq ".tva/" "$(cat .git/info/exclude)"
[[ -d .tva ]] || fail ".tva/ was not created"
assert_exit 128 tva init .

step "hash-object text file"
printf 'hello world\n' >a.txt
assert_eq "$(git hash-object a.txt)" "$(tva hash-object a.txt)"

step "hash-object -w is readable by git"
id="$(tva hash-object -w a.txt)"
assert_eq "hello world" "$(git cat-file -p "$id")"
assert_eq "blob" "$(git cat-file -t "$id")"

step "empty file"
: >empty
id="$(tva hash-object -w empty)"
assert_eq "$(git hash-object empty)" "$id"
assert_eq "0" "$(git cat-file -s "$id")"

step "1 MiB binary file"
head -c 1048576 /dev/urandom >blob.bin
id="$(tva hash-object -w blob.bin)"
assert_eq "$(git hash-object blob.bin)" "$id"
git cat-file -p "$id" | cmp -s - blob.bin || fail "git read back different bytes"

step "tva cat-file"
assert_eq "blob" "$(tva cat-file -t "$id")"
assert_eq "1048576" "$(tva cat-file -s "$id")"
tva cat-file -p "${id:0:8}" | cmp -s - blob.bin || fail "tva read back different bytes"

step "tva reads objects written by git"
printf 'from git\n' >g.txt
gid="$(git hash-object -w g.txt)"
assert_eq "from git" "$(tva cat-file -p "$gid")"

step "-t types and errors"
assert_eq "$(git hash-object -t tree empty)" "$(tva hash-object -t tree empty)"
assert_exit 128 tva hash-object -t tag empty
assert_exit 128 tva hash-object -t bogus empty
assert_exit 128 tva cat-file -p 0123456789012345678901234567890123456789
assert_exit 129 tva cat-file -t -p "$id"
assert_exit 129 tva cat-file "$id"

echo "PASS"
