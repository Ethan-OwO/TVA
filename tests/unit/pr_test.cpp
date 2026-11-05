#include "core/pr.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/checkout.h"
#include "core/error.h"
#include "core/object.h"
#include "core/snapshot.h"
#include "core/trailers.h"
#include "test_util.h"

namespace tva::core {
namespace {

namespace fs = std::filesystem;

TEST(SlugTest, FromTitle) {
  EXPECT_EQ(slug_from_title("Add login page"), "add-login-page");
  EXPECT_EQ(slug_from_title("Fix: README typo!!"), "fix-readme-typo");
  EXPECT_EQ(slug_from_title("  --Hello--World--  "), "hello-world");
  EXPECT_EQ(slug_from_title("v2.0 release"), "v2-0-release");
  EXPECT_EQ(slug_from_title("做 login 功能"), "login");
  EXPECT_EQ(slug_from_title("做登入功能"), "pr");  // nothing ASCII left
  EXPECT_EQ(slug_from_title(""), "pr");
  EXPECT_EQ(slug_from_title("!!!"), "pr");
}

TEST(SlugTest, LongTitlesAreCutAtAWordBoundary) {
  // 40 characters exactly, so the boundary at index 40 is kept whole.
  EXPECT_EQ(slug_from_title("aaaaaaaaa bbbbbbbbb ccccccccc dddddddddd eeee"),
            "aaaaaaaaa-bbbbbbbbb-ccccccccc-dddddddddd");
  EXPECT_EQ(slug_from_title("aaaaaaaaa bbbbbbbbb ccccccccc ddddddddddd"),
            "aaaaaaaaa-bbbbbbbbb-ccccccccc");
  EXPECT_EQ(slug_from_title(std::string(50, 'x')), std::string(40, 'x'));  // no '-' to cut at
  for (const char* t : {"Add a login page with OAuth support and an onboarding flow", "x"}) {
    const std::string s = slug_from_title(t);
    EXPECT_LE(s.size(), kMaxGeneratedSlugLength) << t;
    EXPECT_TRUE(is_valid_slug(s)) << s;
  }
}

TEST(SlugTest, Validity) {
  for (const char* ok : {"a", "pr-2", "add-login-page", "0x1"}) EXPECT_TRUE(is_valid_slug(ok)) << ok;
  for (const char* bad : {"", "-a", "a-", "a--b", "A", "a_b", "a/b", "a.b", "a b", "登入"}) {
    EXPECT_FALSE(is_valid_slug(bad)) << bad;
  }
  EXPECT_TRUE(is_valid_slug(std::string(kMaxSlugLength, 'a')));
  EXPECT_FALSE(is_valid_slug(std::string(kMaxSlugLength + 1, 'a')));
}

class PrTest : public ::testing::Test {
 protected:
  PrTest() {
    env_.set("HOME", (tmp_.path() / "home").c_str());
    env_.set("XDG_CONFIG_HOME", nullptr);
    env_.set("GIT_AUTHOR_NAME", "Ada");
    env_.set("GIT_AUTHOR_EMAIL", "ada@example.com");
    env_.set("GIT_AUTHOR_DATE", "1700000000 +0800");
    env_.set("GIT_COMMITTER_NAME", nullptr);
    env_.set("GIT_COMMITTER_EMAIL", nullptr);
    env_.set("GIT_COMMITTER_DATE", nullptr);
    repo_.emplace(Repository::init(tmp_.path() / "w"));
  }

  Repository& repo() { return *repo_; }
  fs::path at(const std::string& rel) const { return repo_->worktree_root() / rel; }
  void write(const std::string& rel, std::string_view text) { test::write_text(at(rel), text); }

  // `tva snapshot -m`: commits the worktree on the branch HEAD points at.
  ObjectId commit_all(const std::string& message) {
    const std::string branch = *repo().refs().symbolic_target("HEAD");
    const auto parent = repo().refs().resolve(branch);
    Commit c;
    c.tree = snapshot_worktree(repo());
    if (parent) c.parents.push_back(*parent);
    c.author = repo().current_signature();
    c.committer = repo().current_committer();
    c.message = message + "\n";
    const ObjectId id = repo().odb().write_commit(c);
    repo().refs().update(branch, id, parent);
    return id;
  }

  test::TempDir tmp_;
  test::EnvGuard env_;  // before repo_: identity must be set before it is read
  std::optional<Repository> repo_;
};

TEST_F(PrTest, OpenOnTopOfMain) {
  write("a.txt", "a");
  const ObjectId main_head = commit_all("init");
  const ObjectId main_tree = repo().odb().read_commit(main_head).tree;

  const OpenedPr pr = open_pr(repo(), "\n\nAdd login\n\nUse OAuth.\n\nAnd tests.  \n\n", std::nullopt, false);
  EXPECT_EQ(pr.slug, "add-login");
  EXPECT_EQ(pr.title, "Add login");
  EXPECT_EQ(repo().refs().symbolic_target("HEAD"), "refs/heads/pr/add-login");
  EXPECT_EQ(repo().refs().resolve("HEAD"), pr.open_commit);
  EXPECT_EQ(current_pr(repo().refs()), "add-login");

  const Commit open = repo().odb().read_commit(pr.open_commit);
  EXPECT_EQ(open.tree, main_tree);
  EXPECT_EQ(open.parents, std::vector<ObjectId>{main_head});
  EXPECT_EQ(open.message, "Add login\n\nUse OAuth.\n\nAnd tests.\n\nTVA-PR-Open: add-login\n");

  auto info = read_pr(repo(), "add-login");
  ASSERT_TRUE(info);
  EXPECT_EQ(info->open_commit, pr.open_commit);
  EXPECT_EQ(info->base, main_head);
  EXPECT_EQ(info->head, pr.open_commit);
  EXPECT_EQ(info->title, "Add login");
  EXPECT_EQ(info->description, "Use OAuth.\n\nAnd tests.");
  EXPECT_TRUE(info->moments.empty());

  write("b.txt", "b");
  const ObjectId m1 = commit_all("first moment");
  write("b.txt", "bb");
  const ObjectId m2 = commit_all("second moment");
  info = read_pr(repo(), "add-login");
  EXPECT_EQ(info->head, m2);
  EXPECT_EQ(info->moments, (std::vector<ObjectId>{m2, m1}));
  EXPECT_EQ(info->open_commit, pr.open_commit);
}

TEST_F(PrTest, OpenOnAnEmptyMainMakesARootCommit) {
  write("existing.txt", "x");  // unsnapshotted; the trees match, so it carries over
  const OpenedPr pr = open_pr(repo(), "做登入功能", std::nullopt, false);
  EXPECT_EQ(pr.slug, "pr");
  const Commit open = repo().odb().read_commit(pr.open_commit);
  EXPECT_TRUE(open.parents.empty());
  EXPECT_EQ(open.tree, empty_tree_id());
  EXPECT_TRUE(repo().odb().exists(empty_tree_id()));  // fsck needs the tree object itself
  EXPECT_EQ(test::read_text(at("existing.txt")), "x");

  auto info = read_pr(repo(), "pr");
  ASSERT_TRUE(info);
  EXPECT_EQ(info->base, std::nullopt);
  EXPECT_EQ(info->title, "做登入功能");
  EXPECT_EQ(info->description, "");

  EXPECT_EQ(open_pr(repo(), "做登入功能", std::nullopt, false).slug, "pr-2");
}

TEST_F(PrTest, UniqueSlugSkipsTakenAndReservedNames) {
  write("a.txt", "a");
  const ObjectId id = commit_all("init");
  repo().refs().update("refs/heads/pr/fix", id, std::nullopt);
  repo().refs().update("refs/tva/merged/pr/fix-2", id, std::nullopt);
  repo().refs().update("refs/tva/pruned/pr/fix-3", id, std::nullopt);
  EXPECT_TRUE(slug_taken(repo().refs(), "fix-2"));
  EXPECT_EQ(unique_slug(repo().refs(), "Fix"), "fix-4");
  EXPECT_TRUE(slug_taken(repo().refs(), "main"));
  EXPECT_EQ(unique_slug(repo().refs(), "Main"), "main-2");
  EXPECT_EQ(unique_slug(repo().refs(), "Other"), "other");
}

TEST_F(PrTest, SlugOverride) {
  write("a.txt", "a");
  commit_all("init");
  EXPECT_EQ(open_pr(repo(), "Whatever", std::string("my-slug"), false).slug, "my-slug");
  for (const char* bad : {"My Slug", "main", "my-slug", "-x"}) {
    EXPECT_THROW(open_pr(repo(), "Whatever", std::string(bad), false), Error) << bad;
  }
  EXPECT_THROW(open_pr(repo(), " \n\t\n", std::nullopt, false), Error);  // empty intent
  EXPECT_EQ(list_prs(repo()).size(), 1u);
}

TEST_F(PrTest, OpeningFromADivergedPrNeedsACleanWorktree) {
  write("a.txt", "main");
  commit_all("init");
  open_pr(repo(), "First", std::nullopt, false);
  write("a.txt", "first pr");
  const ObjectId first_head = commit_all("edit a");

  write("a.txt", "unsnapshotted");
  EXPECT_THROW(open_pr(repo(), "Second", std::nullopt, false), Error);
  EXPECT_FALSE(repo().refs().resolve("refs/heads/pr/second"));  // nothing was created
  EXPECT_EQ(current_pr(repo().refs()), "first");
  EXPECT_EQ(test::read_text(at("a.txt")), "unsnapshotted");

  // --force discards the edit; the PR starts from main's tree.
  open_pr(repo(), "Second", std::nullopt, true);
  EXPECT_EQ(current_pr(repo().refs()), "second");
  EXPECT_EQ(test::read_text(at("a.txt")), "main");

  // A clean worktree is rewritten without --force.
  switch_branch(repo(), pr_ref("first"), false);
  EXPECT_EQ(test::read_text(at("a.txt")), "first pr");
  EXPECT_EQ(repo().refs().resolve("HEAD"), first_head);
  open_pr(repo(), "Third", std::nullopt, false);
  EXPECT_EQ(test::read_text(at("a.txt")), "main");
}

TEST_F(PrTest, SwitchBranch) {
  write("a.txt", "main");
  commit_all("init");
  open_pr(repo(), "Feature", std::nullopt, false);
  write("dir/new.txt", "new");
  commit_all("add new");

  switch_branch(repo(), std::string(kMainRef), false);
  EXPECT_EQ(repo().refs().symbolic_target("HEAD"), std::string(kMainRef));
  EXPECT_FALSE(fs::exists(at("dir")));

  write("a.txt", "dirty");
  EXPECT_THROW(switch_branch(repo(), pr_ref("feature"), false), Error);
  EXPECT_EQ(repo().refs().symbolic_target("HEAD"), std::string(kMainRef));
  EXPECT_EQ(test::read_text(at("a.txt")), "dirty");

  switch_branch(repo(), pr_ref("feature"), true);
  EXPECT_EQ(test::read_text(at("a.txt")), "main");
  EXPECT_EQ(test::read_text(at("dir/new.txt")), "new");
}

TEST_F(PrTest, SwitchToAnUnbornMainEmptiesTheWorktree) {
  open_pr(repo(), "Start", std::nullopt, false);
  write("a.txt", "a");
  commit_all("first");
  switch_branch(repo(), std::string(kMainRef), false);
  EXPECT_FALSE(fs::exists(at("a.txt")));
  EXPECT_EQ(snapshot_worktree(repo()), empty_tree_id());
}

TEST_F(PrTest, BranchesNotOpenedByTva) {
  write("a.txt", "a");
  const ObjectId init = commit_all("init");
  repo().refs().update("refs/heads/pr/plain", init, std::nullopt);
  const OpenedPr other = open_pr(repo(), "Other", std::nullopt, false);
  // pr/borrowed points into PR "other": its open commit names another slug.
  repo().refs().update("refs/heads/pr/borrowed", other.open_commit, std::nullopt);

  for (const char* slug : {"plain", "borrowed"}) {
    auto info = read_pr(repo(), slug);
    ASSERT_TRUE(info) << slug;
    EXPECT_FALSE(info->open_commit) << slug;
    EXPECT_TRUE(info->moments.empty()) << slug;
  }
  const auto all = list_prs(repo());
  ASSERT_EQ(all.size(), 3u);
  EXPECT_EQ(all[0].slug, "borrowed");
  EXPECT_EQ(all[1].slug, "other");
  EXPECT_EQ(all[2].slug, "plain");
  EXPECT_FALSE(read_pr(repo(), "missing"));
  EXPECT_FALSE(read_pr(repo(), "bad name"));
  EXPECT_FALSE(read_pr(repo(), ""));
}

}  // namespace
}  // namespace tva::core
