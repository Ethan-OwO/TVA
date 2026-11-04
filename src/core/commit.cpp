#include "core/commit.h"

#include <cstdio>
#include <cstdlib>
#include <optional>

#include "core/error.h"

namespace tva::core {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string_view trim_right(std::string_view s) {
  while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
  return s;
}

std::string_view trim_left(std::string_view s) {
  while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
  return s;
}

std::optional<std::int64_t> parse_int(std::string_view s) {
  bool negative = s.starts_with('-');
  if (negative) s.remove_prefix(1);
  if (s.empty() || s.size() > 18) return std::nullopt;
  std::int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    v = v * 10 + (c - '0');
  }
  return negative ? -v : v;
}

std::optional<int> parse_tz(std::string_view s) {
  if (s.size() != 5 || (s[0] != '+' && s[0] != '-')) return std::nullopt;
  auto digits = parse_int(s.substr(1));
  if (!digits || s[1] == '-') return std::nullopt;
  int minutes = static_cast<int>(*digits / 100 * 60 + *digits % 100);
  return s[0] == '-' ? -minutes : minutes;
}

ObjectId parse_id(std::string_view hex, std::string_view what) {
  auto id = ObjectId::from_hex(hex);
  if (!id) throw Error("malformed " + std::string(what) + " line in commit");
  return *id;
}

void check_ident_part(const std::string& s) {
  if (s.find_first_of("<>\n") != std::string::npos) {
    throw Error("invalid identity '" + s + "': must not contain '<', '>' or newline");
  }
}

// Howard Hinnant's days -> civil date algorithm (proleptic Gregorian).
void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const auto doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

}  // namespace

std::string format_tz(int offset_minutes) {
  int a = std::abs(offset_minutes);
  char buf[8];
  std::snprintf(buf, sizeof buf, "%c%02d%02d", offset_minutes < 0 ? '-' : '+', a / 60 % 100,
                a % 60);
  return buf;
}

std::string format_signature(const Signature& sig) {
  return sig.name + " <" + sig.email + "> " + std::to_string(sig.when_unix) + " " +
         format_tz(sig.tz_offset_minutes);
}

Signature parse_signature(std::string_view s) {
  // The email ends at the *last* '>' (git does the same, to survive broken
  // idents with a stray '>' inside the address).
  std::size_t lt = s.find('<');
  std::size_t gt = s.rfind('>');
  if (lt == std::string_view::npos || gt == std::string_view::npos || gt < lt) {
    throw Error("malformed signature '" + std::string(s) + "'");
  }
  Signature sig;
  sig.name = std::string(trim_right(s.substr(0, lt)));
  sig.email = std::string(s.substr(lt + 1, gt - lt - 1));
  std::string_view rest = trim_left(s.substr(gt + 1));
  std::size_t space = rest.find(' ');
  auto when = parse_int(rest.substr(0, space));
  auto tz = space == std::string_view::npos ? std::nullopt : parse_tz(trim_right(rest.substr(space + 1)));
  if (!when || !tz) throw Error("malformed signature date '" + std::string(rest) + "'");
  sig.when_unix = *when;
  sig.tz_offset_minutes = *tz;
  return sig;
}

Bytes serialize_commit(const Commit& c) {
  for (const Signature* sig : {&c.author, &c.committer}) {
    check_ident_part(sig->name);
    check_ident_part(sig->email);
  }
  std::string out = "tree " + c.tree.hex() + "\n";
  for (const auto& p : c.parents) out += "parent " + p.hex() + "\n";
  out += "author " + format_signature(c.author) + "\n";
  out += "committer " + format_signature(c.committer) + "\n";
  out += "\n";
  out += c.message;
  if (!c.message.empty() && c.message.back() != '\n') out += '\n';
  return to_bytes(out);
}

Commit parse_commit(ByteView payload) {
  std::string_view s = as_chars(payload);
  Commit c;
  bool have_tree = false, have_author = false, have_committer = false;
  std::size_t pos = 0;
  while (pos < s.size()) {
    std::size_t nl = s.find('\n', pos);
    std::size_t end = nl == std::string_view::npos ? s.size() : nl;
    std::string_view line = s.substr(pos, end - pos);
    pos = nl == std::string_view::npos ? s.size() : nl + 1;
    if (line.empty()) {  // end of headers
      c.message = std::string(s.substr(pos));
      break;
    }
    if (line.front() == ' ') continue;  // continuation of a multi-line header
    std::size_t space = line.find(' ');
    std::string_view key = line.substr(0, space);
    std::string_view value = space == std::string_view::npos ? "" : line.substr(space + 1);
    if (key == "tree") {
      if (have_tree) throw Error("malformed commit: multiple tree lines");
      c.tree = parse_id(value, "tree");
      have_tree = true;
    } else if (key == "parent") {
      c.parents.push_back(parse_id(value, "parent"));
    } else if (key == "author") {
      if (have_author) throw Error("malformed commit: multiple author lines");
      c.author = parse_signature(value);
      have_author = true;
    } else if (key == "committer") {
      if (have_committer) throw Error("malformed commit: multiple committer lines");
      c.committer = parse_signature(value);
      have_committer = true;
    }
  }
  if (!have_tree || !have_author || !have_committer) {
    throw Error("malformed commit: missing tree, author or committer");
  }
  return c;
}

std::string commit_subject(std::string_view message) {
  std::string subject;
  bool started = false;
  while (!message.empty()) {
    std::size_t nl = message.find('\n');
    std::string_view line = trim_right(message.substr(0, nl));
    message.remove_prefix(nl == std::string_view::npos ? message.size() : nl + 1);
    if (line.empty()) {
      if (started) break;  // end of the first paragraph
      continue;            // leading blank lines
    }
    if (started) subject += ' ';
    subject += line;
    started = true;
  }
  return subject;
}

std::string format_git_date(std::int64_t when_unix, int tz_offset_minutes) {
  static constexpr const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  const std::int64_t local = when_unix + std::int64_t{tz_offset_minutes} * 60;
  std::int64_t days = local / 86400;
  std::int64_t secs = local % 86400;
  if (secs < 0) {  // floor division for dates before 1970
    secs += 86400;
    --days;
  }
  std::int64_t year;
  unsigned month, day;
  civil_from_days(days, year, month, day);
  const auto weekday = static_cast<std::size_t>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
  char buf[64];
  std::snprintf(buf, sizeof buf, "%s %s %u %02d:%02d:%02d %lld %s", kDays[weekday],
                kMonths[month - 1], day, static_cast<int>(secs / 3600),
                static_cast<int>(secs / 60 % 60), static_cast<int>(secs % 60),
                static_cast<long long>(year), format_tz(tz_offset_minutes).c_str());
  return buf;
}

}  // namespace tva::core
