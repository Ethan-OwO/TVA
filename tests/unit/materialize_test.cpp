#include "core/materialize.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "core/snapshot.h"
#include "platform/fs.h"
#include "test_util.h"

namespace tva::core {
namespace {

namespace fs = std::filesystem;

class MaterializeTest : public ::testing::Test {
 protected:
  MaterializeTest() : repo_(Repository::init(tmp_.path() / "w")) {}

  fs::path at(const std::string& rel) const { return repo_.worktree_root() / rel; }
  void write(const std::string& rel, std::string_view text) { test::write_text(at(rel), text); }
  ObjectId snap() { return snapshot_worktree(repo_); }
  // Moves the worktree from its current state to `target`.
  void restore(const ObjectId& target) { materialize_tree(repo_, target, snap()); }

  test::TempDir tmp_;
  Repository repo_;
};

TEST_F(MaterializeTest, RoundTripsContentModesAndSymlinks) {
  write("a.txt", "one");
  write("src/main.c", "int main() {}");
  write("bin/run", "#!/bin/sh");
  platform::set_executable(at("bin/run"), true);
  platform::create_symlink("src/main.c", at("link"));
  const ObjectId a = snap();

  write("a.txt", "two");
  fs::remove(at("src/main.c"));
  write("new/deep/file", "n");
  platform::set_executable(at("bin/run"), false);
  fs::remove(at("link"));
  write("link", "now a file");
  const ObjectId b = snap();
  ASSERT_NE(a, b);

  restore(a);
  EXPECT_EQ(snap(), a);
  EXPECT_EQ(test::read_text(at("a.txt")), "one");
  EXPECT_TRUE(platform::is_executable(at("bin/run")));
  EXPECT_TRUE(fs::is_symlink(fs::symlink_status(at("link"))));
  EXPECT_EQ(platform::read_symlink(at("link")), "src/main.c");
  EXPECT_FALSE(fs::exists(at("new")));  // emptied directories are pruned

  restore(b);
  EXPECT_EQ(snap(), b);
}

TEST_F(MaterializeTest, FileAndDirectorySwapPlaces) {
  write("x", "file");
  const ObjectId as_file = snap();
  fs::remove(at("x"));
  write("x/inner/f", "dir");
  const ObjectId as_dir = snap();

  restore(as_file);
  EXPECT_TRUE(fs::is_regular_file(at("x")));
  restore(as_dir);
  EXPECT_EQ(test::read_text(at("x/inner/f")), "dir");
  EXPECT_EQ(snap(), as_dir);
}

TEST_F(MaterializeTest, IgnoredFilesAreNeverTouched) {
  write(".gitignore", "*.o\n");
  write("src/a.c", "a");
  const ObjectId a = snap();
  write("src/b.c", "b");
  write("src/b.o", "object");  // ignored, inside a directory we will prune
  const ObjectId b = snap();
  (void)b;

  restore(a);
  EXPECT_FALSE(fs::exists(at("src/b.c")));
  EXPECT_EQ(test::read_text(at("src/b.o")), "object");
}

TEST_F(MaterializeTest, ConflictsAbortBeforeAnyChange) {
  write(".gitignore", "*.o\n");
  write("keep.txt", "v1");
  write("dir/x.c", "x");
  const ObjectId with_dir = snap();
  fs::remove_all(at("dir"));
  write("dir", "now a file");
  write("keep.txt", "v2");
  const ObjectId with_file = snap();

  restore(with_dir);
  write("dir/stray.o", "ignored file in the way");
  // Turning "dir" back into a file would require deleting an ignored file.
  EXPECT_THROW(restore(with_file), Error);
  EXPECT_EQ(test::read_text(at("keep.txt")), "v1");  // nothing was applied
  EXPECT_EQ(test::read_text(at("dir/x.c")), "x");
}

TEST_F(MaterializeTest, EmptyDirectoryInTheWayIsReplaced) {
  write("f", "file");
  const ObjectId a = snap();
  fs::remove(at("f"));
  fs::create_directories(at("f/empty/nested"));  // untracked, but holds no files
  restore(a);
  EXPECT_EQ(test::read_text(at("f")), "file");
}

}  // namespace
}  // namespace tva::core
