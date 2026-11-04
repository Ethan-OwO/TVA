#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tva::core {

// Read-only subset of git's config format: [section], [section "sub"],
// key = value, '#'/';' comments (also after a value), double quotes and the
// \" \\ \n \t \b escapes. Section and key names are case-insensitive,
// subsection names are not. Later files and later lines override earlier ones.
class Config {
 public:
  // Missing files are skipped; malformed ones throw.
  static Config load_files(const std::vector<std::filesystem::path>& files);
  void add_text(std::string_view text, std::string_view origin = "<text>");

  // `section` may name a subsection: get("remote.origin", "url").
  std::optional<std::string> get(std::string_view section, std::string_view key) const;

 private:
  std::unordered_map<std::string, std::string> values_;  // "section[.sub].key"
};

}  // namespace tva::core
