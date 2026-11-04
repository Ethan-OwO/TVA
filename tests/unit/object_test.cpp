#include "core/object.h"

#include <gtest/gtest.h>

#include <unordered_set>

#include "core/error.h"
#include "core/object_id.h"

namespace tva::core {
namespace {

TEST(ObjectId, HexRoundTrip) {
  const std::string hex = "3b18e512dba79e4c8300dd08aeb37f8e728b8dad";
  auto id = ObjectId::from_hex(hex);
  ASSERT_TRUE(id);
  EXPECT_EQ(id->hex(), hex);
  EXPECT_EQ(ObjectId::from_hex("3B18E512DBA79E4C8300DD08AEB37F8E728B8DAD"), id);
}

TEST(ObjectId, RejectsBadHex) {
  EXPECT_FALSE(ObjectId::from_hex(""));
  EXPECT_FALSE(ObjectId::from_hex("3b18e512"));
  EXPECT_FALSE(ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8daz"));
  EXPECT_FALSE(ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8dad0"));
}

TEST(ObjectId, OrderingAndHash) {
  auto a = *ObjectId::from_hex(std::string(40, '0'));
  auto b = *ObjectId::from_hex(std::string(39, '0') + "1");
  EXPECT_LT(a, b);
  std::unordered_set<ObjectId> set{a, b, a};
  EXPECT_EQ(set.size(), 2u);
}

TEST(Object, TypeNames) {
  EXPECT_EQ(to_string(ObjectType::Blob), "blob");
  EXPECT_EQ(parse_object_type("tree"), ObjectType::Tree);
  EXPECT_EQ(parse_object_type("commit"), ObjectType::Commit);
  EXPECT_FALSE(parse_object_type("bogus"));
  EXPECT_THROW(parse_object_type("tag"), Error);
}

TEST(Object, HashMatchesGit) {
  // `printf 'hello world\n' | git hash-object --stdin`
  EXPECT_EQ(hash_object(ObjectType::Blob, as_bytes("hello world\n")).hex(),
            "3b18e512dba79e4c8300dd08aeb37f8e728b8dad");
  // The empty blob and the empty tree are well-known ids.
  EXPECT_EQ(hash_object(ObjectType::Blob, {}).hex(), "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
  EXPECT_EQ(hash_object(ObjectType::Tree, {}).hex(), "4b825dc642cb6eb9a060e54bf8d69288fbee4904");
}

TEST(Object, EncodeDecodeRoundTrip) {
  Bytes encoded = encode_loose(ObjectType::Blob, as_bytes("abc"));
  EXPECT_EQ(std::string(as_chars(encoded)), std::string("blob 3\0abc", 10));
  RawObject obj = decode_loose(encoded);
  EXPECT_EQ(obj.type, ObjectType::Blob);
  EXPECT_EQ(obj.payload, to_bytes("abc"));
}

TEST(Object, DecodeRejectsMalformedHeaders) {
  using namespace std::string_literals;
  EXPECT_THROW(decode_loose(to_bytes("blob 4\0abc"s)), Error);    // length mismatch
  EXPECT_THROW(decode_loose(to_bytes("blob 03\0abc"s)), Error);   // leading zero
  EXPECT_THROW(decode_loose(to_bytes("blob 3abc"s)), Error);      // no NUL
  EXPECT_THROW(decode_loose(to_bytes("blob \0"s)), Error);        // no digits
  EXPECT_THROW(decode_loose(to_bytes("blob -1\0"s)), Error);      // not a number
  EXPECT_THROW(decode_loose(to_bytes("bogus 0\0"s)), Error);      // unknown type
  EXPECT_THROW(decode_loose(to_bytes("tag 0\0"s)), Error);        // unsupported type
  EXPECT_THROW(decode_loose(to_bytes("blob 99999999999999999999999\0"s)), Error);  // overflow
  EXPECT_NO_THROW(decode_loose(to_bytes("blob 0\0"s)));
}

TEST(Object, ParseHeaderFromPrefix) {
  using namespace std::string_literals;
  LooseHeader h = parse_loose_header(to_bytes("commit 1234\0partial"s));
  EXPECT_EQ(h.type, ObjectType::Commit);
  EXPECT_EQ(h.size, 1234u);
  EXPECT_EQ(h.header_len, 12u);
}

}  // namespace
}  // namespace tva::core
