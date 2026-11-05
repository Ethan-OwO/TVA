#include "core/commit.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "core/object.h"

namespace tva::core {
namespace {

Commit sample() {
  Commit c;
  c.tree = *ObjectId::from_hex("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
  c.parents = {*ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8dad"),
               *ObjectId::from_hex("e69de29bb2d1d6434b8b29ae775ad8c2e48c5391")};
  c.author = {"Ada Lovelace", "ada@example.com", 1700000000, 480};
  c.committer = {"Charles Babbage", "cb@example.com", 1700000100, -330};
  c.message = "subject line\n\nbody\n";
  return c;
}

TEST(Signature, FormatAndParse) {
  Signature s{"Ada Lovelace", "ada@example.com", 1700000000, 480};
  EXPECT_EQ(format_signature(s), "Ada Lovelace <ada@example.com> 1700000000 +0800");
  EXPECT_EQ(parse_signature(format_signature(s)), s);

  Signature west{"W", "w@x", 1, -330};
  EXPECT_EQ(format_signature(west), "W <w@x> 1 -0530");
  EXPECT_EQ(parse_signature("W <w@x> 1 -0530"), west);
  EXPECT_EQ(format_tz(0), "+0000");

  EXPECT_THROW(parse_signature("no email 1 +0000"), Error);
  EXPECT_THROW(parse_signature("A <a@b> notanumber +0000"), Error);
  EXPECT_THROW(parse_signature("A <a@b> 1 +08"), Error);
  EXPECT_THROW(parse_signature("A <a@b> 1"), Error);
}

TEST(Commit, SerializeFormat) {
  Commit c = sample();
  EXPECT_EQ(std::string(as_chars(serialize_commit(c))),
            "tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904\n"
            "parent 3b18e512dba79e4c8300dd08aeb37f8e728b8dad\n"
            "parent e69de29bb2d1d6434b8b29ae775ad8c2e48c5391\n"
            "author Ada Lovelace <ada@example.com> 1700000000 +0800\n"
            "committer Charles Babbage <cb@example.com> 1700000100 -0530\n"
            "\n"
            "subject line\n\nbody\n");
}

TEST(Commit, MessageGetsTrailingNewline) {
  Commit c = sample();
  c.message = "no newline";
  EXPECT_TRUE(std::string(as_chars(serialize_commit(c))).ends_with("\n\nno newline\n"));
  // Like `git commit-tree -m ""`, an empty message stays empty.
  c.message = "";
  EXPECT_TRUE(std::string(as_chars(serialize_commit(c))).ends_with("-0530\n\n"));
}

TEST(Commit, RoundTrip) {
  Commit c = sample();
  EXPECT_EQ(parse_commit(serialize_commit(c)), c);
  c.parents.clear();
  EXPECT_EQ(parse_commit(serialize_commit(c)), c);
}

TEST(Commit, IdMatchesGit) {
  // GIT_AUTHOR_NAME=A GIT_AUTHOR_EMAIL=a@x GIT_AUTHOR_DATE="1700000000 +0000"
  // (same for committer) git commit-tree 4b825dc... -m hello
  Commit c;
  c.tree = *ObjectId::from_hex("4b825dc642cb6eb9a060e54bf8d69288fbee4904");
  c.author = c.committer = {"A", "a@x", 1700000000, 0};
  c.message = "hello";
  EXPECT_EQ(hash_object(ObjectType::Commit, serialize_commit(c)).hex(),
            "3c641794810e05e85d5676a95115ce50e2b5de7d");
}

TEST(Commit, ParseSkipsUnknownHeaders) {
  const std::string raw =
      "tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904\n"
      "author A <a@x> 1700000000 +0000\n"
      "committer A <a@x> 1700000000 +0000\n"
      "encoding ISO-8859-1\n"
      "gpgsig -----BEGIN PGP SIGNATURE-----\n"
      " \n"
      " iQEzBAABCAAdFiEE\n"
      " -----END PGP SIGNATURE-----\n"
      "\n"
      "signed\n";
  Commit c = parse_commit(as_bytes(raw));
  EXPECT_EQ(c.tree.hex(), "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
  EXPECT_TRUE(c.parents.empty());
  EXPECT_EQ(c.author.name, "A");
  EXPECT_EQ(c.message, "signed\n");
}

TEST(Commit, ParseRejectsMalformed) {
  EXPECT_THROW(parse_commit(as_bytes("author A <a@x> 1 +0000\n\nmsg")), Error);
  EXPECT_THROW(parse_commit(as_bytes("tree nothex\nauthor A <a@x> 1 +0000\n"
                                     "committer A <a@x> 1 +0000\n\n")),
               Error);
}

TEST(Commit, SerializeRejectsDelimitersInIdentity) {
  Commit c = sample();
  c.author.name = "Evil\nparent 0000";
  EXPECT_THROW(serialize_commit(c), Error);
}

TEST(Commit, Subject) {
  EXPECT_EQ(commit_subject("one\n"), "one");
  EXPECT_EQ(commit_subject("\n\n  first\nsecond  \n\nbody\n"), "  first second");
  EXPECT_EQ(commit_subject(""), "");
}

TEST(Commit, GitDateFormat) {
  // `git log` shows these for "1700000000 +0800" / "-0530" / "+0000".
  EXPECT_EQ(format_git_date(1700000000, 480), "Wed Nov 15 06:13:20 2023 +0800");
  EXPECT_EQ(format_git_date(1700000000, -330), "Tue Nov 14 16:43:20 2023 -0530");
  EXPECT_EQ(format_git_date(0, 0), "Thu Jan 1 00:00:00 1970 +0000");
  EXPECT_EQ(format_git_date(951782400, 0), "Tue Feb 29 00:00:00 2000 +0000");
  EXPECT_EQ(format_git_date(-1, 0), "Wed Dec 31 23:59:59 1969 +0000");
}

}  // namespace
}  // namespace tva::core
