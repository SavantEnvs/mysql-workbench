// Fuzzes base::sqlstring (MySQL Workbench's SQL query formatter): input = format string followed by
// a sequence of arguments (ints, doubles, strings, identifiers) picked by control bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

#include "base/sqlstring.h"
#include "base/log.h"

namespace {
// Upstream logging dereferences a null impl unless some Logger instance exists.
base::Logger gLogger(false, "");
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 3)
    return -1;

  int flags = data[0] & 3;
  size_t nargs = data[1] & 7;
  size_t fmtLen = data[2];
  data += 3;
  size -= 3;
  if (fmtLen > size)
    fmtLen = size;
  std::string format(reinterpret_cast<const char *>(data), fmtLen);
  data += fmtLen;
  size -= fmtLen;

  try {
    base::sqlstring q(format, flags);
    for (size_t i = 0; i < nargs && size > 0; i++) {
      uint8_t kind = data[0] % 6;
      data++;
      size--;
      size_t n = size < 8 ? size : 8;
      switch (kind) {
        case 0: {
          int32_t v = 0;
          memcpy(&v, data, n < sizeof(v) ? n : sizeof(v));
          q << v;
          break;
        }
        case 1: {
          int64_t v = 0;
          memcpy(&v, data, n);
          q << v;
          break;
        }
        case 2: {
          double v = 0;
          memcpy(&v, data, n);
          q << v;
          break;
        }
        default: {
          size_t len = size < 64 ? size : 64;
          std::string s(reinterpret_cast<const char *>(data), len);
          n = len;
          if (kind == 3)
            q << s.c_str();
          else if (kind == 4)
            q << base::sqlstring(s, 0);
          else
            q << s;
          break;
        }
      }
      data += n;
      size -= n;
    }
    std::string out = q;
    (void)out;
    (void)q.done();
  } catch (std::invalid_argument &) {
    return -1;
  }
  return 0;
}
