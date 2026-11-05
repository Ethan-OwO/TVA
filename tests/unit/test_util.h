#pragma once

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
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

// Sets environment variables for one test and restores them afterwards.
class EnvGuard {
 public:
  EnvGuard() = default;
  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;
  ~EnvGuard() {
    for (auto& [name, old] : saved_) {
      if (old) {
        ::setenv(name.c_str(), old->c_str(), 1);
      } else {
        ::unsetenv(name.c_str());
      }
    }
  }

  // nullptr unsets the variable.
  void set(const char* name, const char* value) {
    remember(name);
    if (value) {
      ::setenv(name, value, 1);
    } else {
      ::unsetenv(name);
    }
  }

 private:
  void remember(const char* name) {
    if (saved_.count(name)) return;
    const char* v = std::getenv(name);
    saved_[name] = v ? std::optional<std::string>(v) : std::nullopt;
  }

  std::map<std::string, std::optional<std::string>> saved_;
};

}  // namespace tva::test
