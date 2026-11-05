#include "core/rev_parse.h"

#include <gtest/gtest.h>

#include <map>

#include "core/error.h"
#include "core/object.h"
#include "test_util.h"

namespace tva::core {
namespace {

class RevParseTest : public ::testing::Test {
 protected:
  RevParseTest() : repo_(Repository::init(tmp_.path() / "w")) {}

  ObjectId commit(std::vector<ObjectId> parents, const std::string& msg) {
    Commit c;
    c.tree = repo_.odb().write_tree(Tree{});
    c.parents = std::move(parents);
    c.author = c.committer = {"T", "t@x", 1700000000, 0};
    c.message = msg;
    return repo_.odb().write_commit(c);
  }

  test::TempDir tmp_;
  Repository repo_;
};

TEST_F(RevParseTest, NamesAndSuffixes) {
  EXPECT_THROW(rev_parse(repo_, "HEAD"), Error);  // no commits yet

  ObjectId c1 = commit({}, "one");
  ObjectId c2 = commit({c1}, "two");
  ObjectId side = commit({c1}, "side");
  ObjectId merge = commit({c2, side}, "merge");
  repo_.refs().update("refs/heads/main", merge, std::nullopt);
  repo_.refs().update("refs/tags/v1", c1, std::nullopt);

  EXPECT_EQ(rev_parse(repo_, merge.hex()), merge);
  EXPECT_EQ(rev_parse(repo_, "HEAD"), merge);
  EXPECT_EQ(rev_parse(repo_, "main"), merge);
  EXPECT_EQ(rev_parse(repo_, "refs/heads/main"), merge);
  EXPECT_EQ(rev_parse(repo_, "v1"), c1);
  EXPECT_EQ(rev_parse(repo_, merge.hex().substr(0, 7)), merge);

  EXPECT_EQ(rev_parse(repo_, "HEAD^"), c2);
  EXPECT_EQ(rev_parse(repo_, "HEAD^1"), c2);
  EXPECT_EQ(rev_parse(repo_, "HEAD^2"), side);
  EXPECT_EQ(rev_parse(repo_, "HEAD^0"), merge);
  EXPECT_EQ(rev_parse(repo_, "HEAD~"), c2);
  EXPECT_EQ(rev_parse(repo_, "HEAD~2"), c1);
  EXPECT_EQ(rev_parse(repo_, "main^2~1"), c1);
  EXPECT_EQ(rev_parse(repo_, "HEAD^^"), c1);

  EXPECT_THROW(rev_parse(repo_, "HEAD~3"), Error);  // past the root
  EXPECT_THROW(rev_parse(repo_, "HEAD^3"), Error);  // no third parent
  EXPECT_THROW(rev_parse(repo_, "nope"), Error);
  EXPECT_THROW(rev_parse(repo_, "HEAD@{1}"), Error);
  EXPECT_THROW(rev_parse(repo_, "~1"), Error);
  ObjectId tree = repo_.odb().write_tree(Tree{});
  EXPECT_THROW(rev_parse(repo_, tree.hex() + "^"), Error);  // not a commit
}

TEST_F(RevParseTest, BranchesShadowHexPrefixes) {
  ObjectId c1 = commit({}, "one");
  ObjectId c2 = commit({c1}, "two");
  const std::string prefix = c1.hex().substr(0, 4);
  repo_.refs().update("refs/heads/" + prefix, c2, std::nullopt);
  EXPECT_EQ(rev_parse(repo_, prefix), c2);
}

TEST_F(RevParseTest, AmbiguousPrefix) {
  // Find two blobs whose ids share their first 4 hex digits.
  std::map<std::string, std::string> seen;
  std::string a, b;
  for (int i = 0; a.empty(); ++i) {
    std::string content = "blob " + std::to_string(i);
    std::string p = hash_object(ObjectType::Blob, as_bytes(content)).hex().substr(0, 4);
    if (auto it = seen.find(p); it != seen.end()) {
      a = it->second;
      b = content;
    } else {
      seen.emplace(p, content);
    }
  }
  ObjectId ia = repo_.odb().write_blob(as_bytes(a));
  ObjectId ib = repo_.odb().write_blob(as_bytes(b));
  try {
    rev_parse(repo_, ia.hex().substr(0, 4));
    FAIL() << "expected an ambiguity error";
  } catch (const Error& e) {
    EXPECT_EQ(std::string(e.what()), "short object ID " + ia.hex().substr(0, 4) + " is ambiguous");
  }
  EXPECT_EQ(rev_parse(repo_, ia.hex().substr(0, 12)), ia);
  EXPECT_EQ(rev_parse(repo_, ib.hex().substr(0, 12)), ib);
}

}  // namespace
}  // namespace tva::core
