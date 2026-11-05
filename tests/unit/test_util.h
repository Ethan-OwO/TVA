#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <unistd.h>

#include "core/bytes.h"
#include "platform/fs.h"

namespace tva::test {

// A fresh directory under the system temp dir, removed on destruction.
class TempDir {
 public:
  TempDir() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("tva-unit-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

inline void write_text(const std::filesystem::path& p, std::string_view text) {
  std::filesystem::create_directories(p.parent_path());
  platform::write_file_atomic(p, core::as_bytes(text));
}

inline std::string read_text(const std::filesystem::path& p) {
  auto bytes = platform::read_file(p);
  return std::string(core::as_chars(bytes));
}

}  // namespace tva::test
