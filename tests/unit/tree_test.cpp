#include "core/tree.h"

#include <gtest/gtest.h>

#include "core/error.h"
#include "core/object.h"

namespace tva::core {
namespace {

const ObjectId kHello = *ObjectId::from_hex("3b18e512dba79e4c8300dd08aeb37f8e728b8dad");
const ObjectId kEmptyTree = *ObjectId::from_hex("4b825dc642cb6eb9a060e54bf8d69288fbee4904");

std::vector<std::string> names(const Tree& t) {
  std::vector<std::string> out;
  for (const auto& e : t.entries) out.push_back(e.name);
  return out;
}

TEST(Tree, DirectoriesSortAsIfSuffixedWithSlash) {
  // '-' (0x2D) < '.' (0x2E) < '/' (0x2F): the directory "a" sorts as "a/".
  Tree t{{{FileMode::Regular, "a.txt", kHello},
          {FileMode::Directory, "a", kEmptyTree},
          {FileMode::Regular, "a-b", kHello}}};
  EXPECT_EQ(names(parse_tree(serialize_tree(t))), (std::vector<std::string>{"a-b", "a.txt", "a"}));

  // As a *file*, "a" is a prefix of the others and sorts first.
  Tree f{{{FileMode::Regular, "a.txt", kHello},
          {FileMode::Regular, "a", kHello},
          {FileMode::Regular, "a-b", kHello}}};
  EXPECT_EQ(names(parse_tree(serialize_tree(f))), (std::vector<std::string>{"a", "a-b", "a.txt"}));
}

TEST(Tree, EntryLessComparesUnsignedBytes) {
  TreeEntry ascii{FileMode::Regular, "z", kHello};
  TreeEntry utf8{FileMode::Regular, "\xc3\xa9", kHello};  // 0xC3 > 'z'
  EXPECT_TRUE(tree_entry_less(ascii, utf8));
  EXPECT_FALSE(tree_entry_less(utf8, ascii));
}

TEST(Tree, SerializationFormat) {
  Tree t{{{FileMode::Directory, "d", kEmptyTree}}};
  Bytes raw = serialize_tree(t);
  std::string expected = std::string("40000 d") + '\0';  // no leading zero on the mode
  expected.append(reinterpret_cast<const char*>(kEmptyTree.bytes.data()), 20);
  EXPECT_EQ(std::string(as_chars(raw)), expected);
}

TEST(Tree, IdMatchesGitMktree) {
  // printf '100644 blob <hello>\thello.txt\n100755 ...\trun\n120000 ...\tlink\n
  //         040000 tree <empty>\tdir\n' | git mktree
  Tree t{{{FileMode::Symlink, "link", kHello},
          {FileMode::Regular, "hello.txt", kHello},
          {FileMode::Directory, "dir", kEmptyTree},
          {FileMode::Executable, "run", kHello}}};
  EXPECT_EQ(hash_object(ObjectType::Tree, serialize_tree(t)).hex(),
            "d23f4c64d2a229d7764c119cfa31422cbebd2356");
  EXPECT_EQ(hash_object(ObjectType::Tree, serialize_tree(Tree{})), kEmptyTree);
}

TEST(Tree, RoundTrip) {
  Tree t{{{FileMode::Directory, "dir", kEmptyTree},
          {FileMode::Regular, "hello.txt", kHello},
          {FileMode::Symlink, "link", kHello},
          {FileMode::Executable, "run", kHello}}};
  EXPECT_EQ(parse_tree(serialize_tree(t)), t);
}

TEST(Tree, RejectsInvalidNames) {
  for (const std::string& bad : std::vector<std::string>{"", ".", "..", "a/b", std::string("a\0b", 3)}) {
    Tree t{{{FileMode::Regular, bad, kHello}}};
    EXPECT_THROW(serialize_tree(t), Error) << "name: " << bad;
  }
  EXPECT_NO_THROW(serialize_tree(Tree{{{FileMode::Regular, "...", kHello}}}));
}

TEST(Tree, RejectsDuplicates) {
  Tree same{{{FileMode::Regular, "x", kHello}, {FileMode::Regular, "x", kHello}}};
  EXPECT_THROW(serialize_tree(same), Error);
  // File "a" and dir "a" are not adjacent once sorted ("a-b" sits between).
  Tree mixed{{{FileMode::Regular, "a", kHello},
              {FileMode::Regular, "a-b", kHello},
              {FileMode::Directory, "a", kEmptyTree}}};
  EXPECT_THROW(serialize_tree(mixed), Error);
}

TEST(Tree, ParseValidates) {
  auto raw = [](std::string head) {
    head += '\0';
    head.append(20, '\x11');
    return head;
  };
  EXPECT_EQ(parse_tree(as_bytes(raw("100664 legacy"))).entries[0].mode, FileMode::Regular);
  EXPECT_EQ(parse_tree(as_bytes(raw("100775 legacy"))).entries[0].mode, FileMode::Executable);
  EXPECT_THROW(parse_tree(as_bytes(raw("160000 submodule"))), Error);
  EXPECT_THROW(parse_tree(as_bytes(raw("100644 .."))), Error);
  EXPECT_THROW(parse_tree(as_bytes(raw("100644 a/b"))), Error);
  EXPECT_THROW(parse_tree(as_bytes(raw("10x644 name"))), Error);
  std::string truncated = raw("100644 name");
  truncated.pop_back();
  EXPECT_THROW(parse_tree(as_bytes(truncated)), Error);
  EXPECT_TRUE(parse_tree({}).entries.empty());
}

}  // namespace
}  // namespace tva::core
