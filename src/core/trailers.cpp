#include "core/trailers.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace tva::core {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_blank(std::string_view line) {
  for (char c : line) {
    if (!is_space(c)) return false;
  }
  return true;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

std::string_view trim_right(std::string_view s) {
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

// Lines without their '\n'; a final '\n' does not start another line.
std::vector<std::string_view> split_lines(std::string_view s) {
  std::vector<std::string_view> lines;
  while (!s.empty()) {
    std::size_t nl = s.find('\n');
    if (nl == std::string_view::npos) {
      lines.push_back(s);
      break;
    }
    lines.push_back(s.substr(0, nl));
    s.remove_prefix(nl + 1);
  }
  return lines;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool equals_ignore_case(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

}  // namespace

bool is_trailer_token(std::string_view key) {
  if (key.empty()) return false;
  for (char c : key) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}

MessageParts split_trailers(std::string_view message) {
  const std::vector<std::string_view> lines = split_lines(message);
  MessageParts whole{std::string(trim_right(message)), {}};

  std::size_t end = lines.size();  // one past the last non-blank line
  while (end > 0 && is_blank(lines[end - 1])) --end;
  std::size_t start = end;  // first line of the last paragraph
  while (start > 0 && !is_blank(lines[start - 1])) --start;
  std::size_t first = 0;  // first line of the first paragraph
  while (first < end && is_blank(lines[first])) ++first;
  if (start >= end || start <= first) return whole;  // no text, or the subject is the last paragraph

  std::vector<Trailer> trailers;
  for (std::size_t i = start; i < end; ++i) {
    std::string_view line = lines[i];
    if (line.front() == ' ' || line.front() == '\t') {
      if (trailers.empty()) return whole;  // a block cannot start with a continuation
      std::string_view more = trim(line);
      if (!more.empty()) trailers.back().value += " " + std::string(more);
      continue;
    }
    std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || !is_trailer_token(line.substr(0, colon))) return whole;
    trailers.push_back({std::string(line.substr(0, colon)), std::string(trim(line.substr(colon + 1)))});
  }

  std::string body;
  for (std::size_t i = 0; i < start; ++i) {
    body.append(lines[i]);
    body += '\n';
  }
  return {std::string(trim_right(body)), std::move(trailers)};
}

std::string append_trailers(std::string_view body, const std::vector<Trailer>& trailers) {
  std::string out(trim_right(body));
  if (trailers.empty()) return out.empty() ? out : out + "\n";
  if (out.empty()) throw Error("a commit message with trailers needs a non-empty body");
  out += "\n\n";
  for (const Trailer& t : trailers) {
    if (!is_trailer_token(t.key)) throw Error("invalid trailer key '" + t.key + "'");
    std::string value(trim(t.value));
    for (char& c : value) {
      if (c == '\n' || c == '\r') c = ' ';
    }
    out += t.key + ": " + value + "\n";
  }
  return out;
}

std::optional<std::string> trailer_value(const std::vector<Trailer>& trailers, std::string_view key) {
  for (const Trailer& t : trailers) {
    if (equals_ignore_case(t.key, key)) return t.value;
  }
  return std::nullopt;
}

}  // namespace tva::core
