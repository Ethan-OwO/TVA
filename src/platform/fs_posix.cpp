#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <string>
#include <system_error>
#include <utility>

#include "core/error.h"
#include "platform/fs.h"

namespace tva::platform {
namespace fs = std::filesystem;
using core::Error;

namespace {

[[noreturn]] void throw_errno(const std::string& what, const fs::path& p, int err) {
  throw Error(what + " '" + p.string() + "': " + std::generic_category().message(err));
}

void write_all(int fd, core::ByteView data, const fs::path& p) {
  std::size_t done = 0;
  while (done < data.size()) {
    ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n < 0) {
      if (errno == EINTR) continue;
      throw_errno("cannot write", p, errno);
    }
    done += static_cast<std::size_t>(n);
  }
}

std::string unique_suffix() {
  // Uniqueness only has to be good enough to make O_EXCL collisions rare;
  // a collision just means another attempt.
  static std::atomic<std::uint64_t> counter{0};
  auto now = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  std::uint64_t x = now ^ (counter.fetch_add(1) * 0x9E3779B97F4A7C15ull);
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string s(8, '0');
  for (auto& c : s) {
    c = kDigits[x & 0xF];
    x >>= 4;
  }
  return std::to_string(::getpid()) + "." + s;
}

struct ::stat stat_or_throw(const fs::path& p) {
  struct ::stat st{};
  if (::stat(p.c_str(), &st) != 0) throw_errno("cannot stat", p, errno);
  return st;
}

}  // namespace

core::Bytes read_file(const fs::path& p) {
  int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) throw_errno("cannot open", p, errno);
  core::Bytes out;
  struct ::stat st{};
  if (::fstat(fd, &st) == 0 && st.st_size > 0) out.reserve(static_cast<std::size_t>(st.st_size));
  std::uint8_t buf[64 * 1024];
  for (;;) {
    ssize_t n = ::read(fd, buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) continue;
      int err = errno;
      ::close(fd);
      throw_errno("cannot read", p, err);
    }
    if (n == 0) break;
    out.insert(out.end(), buf, buf + n);
  }
  ::close(fd);
  return out;
}

void write_file_atomic(const fs::path& p, core::ByteView data, fs::perms perms) {
  const std::string base = p.filename().string();
  for (int attempt = 0;; ++attempt) {
    fs::path tmp = p.parent_path() / (base + ".tmp." + unique_suffix());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                    static_cast<mode_t>(perms));
    if (fd < 0) {
      if (errno == EEXIST && attempt < 100) continue;
      throw_errno("cannot create", tmp, errno);
    }
    try {
      write_all(fd, data, tmp);
      if (::fsync(fd) != 0) throw_errno("cannot fsync", tmp, errno);
    } catch (...) {
      ::close(fd);
      ::unlink(tmp.c_str());
      throw;
    }
    if (::close(fd) != 0) {
      int err = errno;
      ::unlink(tmp.c_str());
      throw_errno("cannot close", tmp, err);
    }
    if (::rename(tmp.c_str(), p.c_str()) != 0) {
      int err = errno;
      ::unlink(tmp.c_str());
      throw_errno("cannot rename into", p, err);
    }
    return;
  }
}

bool is_executable(const fs::path& p) { return (stat_or_throw(p).st_mode & S_IXUSR) != 0; }

void set_executable(const fs::path& p, bool exec) {
  mode_t mode = stat_or_throw(p).st_mode & 07777;
  // Grant x wherever r is granted (what checking out an executable file
  // with the current umask would give); clearing removes every x bit.
  mode_t updated = exec ? (mode | ((mode & 0444) >> 2)) : (mode & ~mode_t{0111});
  if (updated != mode && ::chmod(p.c_str(), updated) != 0) throw_errno("cannot chmod", p, errno);
}

std::string read_symlink(const fs::path& p) {
  std::string buf(256, '\0');
  for (;;) {
    ssize_t n = ::readlink(p.c_str(), buf.data(), buf.size());
    if (n < 0) throw_errno("cannot read symlink", p, errno);
    if (static_cast<std::size_t>(n) < buf.size()) {
      buf.resize(static_cast<std::size_t>(n));
      return buf;
    }
    buf.resize(buf.size() * 2);  // possibly truncated; retry with more room
  }
}

void create_symlink(const std::string& target, const fs::path& link) {
  if (::symlink(target.c_str(), link.c_str()) != 0) throw_errno("cannot create symlink", link, errno);
}

LockFile::LockFile(fs::path target, fs::path lock_path, int fd)
    : target_(std::move(target)), lock_path_(std::move(lock_path)), fd_(fd), owned_(true) {}

LockFile::LockFile(LockFile&& other) noexcept
    : target_(std::move(other.target_)),
      lock_path_(std::move(other.lock_path_)),
      fd_(std::exchange(other.fd_, -1)),
      owned_(std::exchange(other.owned_, false)) {}

LockFile& LockFile::operator=(LockFile&& other) noexcept {
  if (this != &other) {
    release();
    target_ = std::move(other.target_);
    lock_path_ = std::move(other.lock_path_);
    fd_ = std::exchange(other.fd_, -1);
    owned_ = std::exchange(other.owned_, false);
  }
  return *this;
}

LockFile::~LockFile() { release(); }

void LockFile::release() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  if (owned_) {
    ::unlink(lock_path_.c_str());
    owned_ = false;
  }
}

std::optional<LockFile> LockFile::try_acquire(const fs::path& target) {
  fs::path lock_path = target;
  lock_path += ".lock";
  int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
  if (fd < 0) {
    if (errno == EEXIST) return std::nullopt;
    throw_errno("cannot create lock file", lock_path, errno);
  }
  return LockFile(target, std::move(lock_path), fd);
}

void LockFile::write(core::ByteView data) {
  if (!owned_ || fd_ < 0) throw Error("lock file is not held");
  write_all(fd_, data, lock_path_);
}

void LockFile::commit() {
  if (!owned_ || fd_ < 0) throw Error("lock file is not held");
  if (::fsync(fd_) != 0) throw_errno("cannot fsync", lock_path_, errno);
  int rc = ::close(fd_);
  fd_ = -1;
  if (rc != 0) throw_errno("cannot close", lock_path_, errno);
  if (::rename(lock_path_.c_str(), target_.c_str()) != 0) {
    throw_errno("cannot rename lock file onto", target_, errno);
  }
  owned_ = false;  // the lock file *is* the target now
}

FileStat lstat(const fs::path& p) {
  struct ::stat st{};
  if (::lstat(p.c_str(), &st) != 0) throw_errno("cannot lstat", p, errno);
  return {static_cast<std::uint64_t>(st.st_size),
          static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1'000'000'000 + st.st_mtim.tv_nsec,
          static_cast<std::uint64_t>(st.st_ino), static_cast<std::uint32_t>(st.st_mode)};
}

}  // namespace tva::platform
