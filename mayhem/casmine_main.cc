// mayhem/casmine_main.cc — minimal driver for upstream's casmine test framework
// (testing/test-suite/casmine), standing in for testing/test-suite/main.cpp, which pulls in the
// GRT/workbench headers (data_types.h, wb_version.h). It runs every spec linked into the binary,
// the same way upstream's main does: casmine::CasmineContext::get()->runTests(<baseDir>).
// <baseDir> must contain a `data/` directory; casmine copies it to <baseDir>/tmpdata and writes
// <baseDir>/output and <baseDir>/test-results.json, so test.sh passes a scratch directory.
#include "casmine.h"

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <scratch base dir containing data/>" << std::endl;
    return 2;
  }
  auto context = casmine::CasmineContext::get();
  // Configuration must be a JSON object (upstream reads it from casmine-config.json); ping off.
  context->configuration.Parse("{\"reporting\":{\"ping\":0}}");
  context->settings["no-colors"] = true;
  context->runTests(argv[1]);
  return 0;
}
