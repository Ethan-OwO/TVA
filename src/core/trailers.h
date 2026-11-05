#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tva::core {

// One "Key: value" line of a commit message's trailer block, the format
// `git interpret-trailers` reads and writes (Signed-off-by: ..., etc.).
struct Trailer {
  std::string key;
  std::string value;

  bool operator==(const Trailer&) const = default;
};

struct MessageParts {
  std::string body;               // the message minus its trailer block, trailing whitespace trimmed
  std::vector<Trailer> trailers;  // in message order
};

// [A-Za-z0-9-]+, what git accepts as a trailer key.
bool is_trailer_token(std::string_view key);

// git's trailer block is the last paragraph of a message, provided that it
// is not also the first paragraph (the subject never counts) and that every
// line in it is "Token: value" or a continuation line starting with
// whitespace. Without such a block, `trailers` is empty and `body` is the
// whole message.
MessageParts split_trailers(std::string_view message);

// "<body>\n\n<key>: <value>\n..." -- the body's trailing whitespace is
// trimmed and newlines inside values become spaces. Throws if a key is not a
// token or if the body is empty while trailers are given (git would read a
// lone paragraph as the subject, not as trailers).
std::string append_trailers(std::string_view body, const std::vector<Trailer>& trailers);

// Value of the first trailer whose key equals `key`, ignoring ASCII case
// (git compares trailer keys case-insensitively).
std::optional<std::string> trailer_value(const std::vector<Trailer>& trailers, std::string_view key);

}  // namespace tva::core
