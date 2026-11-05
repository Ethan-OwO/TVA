#include "core/trailers.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/error.h"

namespace tva::core {
namespace {

TEST(TrailersTest, AppendThenSplitRoundTrips) {
  const std::vector<Trailer> trailers = {{"TVA-PR-Open", "login"}, {"TVA-Turn", "3"}};
  const std::string msg = append_trailers("Add login\n\nWith OAuth.\n\n", trailers);
  EXPECT_EQ(msg, "Add login\n\nWith OAuth.\n\nTVA-PR-Open: login\nTVA-Turn: 3\n");

  const MessageParts parts = split_trailers(msg);
  EXPECT_EQ(parts.body, "Add login\n\nWith OAuth.");
  EXPECT_EQ(parts.trailers, trailers);
  EXPECT_EQ(trailer_value(parts.trailers, "TVA-Turn"), "3");
  EXPECT_EQ(trailer_value(parts.trailers, "tva-pr-open"), "login");  // keys ignore case
  EXPECT_EQ(trailer_value(parts.trailers, "TVA-Session"), std::nullopt);
}

TEST(TrailersTest, SubjectIsNeverATrailerBlock) {
  // git's trailer.c skips the first paragraph: a lone "Key: value" is a title.
  for (const char* msg : {"Fix: crash on start\n", "\n\nFix: crash\nKey: v\n"}) {
    const MessageParts parts = split_trailers(msg);
    EXPECT_TRUE(parts.trailers.empty()) << msg;
  }
}

TEST(TrailersTest, LastParagraphMustBeAllTrailers) {
  const MessageParts parts = split_trailers("Subject\n\nKey: value\nnot a trailer\n");
  EXPECT_TRUE(parts.trailers.empty());
  EXPECT_EQ(parts.body, "Subject\n\nKey: value\nnot a trailer");

  for (const char* msg : {"S\n\nbad key: v\n", "S\n\n: v\n", "S\n\nKey value\n", "S\n\n  Key: v\n"}) {
    EXPECT_TRUE(split_trailers(msg).trailers.empty()) << msg;
  }
}

TEST(TrailersTest, OnlyTheLastParagraphCounts) {
  const MessageParts parts = split_trailers("S\n\nA: 1\n\nbody text\n\nB: 2\n\n\n");
  ASSERT_EQ(parts.trailers.size(), 1u);
  EXPECT_EQ(parts.trailers[0], (Trailer{"B", "2"}));
  EXPECT_EQ(parts.body, "S\n\nA: 1\n\nbody text");
}

TEST(TrailersTest, ContinuationLinesAndCrlf) {
  const MessageParts parts = split_trailers("S\r\n\r\nKey: first\r\n  second\r\nOther:x\r\n");
  ASSERT_EQ(parts.trailers.size(), 2u);
  EXPECT_EQ(parts.trailers[0], (Trailer{"Key", "first second"}));
  EXPECT_EQ(parts.trailers[1], (Trailer{"Other", "x"}));
  EXPECT_EQ(parts.body, "S");
}

TEST(TrailersTest, MessagesWithoutTrailers) {
  EXPECT_EQ(split_trailers("").body, "");
  EXPECT_EQ(split_trailers("just a subject\n").body, "just a subject");
  EXPECT_EQ(split_trailers("subject\n\nbody\n").body, "subject\n\nbody");
  EXPECT_EQ(append_trailers("subject  \n\n", {}), "subject\n");
  EXPECT_EQ(append_trailers("", {}), "");
}

TEST(TrailersTest, AppendSanitizesAndValidates) {
  EXPECT_EQ(append_trailers("S", {{"TVA-Session", " a\nb\r\n "}}), "S\n\nTVA-Session: a b\n");
  EXPECT_THROW(append_trailers("S", {{"Bad Key", "v"}}), Error);
  EXPECT_THROW(append_trailers("S", {{"", "v"}}), Error);
  EXPECT_THROW(append_trailers(" \n", {{"Key", "v"}}), Error);  // would parse as a subject
}

TEST(TrailersTest, TokenRules) {
  EXPECT_TRUE(is_trailer_token("TVA-PR-Open"));
  EXPECT_TRUE(is_trailer_token("Signed-off-by"));
  EXPECT_TRUE(is_trailer_token("x1"));
  EXPECT_FALSE(is_trailer_token(""));
  EXPECT_FALSE(is_trailer_token("a b"));
  EXPECT_FALSE(is_trailer_token("a_b"));
  EXPECT_FALSE(is_trailer_token("a:b"));
}

}  // namespace
}  // namespace tva::core
