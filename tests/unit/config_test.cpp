#include "core/config.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "test_util.h"

namespace tva::core {
namespace {

Config parse(std::string_view text) {
  Config c;
  c.add_text(text);
  return c;
}

TEST(Config, SectionsAndKeys) {
  auto c = parse("[user]\n\tname = Ada Lovelace\n\temail=ada@example.com\n");
  EXPECT_EQ(c.get("user", "name"), "Ada Lovelace");
  EXPECT_EQ(c.get("user", "email"), "ada@example.com");
  EXPECT_FALSE(c.get("user", "signingkey"));
  EXPECT_FALSE(c.get("core", "name"));
}

TEST(Config, CaseInsensitiveNamesButNotSubsections) {
  auto c = parse("[User]\nNAME = x\n[remote \"Origin\"]\nURL = https://example.com\n");
  EXPECT_EQ(c.get("USER", "name"), "x");
  EXPECT_EQ(c.get("remote.Origin", "url"), "https://example.com");
  EXPECT_FALSE(c.get("remote.origin", "url"));
}

TEST(Config, CommentsQuotesAndWhitespace) {
  auto c = parse(
      "# full-line comment\n"
      "; another\n"
      "[core]   # after a header\n"
      "  a = value # trailing comment\n"
      "  b = \"  quoted # not a comment  \"\n"
      "  c = two   words   \n"
      "  d = esc\\\"aped\\\\ \\ttab\n"
      "  e\n"
      "  f =\n"
      "  g = mixed\" quoted \"part\r\n");
  EXPECT_EQ(c.get("core", "a"), "value");
  EXPECT_EQ(c.get("core", "b"), "  quoted # not a comment  ");
  EXPECT_EQ(c.get("core", "c"), "two   words");
  EXPECT_EQ(c.get("core", "d"), "esc\"aped\\ \ttab");
  EXPECT_EQ(c.get("core", "e"), "true");
  EXPECT_EQ(c.get("core", "f"), "");
  EXPECT_EQ(c.get("core", "g"), "mixed quoted part");
}

TEST(Config, LaterValuesWin) {
  auto c = parse("[user]\nname = first\n[user]\nname = second\n");
  EXPECT_EQ(c.get("user", "name"), "second");
}

TEST(Config, LoadFilesInOrder) {
  test::TempDir tmp;
  test::write_text(tmp.path() / "global", "[user]\nname = Global\nemail = g@example.com\n");
  test::write_text(tmp.path() / "repo", "[user]\nname = Repo\n");
  auto c = Config::load_files({tmp.path() / "missing", tmp.path() / "global", tmp.path() / "repo"});
  EXPECT_EQ(c.get("user", "name"), "Repo");
  EXPECT_EQ(c.get("user", "email"), "g@example.com");
}

TEST(Config, MalformedThrows) {
  EXPECT_THROW(parse("[unterminated\n"), Error);
  EXPECT_THROW(parse("key = outside section\n"), Error);
  EXPECT_THROW(parse("[s]\nk = \"open quote\n"), Error);
  EXPECT_THROW(parse("[s]\nk = bad \\q escape\n"), Error);
  EXPECT_THROW(parse("[s]\n1key = x\n"), Error);
}

}  // namespace
}  // namespace tva::core
