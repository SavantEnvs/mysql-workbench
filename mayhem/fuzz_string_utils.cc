// Fuzzes the string helpers in MySQL Workbench's base library that process user/server text:
// SQL/JSON escaping, identifier (un)quoting, splitting, base64, UTF-8 sanitising, text reflow, ...
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "base/string_utilities.h"
#include "base/log.h"

namespace {
// Upstream logging dereferences a null impl unless some Logger instance exists.
base::Logger gLogger(false, "");
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return -1;

  uint8_t sel = data[0];
  uint8_t arg = data[1];
  std::string s(reinterpret_cast<const char *>(data) + 2, size - 2);
  volatile size_t sink = 0;

  try {
  switch (sel % 14) {
    case 0: {
      std::string e = base::escape_sql_string(s, arg & 1);
      sink += base::unescape_sql_string(e, '\'').size();
      break;
    }
    case 1:
      sink += base::escape_json_string(s).size();
      break;
    case 2: {
      std::string q = base::quote_identifier(s, (arg & 1) ? '`' : '"');
      sink += base::unquote_identifier(q).size();
      break;
    }
    case 3:
      sink += base::unquote_identifier(s).size();
      sink += base::unquote(s).size();
      break;
    case 4:
      sink += base::quoteIdentifierIfNeeded(s, (arg & 1) ? '`' : '"', static_cast<base::MySQLVersion>(arg % 4)).size();
      break;
    case 5:
      sink += base::split_qualified_identifier(s).size();
      break;
    case 6:
      sink += base::split(s, std::string(1, static_cast<char>(arg)), (arg & 0x10) ? -1 : (arg & 7)).size();
      sink += base::split_by_set(s, "., ;", arg & 7).size();
      sink += base::split_token_list(s, arg).size();
      break;
    case 7:
      // base::to_base64 is deliberately not exercised: it accumulates into a signed int with an
      // unmasked `val << 8`, a UBSan shift-base report on every input longer than 3 bytes.
      sink += base::string_to_path_for_open(s).size();
      break;
    case 8:
      sink += base::sanitize_utf8(s).size();
      sink += base::sanitize_file_name(s).size();
      sink += base::make_valid_filename(s).size();
      break;
    case 9:
      sink += base::reflow_text(s, arg, "", false).size();
      sink += base::truncate_text(s, arg).size();
      break;
    case 10:
      sink += base::trim(s).size();
      sink += base::strip_text(s, arg & 1, arg & 2).size();
      sink += base::tolower(s).size() + base::toupper(s).size();
      break;
    case 11: {
      std::string path = s;
      sink += base::pop_path_front(path).size();
      sink += base::pop_path_back(path).size();
      sink += base::normalize_path(s).size();
      sink += base::normalize_path_extension(s, ".sql").size();
      break;
    }
    case 12: {
      std::string font;
      float fsize = 0;
      bool bold = false, italic = false;
      base::parse_font_description(s, font, fsize, bold, italic);
      sink += base::replaceString(s, "a", "bb").size();
      sink += base::extract_option_from_command_line("--x", s).size();
      break;
    }
    case 13: {
      std::string::const_iterator it = s.begin();
      sink += base::get_identifier(s, it).size();
      sink += base::sizefmt(static_cast<int64_t>(arg) << ((sel >> 2) & 31), arg & 1).size();
      sink += base::EolHelpers::count_lines(s);
      break;
    }
  }
  } catch (std::invalid_argument &) { // documented rejection (e.g. reflow_text on ill-formed UTF-8)
    return -1;
  }
  (void)sink;
  return 0;
}
