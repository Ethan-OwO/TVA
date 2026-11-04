#include "core/config.h"

#include <algorithm>
#include <cctype>

#include "core/bytes.h"
#include "core/error.h"
#include "platform/fs.h"

namespace tva::core {
namespace fs = std::filesystem;

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

class Parser {
 public:
  Parser(std::string_view text, std::string_view origin) : text_(text), origin_(origin) {}

  void run(std::unordered_map<std::string, std::string>& out) {
    while (!text_.empty()) parse_line(take_line(), out);
  }

 private:
  // Next line without its "\n" (or "\r\n").
  std::string_view take_line() {
    std::size_t nl = text_.find('\n');
    std::string_view line = text_.substr(0, nl);
    text_.remove_prefix(nl == std::string_view::npos ? text_.size() : nl + 1);
    if (line.ends_with('\r')) line.remove_suffix(1);
    ++line_no_;
    return line;
  }

  [[noreturn]] void fail(const std::string& what) const {
    throw Error("bad config line " + std::to_string(line_no_) + " in " + std::string(origin_) +
                ": " + what);
  }

  static std::string_view skip_space(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    return s;
  }

  void parse_line(std::string_view line, std::unordered_map<std::string, std::string>& out) {
    line = skip_space(line);
    if (!line.empty() && line.front() == '[') line = skip_space(parse_section(line));
    if (line.empty() || line.front() == '#' || line.front() == ';') return;

    std::size_t n = 0;
    while (n < line.size() && (std::isalnum(static_cast<unsigned char>(line[n])) || line[n] == '-')) ++n;
    if (n == 0 || !std::isalpha(static_cast<unsigned char>(line[0]))) fail("invalid key");
    if (section_.empty()) fail("key outside of a section");
    std::string key = section_ + "." + lower(line.substr(0, n));
    std::string_view rest = skip_space(line.substr(n));
    if (rest.empty() || rest.front() == '#' || rest.front() == ';') {
      out[key] = "true";  // "[core]\n bare" means bare = true
    } else if (rest.front() == '=') {
      out[key] = parse_value(rest.substr(1));
    } else {
      fail("expected '='");
    }
  }

  // Parses "[name]" or [name "sub"]; returns what follows the ']'.
  std::string_view parse_section(std::string_view line) {
    std::size_t i = 1;
    std::size_t name_end = i;
    while (name_end < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[name_end])) || line[name_end] == '-' ||
            line[name_end] == '.')) {
      ++name_end;
    }
    if (name_end == i) fail("invalid section name");
    section_ = lower(line.substr(i, name_end - i));
    i = name_end;
    if (i < line.size() && is_space(line[i])) {
      while (i < line.size() && is_space(line[i])) ++i;
      if (i >= line.size() || line[i] != '"') fail("invalid section header");
      std::string sub;
      for (++i;; ++i) {
        if (i >= line.size()) fail("unterminated subsection name");
        if (line[i] == '"') break;
        if (line[i] == '\\' && i + 1 < line.size()) ++i;
        sub += line[i];
      }
      section_ += "." + sub;  // subsection names keep their case
      ++i;
    }
    if (i >= line.size() || line[i] != ']') fail("invalid section header");
    return line.substr(i + 1);
  }

  // Mirrors git's parse_value(): unquoted whitespace is trimmed at both ends
  // but kept inside the value; quotes may cover any part of the value; a
  // trailing backslash continues the value on the next line.
  std::string parse_value(std::string_view s) {
    std::string value;
    bool quoted = false;
    std::size_t keep = 0;  // length without trailing unquoted whitespace
    for (std::size_t i = 0; i < s.size(); ++i) {
      char c = s[i];
      if (!quoted && is_space(c)) {
        if (!value.empty()) value += c;
        continue;
      }
      if (!quoted && (c == '#' || c == ';')) break;
      if (c == '"') {
        quoted = !quoted;
        keep = value.size();
        continue;
      }
      if (c == '\\') {
        if (++i >= s.size()) {
          // Backslash-newline: the value continues on the next line.
          if (text_.empty()) break;
          s = take_line();
          i = static_cast<std::size_t>(-1);  // ++i in the loop header makes it 0
          continue;
        }
        switch (s[i]) {
          case 'n': c = '\n'; break;
          case 't': c = '\t'; break;
          case 'b': c = '\b'; break;
          case '"':
          case '\\': c = s[i]; break;
          default: fail("invalid escape sequence");
        }
      }
      value += c;
      keep = value.size();
    }
    if (quoted) fail("unterminated quote");
    value.resize(keep);
    return value;
  }

  std::string_view text_;
  std::string_view origin_;
  std::string section_;
  int line_no_ = 0;
};

}  // namespace

Config Config::load_files(const std::vector<fs::path>& files) {
  Config config;
  for (const auto& file : files) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) continue;
    config.add_text(as_chars(platform::read_file(file)), file.string());
  }
  return config;
}

void Config::add_text(std::string_view text, std::string_view origin) {
  Parser(text, origin).run(values_);
}

std::optional<std::string> Config::get(std::string_view section, std::string_view key) const {
  std::size_t dot = section.find('.');
  std::string full = lower(section.substr(0, dot));
  if (dot != std::string_view::npos) full += std::string(section.substr(dot));
  full += "." + lower(key);
  if (auto it = values_.find(full); it != values_.end()) return it->second;
  return std::nullopt;
}

}  // namespace tva::core
