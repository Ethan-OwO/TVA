#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.h"
#include "core/object_id.h"

namespace tva::core {

struct Signature {
  std::string name;
  std::string email;
  std::int64_t when_unix = 0;
  int tz_offset_minutes = 0;  // east of UTC, e.g. +0800 -> 480

  bool operator==(const Signature&) const = default;
};

std::string format_tz(int offset_minutes);  // 480 -> "+0800", -330 -> "-0530"
// "Name <email> 1700000000 +0800"
std::string format_signature(const Signature& sig);
Signature parse_signature(std::string_view line_after_keyword);

struct Commit {
  ObjectId tree;
  std::vector<ObjectId> parents;
  Signature author;
  Signature committer;
  std::string message;

  bool operator==(const Commit&) const = default;
};

// tree / parent* / author / committer headers, a blank line, then the
// message. Like git's strbuf_complete_line(), a non-empty message that does
// not end in '\n' gets one.
Bytes serialize_commit(const Commit& commit);
// Unknown headers (gpgsig, encoding, mergetag...) and their space-prefixed
// continuation lines are skipped.
Commit parse_commit(ByteView payload);

// git's %s: the first paragraph of the message, its lines joined by spaces.
std::string commit_subject(std::string_view message);

// git's default date format, in the signature's own time zone:
// "Wed Nov 15 06:13:20 2023 +0800".
std::string format_git_date(std::int64_t when_unix, int tz_offset_minutes);

}  // namespace tva::core
