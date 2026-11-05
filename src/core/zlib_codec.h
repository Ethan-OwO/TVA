#pragma once

#include <cstddef>
#include <utility>

#include "core/bytes.h"

namespace tva::core {

// zlib *format* (RFC 1950: 2-byte header + deflate + adler32), which is what
// git uses for loose objects -- not raw deflate and not gzip.
Bytes zlib_deflate(ByteView data);

// Throws Error on corrupt, truncated or trailing-garbage input.
Bytes zlib_inflate(ByteView data);

// Inflates only the first `max_out` bytes of output (fewer if the stream ends
// first). Returns {output, compressed bytes consumed}. Used to read an object
// header without decompressing the whole payload.
std::pair<Bytes, std::size_t> zlib_inflate_prefix(ByteView data, std::size_t max_out);

}  // namespace tva::core
