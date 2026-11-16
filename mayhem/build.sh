#!/usr/bin/env bash
#
# mayhem/build.sh — builds the MySQL Workbench library slices the harnesses fuzz (ANTLR MySQL
# lexer/parser, base string/sqlstring/config-file code), twice:
#   * sanitized + SanCov (`build-fuzz/fuzz/`)  -> the libFuzzer targets and their -standalone reproducers
#   * the project's normal flags (`build-fuzz/norm/`) -> what mayhem/test.sh runs: upstream's casmine specs
#     for the fuzzed base code (`build-fuzz/casmine-specs`) and the known-answer probe (`build-fuzz/kat`)
# Everything resolves from the image: apt -dev packages plus the pinned ANTLR 4.13.2 tool jar and C++
# runtime under /opt/toolchains/deps/antlr4-4.13.2 (no network, safe to re-run).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

ANTLR=/opt/toolchains/deps/antlr4-4.13.2
B="$SRC/mayhem/build-fuzz"
GEN="$B/gen"
mkdir -p "$B/fuzz" "$B/norm" "$B/casmine" "$GEN/mysql"

# --- 1) the ANTLR-generated MySQL lexer/parser (upstream generates them at build time, with this exact tool)
if [ ! -f "$GEN/mysql/MySQLParser.cpp" ] || [ "$SRC/library/parsers/grammars/MySQLParser.g4" -nt "$GEN/mysql/MySQLParser.cpp" ] \
   || [ "$SRC/library/parsers/grammars/MySQLLexer.g4" -nt "$GEN/mysql/MySQLParser.cpp" ]; then
  (cd "$SRC/library/parsers/grammars" && java -Xmx1024m -jar "$ANTLR/lib/antlr-4.13.2-complete.jar" \
      -Dlanguage=Cpp -listener -visitor -o "$GEN/mysql" -package parsers MySQLLexer.g4 MySQLParser.g4)
fi

INCLUDES="-I$SRC/library/base -I$SRC/library/parsers -I$SRC/library/parsers/mysql -I$GEN -I$GEN/mysql \
  -I$ANTLR/include/antlr4-runtime -I$SRC/res -I$SRC/backend/wbprivate $(pkg-config --cflags glib-2.0 libxml-2.0)"
CXXSTD="-std=c++17 -D__STDC_FORMAT_MACROS -Wno-deprecated-declarations -w"

LIB_SOURCES=(
  library/base/string_utilities.cpp
  library/base/sqlstring.cpp
  library/base/utf8string.cpp
  library/base/config_file.cpp
  library/base/symbol-info.cpp
  library/base/log.cpp
  library/base/file_functions.cpp
  library/base/file_utilities.cpp
  library/parsers/parsers-common.cpp
  library/parsers/SymbolTable.cpp
  library/parsers/mysql/MySQLBaseLexer.cpp
  library/parsers/mysql/MySQLBaseRecognizer.cpp
  library/parsers/mysql/MySQLRecognizerCommon.cpp
)
GEN_SOURCES=(
  "$GEN/mysql/MySQLLexer.cpp"
  "$GEN/mysql/MySQLParser.cpp"
  "$GEN/mysql/MySQLParserBaseListener.cpp"
)

# Fuzz flavor: the project is instrumented (sanitizers + SanCov); DWARF <= 3.
FUZZ_FLAGS="$CXXSTD $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
# Oracle flavor: the project's normal flags (no sanitizers, no DWARF pin), optionally coverage-instrumented.
NORM_FLAGS="$CXXSTD -O2 $COVERAGE_FLAGS"

obj_for() { # <flavor> <source>
  local base="${2//\//_}"
  echo "$B/$1/${base%.cpp}.o"
}

jobs_file=$(mktemp)
trap 'rm -f "$jobs_file"' EXIT
queue() { # <flavor> <flags> <source>
  local o; o=$(obj_for "$1" "${3#$SRC/}")
  if [ ! -f "$o" ] || [ "$3" -nt "$o" ]; then
    printf '%s -c %s %s %s -o %s\n' "$CXX" "$2" "$INCLUDES" "$3" "$o" >> "$jobs_file"
  fi
}
for s in "${LIB_SOURCES[@]}"; do
  queue fuzz "$FUZZ_FLAGS" "$SRC/$s"
  queue norm "$NORM_FLAGS" "$SRC/$s"
done
for s in "${GEN_SOURCES[@]}"; do
  queue fuzz "$FUZZ_FLAGS" "$s"
  queue norm "$NORM_FLAGS" "$s"
done
# Largest translation unit first so the long pole starts immediately.
sort -r -k1,1 "$jobs_file" | xargs -P "$MAYHEM_JOBS" -I{} bash -c '{}'

objs() { # <flavor>
  local s
  for s in "${LIB_SOURCES[@]}"; do obj_for "$1" "$s"; done
  for s in "${GEN_SOURCES[@]}"; do obj_for "$1" "${s#$SRC/}"; done
}
LIBS="$ANTLR/lib/libantlr4-runtime.a $(pkg-config --libs glib-2.0) -lpthread"

# --- 2) LeakSanitizer off for every fuzz/standalone binary (leaks are not what this fleet fuzzes for)
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

# --- 3) the harnesses: libFuzzer binary + non-fuzzer run-once reproducer
for h in fuzz_mysql_parser fuzz_sqlstring fuzz_string_utils fuzz_config_file; do
  $CXX $FUZZ_FLAGS $INCLUDES -c "$SRC/mayhem/$h.cc" -o "$B/fuzz/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/$h.o" $(objs fuzz) "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$B/fuzz/$h.o" "$B/fuzz/standalone_main.o" $(objs fuzz) "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$h-standalone"
done

# --- 4) the known-answer probe mayhem/test.sh runs: the project's NORMAL flags (no sanitizers), dynamic glib
$CXX $NORM_FLAGS $INCLUDES "$SRC/mayhem/kat.cc" $(objs norm) $LIBS -o "$B/kat"

# --- 5) upstream's own unit tests for the fuzzed base code: the casmine framework
# (testing/test-suite/casmine, built as its CMakeLists does: -DRAPIDJSON_HAS_STDSTRING) and the base specs
# that need nothing beyond library/base (plus data_types.cpp, the code commandlineparser_specs tests), linked against the same NORMAL-flag objects as the probe.
# The rest of upstream's wbtests-bin (GRT, DB, forms, canvas specs) needs the full workbench application
# stack (GRT, mforms, MySQL Connector/C++, GDAL, cairo...) and is not built here.
CAS="$SRC/testing/test-suite/casmine"
SPECS=(
  testing/test-suite/tests/library/base/sqlstring_specs.cpp
  testing/test-suite/tests/library/base/stringutilities_specs.cpp
  testing/test-suite/tests/library/base/utf8string_specs.cpp
  testing/test-suite/tests/library/base/commandlineparser_specs.cpp
  library/base/data_types.cpp
)
CAS_INCLUDES="-I$CAS -I$SRC/library -I$SRC/library/base -I$SRC/library/base/base $(pkg-config --cflags glib-2.0 libxml-2.0)"
# casmine/helpers.h names a bare `nullptr_t` (GCC-only); mayhem/casmine_compat.h provides it under clang.
CAS_FLAGS="$CXXSTD -O2 -DRAPIDJSON_HAS_STDSTRING -include $SRC/mayhem/casmine_compat.h $COVERAGE_FLAGS"
cas_objs=()
for s in "$CAS"/{ansi-styles,casmine,describe,matchers,reporter,helpers}.cpp "${SPECS[@]/#/$SRC/}" "$SRC/mayhem/casmine_main.cc"; do
  o="$B/casmine/$(basename "${s%.*}").o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
    $CXX -c $CAS_FLAGS $CAS_INCLUDES $INCLUDES "$s" -o "$o"
  fi
  cas_objs+=("$o")
done
$CXX $NORM_FLAGS "${cas_objs[@]}" $(objs norm) $LIBS -o "$B/casmine-specs"
