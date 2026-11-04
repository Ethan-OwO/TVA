#include "core/zlib_codec.h"

#define ZLIB_CONST
#include <zlib.h>

#include <algorithm>
#include <limits>
#include <string>

#include "core/error.h"

namespace tva::core {
namespace {

// avail_in / avail_out are 32-bit; feed larger buffers in pieces.
constexpr std::size_t kMaxChunk = std::numeric_limits<uInt>::max();
constexpr std::size_t kOutStep = 64 * 1024;

std::string zlib_message(const z_stream& zs, const char* fallback) {
  return std::string("zlib: ") + (zs.msg != nullptr ? zs.msg : fallback);
}

class Inflater {
 public:
  Inflater() {
    if (inflateInit(&zs_) != Z_OK) throw Error("zlib: inflateInit failed");
  }
  ~Inflater() { inflateEnd(&zs_); }
  Inflater(const Inflater&) = delete;
  Inflater& operator=(const Inflater&) = delete;

  // Inflates until `out` holds `limit` bytes or the stream ends. Returns true
  // when the end of the zlib stream was reached.
  bool run(ByteView in, Bytes& out, std::size_t limit) {
    for (;;) {
      if (zs_.avail_in == 0 && in_pos_ < in.size()) {
        std::size_t chunk = std::min(in.size() - in_pos_, kMaxChunk);
        zs_.next_in = in.data() + in_pos_;
        zs_.avail_in = static_cast<uInt>(chunk);
        in_pos_ += chunk;
      }
      if (out.size() >= limit) return false;
      std::size_t old = out.size();
      std::size_t room = std::min({kOutStep, limit - old, kMaxChunk});
      out.resize(old + room);
      zs_.next_out = out.data() + old;
      zs_.avail_out = static_cast<uInt>(room);
      int rc = inflate(&zs_, Z_NO_FLUSH);
      out.resize(old + (room - zs_.avail_out));
      switch (rc) {
        case Z_STREAM_END:
          return true;
        case Z_OK:
          break;
        case Z_BUF_ERROR:
          // No progress possible. Output space was available, so zlib wants
          // more input; if there is none left, the stream is truncated.
          if (zs_.avail_in == 0 && in_pos_ == in.size()) throw Error("zlib: truncated stream");
          break;
        default:  // Z_DATA_ERROR, Z_NEED_DICT, Z_MEM_ERROR, Z_STREAM_ERROR
          throw Error(zlib_message(zs_, "corrupt stream"));
      }
    }
  }

  // Compressed bytes consumed so far.
  std::size_t consumed() const { return in_pos_ - zs_.avail_in; }

 private:
  z_stream zs_{};
  std::size_t in_pos_ = 0;
};

}  // namespace

Bytes zlib_deflate(ByteView data) {
  z_stream zs{};
  if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) throw Error("zlib: deflateInit failed");
  Bytes out;
  std::size_t in_pos = 0;
  int rc = Z_OK;
  while (rc != Z_STREAM_END) {
    if (zs.avail_in == 0 && in_pos < data.size()) {
      std::size_t chunk = std::min(data.size() - in_pos, kMaxChunk);
      zs.next_in = data.data() + in_pos;
      zs.avail_in = static_cast<uInt>(chunk);
      in_pos += chunk;
    }
    int flush = (in_pos == data.size()) ? Z_FINISH : Z_NO_FLUSH;
    std::size_t old = out.size();
    out.resize(old + kOutStep);
    zs.next_out = out.data() + old;
    zs.avail_out = static_cast<uInt>(kOutStep);
    rc = deflate(&zs, flush);
    out.resize(old + (kOutStep - zs.avail_out));
    if (rc == Z_STREAM_ERROR) {
      deflateEnd(&zs);
      throw Error("zlib: deflate failed");
    }
  }
  deflateEnd(&zs);
  return out;
}

Bytes zlib_inflate(ByteView data) {
  Inflater inf;
  Bytes out;
  inf.run(data, out, std::numeric_limits<std::size_t>::max());
  if (inf.consumed() != data.size()) throw Error("zlib: trailing data after stream");
  return out;
}

std::pair<Bytes, std::size_t> zlib_inflate_prefix(ByteView data, std::size_t max_out) {
  Inflater inf;
  Bytes out;
  inf.run(data, out, max_out);
  return {std::move(out), inf.consumed()};
}

}  // namespace tva::core
