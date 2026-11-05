#include "core/tree_diff.h"

#include <gtest/gtest.h>

#include <map>

#include "test_util.h"

namespace tva::core {
namespace {

class TreeDiffTest : public ::testing::Test {
 protected:
  TreeDiffTest() : odb_(tmp_.path() / "objects") {}

  ObjectId blob(std::string_view s) { return odb_.write_blob(as_bytes(s)); }
  ObjectId tree(std::vector<TreeEntry> entries) { return odb_.write_tree(Tree{std::move(entries)}); }
  TreeEntry file(std::string name, std::string_view content, FileMode m = FileMode::Regular) {
    return {m, std::move(name), blob(content)};
  }
  TreeEntry dir(std::string name, std::vector<TreeEntry> entries) {
    return {FileMode::Directory, std::move(name), tree(std::move(entries))};
  }

  // (kind, path) pairs, which is what most assertions care about.
  std::vector<std::pair<ChangeKind, std::string>> summary(std::optional<ObjectId> a,
                                                          std::optional<ObjectId> b) {
    std::vector<std::pair<ChangeKind, std::string>> out;
    for (const auto& c : diff_trees(odb_, a, b)) out.emplace_back(c.kind, c.path);
    return out;
  }

  test::TempDir tmp_;
  ObjectDatabase odb_;
};

using K = ChangeKind;
using Summary = std::vector<std::pair<ChangeKind, std::string>>;

TEST_F(TreeDiffTest, AddedDeletedModified) {
  ObjectId a = tree({file("keep", "k"), file("gone", "g"), file("edit", "v1"),
                     dir("src", {file("x.c", "x"), file("y.c", "y")})});
  ObjectId b = tree({file("keep", "k"), file("edit", "v2"), file("new", "n"),
                     dir("src", {file("x.c", "x2"), file("z.c", "z")})});
  EXPECT_EQ(summary(a, b), (Summary{{K::Modified, "edit"},
                                    {K::Deleted, "gone"},
                                    {K::Added, "new"},
                                    {K::Modified, "src/x.c"},
                                    {K::Deleted, "src/y.c"},
                                    {K::Added, "src/z.c"}}));
  auto changes = diff_trees(odb_, a, b);
  EXPECT_EQ(changes[0].old_entry->id, blob("v1"));
  EXPECT_EQ(changes[0].new_entry->id, blob("v2"));
  EXPECT_FALSE(changes[1].new_entry);
  EXPECT_FALSE(changes[2].old_entry);
}

TEST_F(TreeDiffTest, ModeChanges) {
  ObjectId a = tree({file("run", "#!", FileMode::Regular), file("link", "target"),
                     file("both", "1", FileMode::Regular)});
  ObjectId b = tree({file("run", "#!", FileMode::Executable),
                     file("link", "target", FileMode::Symlink),
                     file("both", "2", FileMode::Executable)});
  EXPECT_EQ(summary(a, b), (Summary{{K::Modified, "both"},  // content wins over mode
                                    {K::ModeChanged, "link"},
                                    {K::ModeChanged, "run"}}));
}

TEST_F(TreeDiffTest, FileBecomesDirectoryAndBack) {
  ObjectId as_file = tree({file("a", "file"), file("a-b", "x"), file("a.txt", "y")});
  ObjectId as_dir = tree({dir("a", {file("inner", "i"), dir("deep", {file("f", "f")})}),
                          file("a-b", "x"), file("a.txt", "y")});
  EXPECT_EQ(summary(as_file, as_dir),
            (Summary{{K::Deleted, "a"}, {K::Added, "a/deep/f"}, {K::Added, "a/inner"}}));
  EXPECT_EQ(summary(as_dir, as_file),
            (Summary{{K::Added, "a"}, {K::Deleted, "a/deep/f"}, {K::Deleted, "a/inner"}}));
}

TEST_F(TreeDiffTest, EmptySides) {
  ObjectId t = tree({file("f", "1"), dir("d", {file("g", "2")})});
  EXPECT_EQ(summary(std::nullopt, t), (Summary{{K::Added, "d/g"}, {K::Added, "f"}}));
  EXPECT_EQ(summary(t, std::nullopt), (Summary{{K::Deleted, "d/g"}, {K::Deleted, "f"}}));
  EXPECT_TRUE(summary(t, t).empty());
  EXPECT_TRUE(summary(std::nullopt, std::nullopt).empty());
}

TEST_F(TreeDiffTest, IdenticalSubtreesAreNeverRead) {
  TreeEntry shared = dir("vendor", {dir("lib", {file("a", "1"), file("b", "2")}), file("c", "3")});
  ObjectId a = tree({shared, dir("src", {file("main.c", "v1")})});
  ObjectId b = tree({shared, dir("src", {file("main.c", "v2")})});

  std::map<ObjectId, int> reads;
  TreeReader counting = [&](const ObjectId& id) {
    ++reads[id];
    return odb_.read_tree(id);
  };
  auto changes = diff_trees(counting, a, b);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].path, "src/main.c");
  EXPECT_EQ(reads.count(shared.id), 0u);  // skipped by id, not expanded
  int total = 0;
  for (auto& [id, n] : reads) total += n;
  EXPECT_EQ(total, 4);  // two roots + the two versions of src/

  reads.clear();
  EXPECT_TRUE(diff_trees(counting, a, a).empty());
  EXPECT_TRUE(reads.empty());
}

TEST_F(TreeDiffTest, PathsAreSortedBytewise) {
  ObjectId a = tree({});
  ObjectId b = tree({dir("a", {file("z", "1")}), file("a-b", "2"), file("a.c", "3"),
                     dir("b", {dir("c", {file("d", "4")})}), file("B", "5")});
  auto changes = diff_trees(odb_, a, b);
  for (std::size_t i = 1; i < changes.size(); ++i) {
    EXPECT_LT(changes[i - 1].path, changes[i].path);
  }
  EXPECT_EQ(changes.size(), 5u);
}

}  // namespace
}  // namespace tva::core
