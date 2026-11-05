# Shared helpers for the integration tests; `source` it from a test script.
# Real git is the oracle: tests check that git reads what tva writes and that
# both produce identical object ids for identical input.
set -euo pipefail

: "${TVA_BIN:?TVA_BIN must point at the tva executable}"
TVA_BIN="$(cd "$(dirname "$TVA_BIN")" && pwd)/$(basename "$TVA_BIN")"

TEST_TMP="$(cd "$(mktemp -d "${TMPDIR:-/tmp}/tva-it.XXXXXX")" && pwd -P)"
# Set TVA_KEEP_TMP=1 to keep the scratch directory for debugging.
trap '[[ "${TVA_KEEP_TMP:-0}" == 1 ]] && echo "kept $TEST_TMP" || rm -rf "$TEST_TMP"' EXIT

# Isolate both tools from the machine's git configuration (signing, hooks,
# autocrlf, global excludes...) -- any of those would change object ids.
export HOME="$TEST_TMP/home"
export XDG_CONFIG_HOME="$HOME/.config"
export GIT_CONFIG_NOSYSTEM=1
export LC_ALL=C
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY \
  GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE \
  GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE
mkdir -p "$HOME"
cat >"$HOME/.gitconfig" <<'CFG'
[user]
	name = TVA Test
	email = tva-test@example.com
[init]
	defaultBranch = main
CFG

tva() { "$TVA_BIN" "$@"; }

CURRENT_STEP="setup"
step() {
  CURRENT_STEP="$1"
  echo "--- $1"
}

fail() {
  echo "FAIL [$CURRENT_STEP]: $*" >&2
  exit 1
}

# assert_eq <expected> <actual>  (expected = git's value when comparing)
assert_eq() {
  [[ "$1" == "$2" ]] || fail "expected $1 got $2"
}

# Runs a command that must fail with the given exit code.
assert_exit() {
  local expected="$1"
  shift
  local code=0
  "$@" >/dev/null 2>&1 || code=$?
  [[ "$code" == "$expected" ]] || fail "expected exit code $expected got $code from: $*"
}

# Prints one line per path below the current directory (excluding .git):
# type, permissions and content (or link target). Two equal fingerprints mean
# nothing in the worktree changed.
worktree_fingerprint() {
  find . -path ./.git -prune -o -mindepth 1 -printf '%P %y %m\n' | sort | while read -r path type mode; do
    case "$type" in
      f) echo "$path f $mode $(sha1sum <"$path" | cut -d' ' -f1)" ;;
      l) echo "$path l $(readlink "$path")" ;;
      *) echo "$path $type" ;;
    esac
  done
}
