// Fuzzes MySQL Workbench's ANTLR4 MySQL lexer + parser the way the application drives it
// (SLL with bail-out first, then full LL with the default error strategy), then walks the tree.
// Input layout: byte 0 selects the server version, byte 1 the SQL mode flags (low 5 bits) and
// whether to additionally determine the query type (bit 5); the rest is the SQL text.
#include <cstddef>
#include <cstdint>
#include <set>
#include <memory>
#include <string>

#include "antlr4-runtime.h"
#include "base/log.h"

#include "mysql/MySQLLexer.h"
#include "mysql/MySQLParser.h"
#include "mysql/MySQLParserBaseListener.h"

using namespace antlr4;
using namespace parsers;

namespace {
// Upstream logging dereferences a null impl unless some Logger instance exists.
base::Logger gLogger(false, "");


const long kVersions[] = {50500, 50600, 50700, 80000, 80016, 80021, 80029, 80040};

// A representative charset list, as the application obtains it from its RDBMS description.
const std::set<std::string> kCharsets = {"_armscii8", "_ascii",  "_big5",    "_binary", "_cp1250",   "_cp1251",
                                         "_cp850",    "_cp852",  "_cp866",   "_cp932",  "_dec8",     "_euckr",
                                         "_gb2312",   "_gbk",    "_greek",   "_hebrew", "_koi8r",    "_latin1",
                                         "_latin2",   "_sjis",   "_swe7",    "_tis620", "_ucs2",     "_ujis",
                                         "_utf16",    "_utf16le", "_utf32",  "_utf8",   "_utf8mb3",  "_utf8mb4"};

class SilentErrors : public BaseErrorListener {
public:
  void syntaxError(Recognizer *, Token *, size_t, size_t, const std::string &, std::exception_ptr) override {}
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return -1;

  long version = kVersions[data[0] % (sizeof(kVersions) / sizeof(kVersions[0]))];
  auto mode = static_cast<MySQLRecognizerCommon::SqlMode>(data[1] & 0x1f);
  bool alsoQueryType = (data[1] & 0x20) != 0;
  std::string sql(reinterpret_cast<const char *>(data) + 2, size - 2);

  // The ANTLR runtime rejects ill-formed UTF-8 up front; that is the documented input contract.
  std::unique_ptr<ANTLRInputStream> inputPtr;
  try {
    inputPtr.reset(new ANTLRInputStream(sql));
  } catch (IllegalArgumentException &) {
    return -1;
  }
  ANTLRInputStream &input = *inputPtr;
  SilentErrors errors;

  if (alsoQueryType) {
    MySQLLexer typeLexer(&input);
    typeLexer.serverVersion = version;
    typeLexer.sqlMode = mode;
    typeLexer.charsets = kCharsets;
    typeLexer.removeErrorListeners();
    typeLexer.determineQueryType();
    input.reset();
  }

  MySQLLexer lexer(&input);
  lexer.serverVersion = version;
  lexer.sqlMode = mode;
  lexer.charsets = kCharsets;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);

  CommonTokenStream tokens(&lexer);
  MySQLParser parser(&tokens);
  parser.serverVersion = version;
  parser.sqlMode = mode;
  parser.removeErrorListeners();

  auto bailOut = std::make_shared<BailErrorStrategy>();
  parser.setErrorHandler(bailOut);
  parser.getInterpreter<atn::ParserATNSimulator>()->setPredictionMode(atn::PredictionMode::SLL);

  try {
    tokens.fill();
  } catch (IllegalStateException &) {
    return 0;
  }

  MySQLParser::QueryContext *tree = nullptr;
  try {
    tree = parser.query();
  } catch (ParseCancellationException &) {
    // Either a real syntax error or SLL was not enough: retry with full LL and error recovery.
    tokens.reset();
    parser.reset();
    parser.setErrorHandler(std::make_shared<DefaultErrorStrategy>());
    parser.getInterpreter<atn::ParserATNSimulator>()->setPredictionMode(atn::PredictionMode::LL);
    parser.addErrorListener(&errors);
    tree = parser.query();
  }

  MySQLParserBaseListener listener;
  tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
  MySQLRecognizerCommon::dumpTree(tree, parser.getVocabulary());
  return 0;
}
