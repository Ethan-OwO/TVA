#include "core/ignore.h"

#include <gtest/gtest.h>

#include "test_util.h"

namespace tva::core {
namespace {

// Matcher with a single root .gitignore.
IgnoreMatcher root(std::string_view text) {
  IgnoreMatcher m;
  m.add_patterns("", text);
  return m;
}

TEST(GlobMatch, Basics) {
  EXPECT_TRUE(glob_match("foo", "foo"));
  EXPECT_FALSE(glob_match("foo", "foobar"));
  EXPECT_TRUE(glob_match("*.c", "main.c"));
  EXPECT_TRUE(glob_match("*", ""));
  EXPECT_FALSE(glob_match("?", ""));
  EXPECT_TRUE(glob_match("\\*", "*"));
  EXPECT_FALSE(glob_match("\\*", "x"));
}

TEST(GlobMatch, StarAndQuestionDoNotCrossSlash) {
  EXPECT_TRUE(glob_match("a/*.c", "a/x.c"));
  EXPECT_FALSE(glob_match("a/*.c", "a/b/x.c"));
  EXPECT_FALSE(glob_match("a?b", "a/b"));
  EXPECT_FALSE(glob_match("a[/]b", "a/b"));
  EXPECT_TRUE(glob_match("*/b", "a/b"));
  EXPECT_FALSE(glob_match("*/b", "a/c/b"));
}

TEST(GlobMatch, BracketExpressions) {
  EXPECT_TRUE(glob_match("[abc]", "b"));
  EXPECT_FALSE(glob_match("[abc]", "d"));
  EXPECT_TRUE(glob_match("[a-z]x", "qx"));
  EXPECT_FALSE(glob_match("[a-z]x", "Qx"));
  EXPECT_TRUE(glob_match("[!a]", "b"));
  EXPECT_FALSE(glob_match("[!a]", "a"));
  EXPECT_TRUE(glob_match("[^a]", "b"));
  EXPECT_TRUE(glob_match("[]]", "]"));     // ']' first is a literal member
  EXPECT_TRUE(glob_match("[a-]", "-"));    // trailing '-' is literal
  EXPECT_TRUE(glob_match("[[:digit:]]x", "7x"));
  EXPECT_FALSE(glob_match("[[:digit:]]x", "ax"));
  EXPECT_FALSE(glob_match("[[:bogus:]]", "a"));  // malformed class never matches
  EXPECT_FALSE(glob_match("[abc", "a"));         // unterminated set never matches
}

TEST(GlobMatch, DoubleStar) {
  EXPECT_TRUE(glob_match("**/foo", "foo"));
  EXPECT_TRUE(glob_match("**/foo", "a/b/foo"));
  EXPECT_FALSE(glob_match("**/foo", "a/xfoo"));
  EXPECT_TRUE(glob_match("abc/**", "abc/x"));
  EXPECT_TRUE(glob_match("abc/**", "abc/x/y"));
  EXPECT_FALSE(glob_match("abc/**", "abc"));
  EXPECT_TRUE(glob_match("a/**/b", "a/b"));
  EXPECT_TRUE(glob_match("a/**/b", "a/x/y/b"));
  EXPECT_FALSE(glob_match("a/**/b", "a/xb"));
  // "**" that is not a whole path component behaves like "*".
  EXPECT_TRUE(glob_match("a**b", "axxb"));
  EXPECT_FALSE(glob_match("a**b", "a/b"));
}

TEST(Ignore, BlankLinesAndComments) {
  auto m = root("\n\n# *.o\n#hash\n\n*.tmp\n");
  EXPECT_FALSE(m.is_ignored("x.o", false));    // commented out
  EXPECT_FALSE(m.is_ignored("#hash", false));  // a comment, not a pattern
  EXPECT_TRUE(m.is_ignored("x.tmp", false));
}

TEST(Ignore, EscapedHashAndBang) {
  auto m = root("\\#hash\n\\!bang\n");
  EXPECT_TRUE(m.is_ignored("#hash", false));
  EXPECT_TRUE(m.is_ignored("!bang", false));
  EXPECT_FALSE(m.is_ignored("bang", false));
}

TEST(Ignore, TrailingSpaces) {
  auto m = root("foo   \nbar\\ \n");
  EXPECT_TRUE(m.is_ignored("foo", false));
  EXPECT_FALSE(m.is_ignored("foo ", false));
  EXPECT_TRUE(m.is_ignored("bar ", false));  // escaped space is kept
  EXPECT_FALSE(m.is_ignored("bar", false));
}

TEST(Ignore, CrlfLineEndings) {
  auto m = root("*.o\r\n!keep.o\r\n");
  EXPECT_TRUE(m.is_ignored("a.o", false));
  EXPECT_FALSE(m.is_ignored("keep.o", false));
}

TEST(Ignore, NegationAndLastMatchWins) {
  auto m = root("*.log\n!keep.log\n");
  EXPECT_TRUE(m.is_ignored("debug.log", false));
  EXPECT_FALSE(m.is_ignored("keep.log", false));
  auto reversed = root("!keep.log\n*.log\n");
  EXPECT_TRUE(reversed.is_ignored("keep.log", false));
}

TEST(Ignore, TrailingSlashMatchesDirectoriesOnly) {
  auto m = root("build/\n");
  EXPECT_TRUE(m.is_ignored("build", true));
  EXPECT_TRUE(m.is_ignored("src/build", true));
  EXPECT_FALSE(m.is_ignored("build", false));
}

TEST(Ignore, SlashAnchorsPattern) {
  auto m = root("doc/frotz\n/top\n");
  EXPECT_TRUE(m.is_ignored("doc/frotz", false));
  EXPECT_FALSE(m.is_ignored("a/doc/frotz", false));
  EXPECT_TRUE(m.is_ignored("top", false));
  EXPECT_FALSE(m.is_ignored("sub/top", false));
  // Without a slash, a pattern matches the basename at any depth.
  auto any = root("frotz\n");
  EXPECT_TRUE(any.is_ignored("a/b/frotz", false));
}

TEST(Ignore, StarDoesNotCrossDirectories) {
  auto m = root("src/*.o\n");
  EXPECT_TRUE(m.is_ignored("src/a.o", false));
  EXPECT_FALSE(m.is_ignored("src/sub/a.o", false));
}

TEST(Ignore, QuestionMarkAndBrackets) {
  auto m = root("?.txt\n[ab].c\n[!x]*.h\n[0-9].dat\n");
  EXPECT_TRUE(m.is_ignored("a.txt", false));
  EXPECT_FALSE(m.is_ignored("ab.txt", false));
  EXPECT_TRUE(m.is_ignored("b.c", false));
  EXPECT_FALSE(m.is_ignored("c.c", false));
  EXPECT_TRUE(m.is_ignored("y.h", false));
  EXPECT_FALSE(m.is_ignored("x.h", false));
  EXPECT_TRUE(m.is_ignored("5.dat", false));
  EXPECT_FALSE(m.is_ignored("x.dat", false));
}

TEST(Ignore, LeadingDoubleStar) {
  auto m = root("**/tmp\n");
  EXPECT_TRUE(m.is_ignored("tmp", true));
  EXPECT_TRUE(m.is_ignored("a/b/tmp", false));
  EXPECT_FALSE(m.is_ignored("a/tmpx", false));
}

TEST(Ignore, TrailingDoubleStar) {
  auto m = root("out/**\n!out/keep\n");
  EXPECT_FALSE(m.is_ignored("out", true));  // the directory itself is not ignored...
  EXPECT_TRUE(m.is_ignored("out/a", false));  // ...but everything inside is
  EXPECT_TRUE(m.is_ignored("out/x/y", false));
  EXPECT_FALSE(m.is_ignored("out/keep", false));  // so re-including works here
}

TEST(Ignore, MiddleDoubleStar) {
  auto m = root("docs/**/*.bak\n");
  EXPECT_TRUE(m.is_ignored("docs/a.bak", false));
  EXPECT_TRUE(m.is_ignored("docs/x/y/a.bak", false));
  EXPECT_FALSE(m.is_ignored("other/a.bak", false));
}

TEST(Ignore, IgnoredParentCannotBeReincluded) {
  auto m = root("build/\n!build/keep.txt\n");
  EXPECT_TRUE(m.is_ignored("build/keep.txt", false));
  EXPECT_TRUE(m.is_ignored("build/deep/file", false));
}

TEST(Ignore, NestedGitignoreIsRelativeToItsDirectory) {
  IgnoreMatcher m;
  m.add_patterns("", "*.log\n");
  m.add_patterns("src", "/local.txt\n!keep.log\n*.gen\n");
  EXPECT_TRUE(m.is_ignored("src/local.txt", false));
  EXPECT_FALSE(m.is_ignored("local.txt", false));
  EXPECT_FALSE(m.is_ignored("src/sub/local.txt", false));
  EXPECT_TRUE(m.is_ignored("src/sub/x.gen", false));
  EXPECT_FALSE(m.is_ignored("x.gen", false));
  // The deeper file overrides the root one.
  EXPECT_FALSE(m.is_ignored("src/keep.log", false));
  EXPECT_TRUE(m.is_ignored("keep.log", false));
}

TEST(Ignore, GitignoreOverridesInfoExclude) {
  IgnoreMatcher m;
  m.add_exclude_patterns("*.o\nsecret\n");
  m.add_patterns("", "!keep.o\n");
  EXPECT_TRUE(m.is_ignored("a.o", false));
  EXPECT_TRUE(m.is_ignored("dir/secret", false));
  EXPECT_FALSE(m.is_ignored("keep.o", false));
}

TEST(Ignore, GitAndTvaAreAlwaysIgnored) {
  IgnoreMatcher m;
  m.add_patterns("", "!.git\n!.tva/\n");
  EXPECT_TRUE(m.is_ignored(".git", true));
  EXPECT_TRUE(m.is_ignored(".git", false));
  EXPECT_TRUE(m.is_ignored("sub/.git/config", false));
  EXPECT_TRUE(m.is_ignored(".tva", true));
  EXPECT_TRUE(m.is_ignored("a/.tva/state", false));
  EXPECT_FALSE(m.is_ignored(".tva", false));  // only the directory is special
  EXPECT_FALSE(m.is_ignored(".gitignore", false));
}

TEST(Ignore, LoadsFilesFromWorktree) {
  test::TempDir tmp;
  test::write_text(tmp.path() / ".gitignore", "*.o\n");
  test::write_text(tmp.path() / "sub/.gitignore", "!keep.o\n");
  test::write_text(tmp.path() / "skip/.gitignore", "!never.o\n");
  test::write_text(tmp.path() / ".git/info/exclude", "skip/\n");
  auto m = IgnoreMatcher::load(tmp.path());
  EXPECT_TRUE(m.is_ignored("a.o", false));
  EXPECT_FALSE(m.is_ignored("sub/keep.o", false));
  EXPECT_TRUE(m.is_ignored("sub/other.o", false));
  EXPECT_TRUE(m.is_ignored("skip/never.o", false));  // .gitignore in an ignored dir is not read
}

}  // namespace
}  // namespace tva::core
