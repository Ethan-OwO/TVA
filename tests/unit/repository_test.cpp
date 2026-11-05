#include "core/repository.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "test_util.h"

namespace tva::core {
namespace {

class RepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Keep the developer's own ~/.gitconfig out of these tests.
    env_.set("HOME", (tmp_.path() / "home").c_str());
    env_.set("XDG_CONFIG_HOME", nullptr);
    for (const char* v : {"GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_AUTHOR_DATE",
                          "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL", "GIT_COMMITTER_DATE"}) {
      env_.set(v, nullptr);
    }
  }

  test::TempDir tmp_;
  test::EnvGuard env_;
};

TEST_F(RepositoryTest, InitLayout) {
  auto repo = Repository::init(tmp_.path() / "w");
  const auto git = repo.git_dir();
  EXPECT_EQ(test::read_text(git / "HEAD"), "ref: refs/heads/main\n");
  EXPECT_EQ(test::read_text(git / "info/exclude"), ".tva/\n");
  EXPECT_EQ(test::read_text(git / "config"),
            "[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n");
  for (const char* dir : {"objects", "refs/heads", "refs/tags"}) {
    EXPECT_TRUE(std::filesystem::is_directory(git / dir)) << dir;
  }
  EXPECT_TRUE(std::filesystem::is_directory(repo.tva_dir()));
  EXPECT_THROW(Repository::init(tmp_.path() / "w"), Error);
}

TEST_F(RepositoryTest, DiscoverWalksUp) {
  auto repo = Repository::init(tmp_.path() / "w");
  std::filesystem::create_directories(tmp_.path() / "w/a/b");
  EXPECT_EQ(Repository::discover(tmp_.path() / "w/a/b").worktree_root(), repo.worktree_root());
  EXPECT_THROW(Repository::discover(tmp_.path()), Error);
}

TEST_F(RepositoryTest, IdentityFromConfig) {
  test::write_text(tmp_.path() / "home/.gitconfig", "[user]\n\tname = Cfg User\n\temail = cfg@x\n");
  auto repo = Repository::init(tmp_.path() / "w");
  Signature author = repo.current_signature();
  EXPECT_EQ(author.name, "Cfg User");
  EXPECT_EQ(author.email, "cfg@x");
  EXPECT_GT(author.when_unix, 1'600'000'000);
  EXPECT_EQ(repo.current_committer(), author);
}

TEST_F(RepositoryTest, RepoConfigOverridesGlobal) {
  test::write_text(tmp_.path() / "home/.gitconfig", "[user]\n\tname = Global\n\temail = g@x\n");
  auto repo0 = Repository::init(tmp_.path() / "w");
  test::write_text(repo0.git_dir() / "config", "[user]\n\tname = Local\n");
  auto repo = Repository::discover(tmp_.path() / "w");
  EXPECT_EQ(repo.current_signature().name, "Local");
  EXPECT_EQ(repo.current_signature().email, "g@x");
}

TEST_F(RepositoryTest, IdentityFromEnvironment) {
  auto repo = Repository::init(tmp_.path() / "w");
  EXPECT_THROW(repo.current_signature(), Error);  // nothing configured

  env_.set("GIT_AUTHOR_NAME", "  <Ada Lovelace>. ");  // git trims "crud" at the ends
  env_.set("GIT_AUTHOR_EMAIL", "ada@example.com");
  env_.set("GIT_AUTHOR_DATE", "1700000000 +0800");
  Signature author = repo.current_signature();
  EXPECT_EQ(author, (Signature{"Ada Lovelace", "ada@example.com", 1700000000, 480}));

  // Unset committer parts fall back to the author's.
  EXPECT_EQ(repo.current_committer(), author);
  env_.set("GIT_COMMITTER_NAME", "Charles");
  env_.set("GIT_COMMITTER_DATE", "@1700000100 -0530");
  EXPECT_EQ(repo.current_committer(), (Signature{"Charles", "ada@example.com", 1700000100, -330}));

  env_.set("GIT_AUTHOR_DATE", "yesterday");
  EXPECT_THROW(repo.current_signature(), Error);
}

}  // namespace
}  // namespace tva::core
