#include "core/refs.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "test_util.h"

namespace tva::core {
namespace {

const ObjectId kA = *ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8dad");
const ObjectId kB = *ObjectId::from_hex("4b825dc642cb6eb9a060e54bf8d69288fbee4904");

class RefsTest : public ::testing::Test {
 protected:
  void SetUp() override { std::filesystem::create_directories(git() / "refs/heads"); }
  std::filesystem::path git() const { return tmp_.path() / ".git"; }
  std::vector<std::string> files_in(const std::filesystem::path& dir) const {
    std::vector<std::string> out;
    for (const auto& e : std::filesystem::directory_iterator(dir)) out.push_back(e.path().filename());
    std::sort(out.begin(), out.end());
    return out;
  }

  test::TempDir tmp_;
};

TEST_F(RefsTest, ReadRawAndResolve) {
  test::write_text(git() / "HEAD", "ref: refs/heads/main\n");
  RefStore refs(git());
  EXPECT_EQ(refs.symbolic_target("HEAD"), "refs/heads/main");
  EXPECT_FALSE(refs.resolve("HEAD"));  // unborn branch
  EXPECT_FALSE(refs.read_raw("refs/heads/main"));

  test::write_text(git() / "refs/heads/main", kA.hex() + "\n");
  EXPECT_EQ(refs.resolve("HEAD"), kA);
  EXPECT_EQ(refs.resolve("refs/heads/main"), kA);
  EXPECT_FALSE(refs.symbolic_target("refs/heads/main"));

  test::write_text(git() / "refs/heads/bad", "garbage\n");
  EXPECT_THROW(refs.resolve("refs/heads/bad"), Error);
}

TEST_F(RefsTest, SymbolicDepthLimit) {
  RefStore refs(git());
  test::write_text(git() / "refs/heads/r5", kA.hex() + "\n");
  for (int i = 0; i < 5; ++i) {
    test::write_text(git() / ("refs/heads/r" + std::to_string(i)),
                     "ref: refs/heads/r" + std::to_string(i + 1) + "\n");
  }
  EXPECT_EQ(refs.resolve("refs/heads/r0"), kA);  // 5 hops is the limit
  test::write_text(git() / "refs/heads/rx", "ref: refs/heads/r0\n");
  EXPECT_THROW(refs.resolve("refs/heads/rx"), Error);  // 6 hops
  test::write_text(git() / "refs/heads/loop", "ref: refs/heads/loop\n");
  EXPECT_THROW(refs.resolve("refs/heads/loop"), Error);
}

TEST_F(RefsTest, CompareAndSwap) {
  RefStore refs(git());
  refs.update("refs/heads/main", kA, std::nullopt);  // create
  EXPECT_EQ(refs.resolve("refs/heads/main"), kA);
  EXPECT_EQ(test::read_text(git() / "refs/heads/main"), kA.hex() + "\n");

  refs.update("refs/heads/main", kB, kA);  // expected old matches
  EXPECT_EQ(refs.resolve("refs/heads/main"), kB);

  EXPECT_THROW(refs.update("refs/heads/main", kA, kA), Error);           // stale expectation
  EXPECT_THROW(refs.update("refs/heads/main", kA, std::nullopt), Error);  // expected absent
  EXPECT_THROW(refs.update("refs/heads/other", kA, kB), Error);           // expected present
  EXPECT_EQ(refs.resolve("refs/heads/main"), kB);
  // The failed attempts released their locks.
  EXPECT_EQ(files_in(git() / "refs/heads"), std::vector<std::string>{"main"});

  refs.update("refs/heads/pr/feature", kA, std::nullopt);  // creates parent dirs
  EXPECT_EQ(refs.resolve("refs/heads/pr/feature"), kA);
}

TEST_F(RefsTest, HeldLockFailsWithoutSideEffects) {
  RefStore refs(git());
  refs.update("refs/heads/main", kA, std::nullopt);
  test::write_text(git() / "refs/heads/main.lock", "someone else\n");
  try {
    refs.update("refs/heads/main", kB, kA);
    FAIL() << "expected the held lock to be reported";
  } catch (const Error& e) {
    EXPECT_EQ(std::string(e.what()), "unable to lock ref refs/heads/main");
  }
  EXPECT_EQ(refs.resolve("refs/heads/main"), kA);
  // The other process's lock is untouched and nothing else was left behind.
  EXPECT_EQ(test::read_text(git() / "refs/heads/main.lock"), "someone else\n");
  EXPECT_EQ(files_in(git() / "refs/heads"), (std::vector<std::string>{"main", "main.lock"}));
}

TEST_F(RefsTest, SetSymbolicAndList) {
  RefStore refs(git());
  refs.set_symbolic("HEAD", "refs/heads/dev");
  EXPECT_EQ(test::read_text(git() / "HEAD"), "ref: refs/heads/dev\n");
  EXPECT_THROW(refs.set_symbolic("HEAD", "HEAD"), Error);
  EXPECT_THROW(refs.set_symbolic("HEAD", "refs/heads/bad name"), Error);

  refs.update("refs/heads/b", kB, std::nullopt);
  refs.update("refs/heads/a", kA, std::nullopt);
  refs.update("refs/tags/v1", kA, std::nullopt);
  test::write_text(git() / "refs/heads/c.lock", "ignored\n");
  using Entry = std::pair<std::string, ObjectId>;
  EXPECT_EQ(refs.list("refs/heads/"),
            (std::vector<Entry>{{"refs/heads/a", kA}, {"refs/heads/b", kB}}));
  EXPECT_EQ(refs.list("refs/").size(), 3u);
}

TEST(RefName, Validation) {
  for (const char* ok : {"HEAD", "refs/heads/main", "refs/heads/pr/login-page", "refs/tva/merged/pr/x",
                         "refs/tags/v1.0"}) {
    EXPECT_TRUE(is_valid_ref_name(ok)) << ok;
  }
  for (const char* bad : {"main", "refs/", "refs/heads/", "refs/heads/a..b", "refs/heads/a b",
                          "refs/heads/a~1", "refs/heads/a^", "refs/heads/a:b", "refs/heads/a?",
                          "refs/heads/a*", "refs/heads/a[", "refs/heads/a\\b", "refs/heads/x.lock",
                          "refs/heads/.hidden", "refs//heads", "refs/heads/a.", "refs/heads/a@{1}",
                          "refs/../../etc/passwd", "refs/heads/tab\there", "head"}) {
    EXPECT_FALSE(is_valid_ref_name(bad)) << bad;
  }
  test::TempDir tmp;
  RefStore refs(tmp.path());
  EXPECT_THROW(refs.read_raw("refs/../../outside"), Error);
}

}  // namespace
}  // namespace tva::core
