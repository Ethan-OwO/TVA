#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "core/bytes.h"

// The only place where OS-specific calls live (POSIX today, Windows in M9).
// Failures throw tva::core::Error with the path in the message.
namespace tva::platform {

// 0666 / 0777 are filtered through the process umask, like any new file.
inline constexpr std::filesystem::perms kRegularPerms = std::filesystem::perms(0666);
inline constexpr std::filesystem::perms kExecutablePerms = std::filesystem::perms(0777);

core::Bytes read_file(const std::filesystem::path& p);

// Writes <name>.tmp.<pid>.<random> in the same directory, fsyncs it, then
// renames it over `p`. Readers see either the old file or the complete new
// one, never a partial write.
void write_file_atomic(const std::filesystem::path& p, core::ByteView data,
                       std::filesystem::perms perms = kRegularPerms);

bool is_executable(const std::filesystem::path& p);  // owner exec bit (S_IXUSR)
void set_executable(const std::filesystem::path& p, bool exec);

std::string read_symlink(const std::filesystem::path& p);  // raw link target
void create_symlink(const std::string& target, const std::filesystem::path& link);

// git-style lock file: holding <target>.lock (created with O_CREAT|O_EXCL)
// is the lock; commit() renames it over <target>. If the object is destroyed
// without commit(), the lock file is removed.
class LockFile {
 public:
  // nullopt if <target>.lock already exists (someone else holds the lock).
  static std::optional<LockFile> try_acquire(const std::filesystem::path& target);

  LockFile(LockFile&& other) noexcept;
  LockFile& operator=(LockFile&& other) noexcept;
  LockFile(const LockFile&) = delete;
  LockFile& operator=(const LockFile&) = delete;
  ~LockFile();

  void write(core::ByteView data);
  void commit();
  const std::filesystem::path& lock_path() const { return lock_path_; }

 private:
  LockFile(std::filesystem::path target, std::filesystem::path lock_path, int fd);
  void release() noexcept;

  std::filesystem::path target_;
  std::filesystem::path lock_path_;
  int fd_ = -1;
  bool owned_ = false;
};

struct FileStat {
  std::uint64_t size;
  std::int64_t mtime_ns;
  std::uint64_t inode;
  std::uint32_t mode;
};
FileStat lstat(const std::filesystem::path& p);  // for the M3 stat cache

}  // namespace tva::platform
