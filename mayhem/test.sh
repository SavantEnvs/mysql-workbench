#!/usr/bin/env bash
#
# mayhem/test.sh — runs what mayhem/build.sh built with the project's normal flags:
#   1) upstream's own casmine unit tests for the fuzzed base code (build-fuzz/casmine-specs):
#      testing/test-suite/tests/library/base/{sqlstring,stringutilities,utf8string,commandlineparser}_specs.cpp.
#      Excluded from upstream's wbtests-bin: every GRT/DB/forms/canvas/model spec (tests/library/grt,
#      cdbc, forms, mysql.canvas, mtemplates, tests/wb*, tests/backend...) and the base specs that include
#      GRT/workbench headers (config_file_specs -> grts/structs.workbench.h, fileutilities_specs -> grt.h).
#      They need the full workbench application stack (GRT, mforms, MySQL Connector/C++, GDAL, cairo),
#      which this image does not build. threading_specs (glib thread timing, not fuzzed code) is left out.
#   2) the known-answer probe (build-fuzz/kat) for what those specs do not cover: the my.cnf parser and
#      the ANTLR MySQL lexer/parser (upstream's mysql_parser_specs needs GRT). One `KAT <name> ok|FAIL`
#      line per check.
# Both results go into one CTRF summary; any failure, missing runner or short run fails the script.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

passed=0; failed=0; pending=0

# --- 1) upstream's casmine specs
SPECS_BIN="$SRC/mayhem/build-fuzz/casmine-specs"
if [ ! -x "$SPECS_BIN" ]; then
  echo "missing $SPECS_BIN — mayhem/build.sh must build it" >&2
  failed=$((failed + 1))
else
  base=$(mktemp -d "${TMPDIR:-/tmp}/casmine.XXXXXX")
  mkdir -p "$base/data"
  out=$(cd "$base" && "$SPECS_BIN" "$base" 2>&1); rc=$?
  rm -rf "$base"
  printf '%s\n' "$out"
  # ConsoleReporter summary: "<executed> of <total> tests executed (<ok> succeeded, <bad> failed, <p> pending, <d> disabled, <e> excluded)"
  line=$(printf '%s\n' "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -E '^[0-9]+ of [0-9]+ tests executed' | tail -n1)
  if [[ "$line" =~ ^([0-9]+)\ of\ ([0-9]+)\ tests\ executed\ \(([0-9]+)\ succeeded,\ ([0-9]+)\ failed,\ ([0-9]+)\ pending ]]; then
    executed=${BASH_REMATCH[1]}; total=${BASH_REMATCH[2]}
    passed=$((passed + BASH_REMATCH[3])); failed=$((failed + BASH_REMATCH[4])); pending=${BASH_REMATCH[5]}
    # Upstream marks 2 specs $pending (not run). A crash, a disabled/excluded spec, or a run that accounts
    # for fewer specs than are linked in is a failure.
    [ "$rc" -ne 0 ] && failed=$((failed + 1))
    [ $((executed + pending)) -ne "$total" ] && failed=$((failed + 1))
    [ "$total" -lt 55 ] && failed=$((failed + 1))
  else
    echo "casmine summary line not found (rc=$rc)" >&2
    failed=$((failed + 1))
  fi
fi

# --- 2) the known-answer probe
KAT="$SRC/mayhem/build-fuzz/kat"
if [ ! -x "$KAT" ]; then
  echo "missing $KAT — mayhem/build.sh must build it" >&2
  failed=$((failed + 1))
else
  out=$("$KAT" 2>&1); rc=$?
  printf '%s\n' "$out"
  kp=$(printf '%s\n' "$out" | grep -c '^KAT .* ok$')
  kf=$(printf '%s\n' "$out" | grep -c '^KAT .* FAIL$')
  passed=$((passed + kp)); failed=$((failed + kf))
  # A probe that exits non-zero or reports fewer checks than it contains is a failure, not a pass.
  [ "$rc" -ne 0 ] && failed=$((failed + 1))
  [ "$kp" -lt 21 ] && failed=$((failed + 1))
fi

emit_ctrf casmine+kat "$passed" "$failed" 0 "$pending"
