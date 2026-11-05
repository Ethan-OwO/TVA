#include "core/object_database.h"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include "core/error.h"
#include "core/zlib_codec.h"
#include "test_util.h"

namespace tva::core {
namespace {

TEST(ObjectDatabase, WriteThenRead) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  ObjectId id = odb.write_blob(as_bytes("hello world\n"));
  EXPECT_EQ(id.hex(), "3b18e512dba79e4c8300dd08aeb37f8e728b8dad");
  EXPECT_TRUE(odb.exists(id));
  EXPECT_TRUE(std::filesystem::exists(tmp.path() / "objects/3b/18e512dba79e4c8300dd08aeb37f8e728b8dad"));

  auto obj = odb.read(id);
  ASSERT_TRUE(obj);
  EXPECT_EQ(obj->type, ObjectType::Blob);
  EXPECT_EQ(obj->payload, to_bytes("hello world\n"));
  EXPECT_EQ(odb.read_blob(id), to_bytes("hello world\n"));

  auto header = odb.read_header(id);
  ASSERT_TRUE(header);
  EXPECT_EQ(header->first, ObjectType::Blob);
  EXPECT_EQ(header->second, 12u);
}

TEST(ObjectDatabase, ObjectFilesAreReadOnly) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  ObjectId id = odb.write_blob(as_bytes("x"));
  struct ::stat st{};
  ASSERT_EQ(::stat(odb.object_path(id).c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0222, 0u);  // no write bits, whatever the umask
  EXPECT_EQ(odb.write_blob(as_bytes("x")), id);  // rewrite of an existing object is a no-op
}

TEST(ObjectDatabase, MissingObject) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  auto id = *ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8dad");
  EXPECT_FALSE(odb.exists(id));
  EXPECT_FALSE(odb.read(id));
  EXPECT_FALSE(odb.read_header(id));
  EXPECT_THROW(odb.read_blob(id), Error);
}

TEST(ObjectDatabase, DetectsCorruption) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  ObjectId id = odb.write_blob(as_bytes("original"));
  // Replace the file with a well-formed object of different content: only
  // re-hashing on read can notice.
  std::filesystem::path p = odb.object_path(id);
  std::filesystem::permissions(p, std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
  platform::write_file_atomic(p, zlib_deflate(encode_loose(ObjectType::Blob, as_bytes("tampered"))));
  try {
    (void)odb.read(id);
    FAIL() << "expected corruption to be detected";
  } catch (const Error& e) {
    EXPECT_EQ(std::string(e.what()), "corrupt object " + id.hex());
  }

  platform::write_file_atomic(p, as_bytes("not zlib at all"));
  EXPECT_THROW((void)odb.read(id), Error);
}

TEST(ObjectDatabase, TypedReadRejectsWrongType) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  ObjectId tree = odb.write(ObjectType::Tree, {});
  EXPECT_THROW(odb.read_blob(tree), Error);
}

TEST(ObjectDatabase, ResolvePrefix) {
  test::TempDir tmp;
  ObjectDatabase odb(tmp.path() / "objects");
  ObjectId id = odb.write_blob(as_bytes("hello world\n"));
  EXPECT_EQ(odb.resolve_prefix("3b18"), std::vector<ObjectId>{id});
  EXPECT_EQ(odb.resolve_prefix("3B18E5"), std::vector<ObjectId>{id});
  EXPECT_EQ(odb.resolve_prefix(id.hex()), std::vector<ObjectId>{id});
  EXPECT_TRUE(odb.resolve_prefix("3b19").empty());
  EXPECT_TRUE(odb.resolve_prefix("ffff").empty());
  EXPECT_THROW(odb.resolve_prefix("3b1"), Error);   // too short
  EXPECT_THROW(odb.resolve_prefix("3b1x"), Error);  // not hex
}

}  // namespace
}  // namespace tva::core
