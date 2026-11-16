// Known-answer probe for mayhem/test.sh: runs MySQL Workbench's own base-library and parser code (built
// with the project's normal flags, no sanitizers) on fixed inputs and prints one `KAT <name> ok|FAIL`
// line per check. test.sh turns those lines into the CTRF counts, so the verdict comes from asserted
// values, not from an exit status.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "antlr4-runtime.h"
#include "base/config_file.h"
#include "base/log.h"
#include "base/sqlstring.h"
#include "base/string_utilities.h"
#include "mysql/MySQLLexer.h"
#include "mysql/MySQLParser.h"

using namespace antlr4;
using namespace parsers;

namespace {
base::Logger gLogger(false, "");

void check(const char *name, bool ok) {
  printf("KAT %s %s\n", name, ok ? "ok" : "FAIL");
}

size_t syntaxErrors(const std::string &sql, long version) {
  ANTLRInputStream input(sql);
  MySQLLexer lexer(&input);
  lexer.serverVersion = version;
  CommonTokenStream tokens(&lexer);
  MySQLParser parser(&tokens);
  parser.serverVersion = version;
  parser.removeParseListeners();
  parser.query();
  return parser.getNumberOfSyntaxErrors() + lexer.getNumberOfSyntaxErrors();
}

std::string firstRule(const std::string &sql) {
  ANTLRInputStream input(sql);
  MySQLLexer lexer(&input);
  lexer.serverVersion = 80029;
  CommonTokenStream tokens(&lexer);
  MySQLParser parser(&tokens);
  parser.serverVersion = 80029;
  tree::ParseTree *tree = parser.query();
  if (tree->children.empty())
    return "";
  auto *simple = dynamic_cast<MySQLParser::QueryContext *>(tree);
  if (!simple || !simple->simpleStatement())
    return "";
  auto *stmt = simple->simpleStatement();
  if (stmt->selectStatement())
    return "select";
  if (stmt->createStatement())
    return "create";
  if (stmt->insertStatement())
    return "insert";
  return "other";
}
} // namespace

int main() {
  // sqlstring
  {
    base::sqlstring q("SELECT ?, ? FROM !.!", 0);
    q << 42 << "O'Brien" << "db" << "t";
    check("sqlstring-format", std::string(q) == "SELECT 42, 'O\\'Brien' FROM `db`.`t`");
    base::sqlstring n("VALUES (?)", 0);
    n << (const char *)nullptr;
    check("sqlstring-null", std::string(n) == "VALUES (NULL)");
  }
  // string utilities
  check("escape-sql", base::escape_sql_string("a'b\"c\n") == "a\\'b\\\"c\\n");
  check("quote-identifier", base::quote_identifier("tbl", '`') == "`tbl`");
  check("unquote-identifier", base::unquote_identifier("`tbl`") == "tbl");
  {
    auto parts = base::split_qualified_identifier("`db`.tbl");
    check("split-qualified", parts.size() == 2 && parts[0] == "db" && parts[1] == "tbl");
  }
  check("trim", base::trim("  x y \t") == "x y");
  check("to-base64", base::to_base64("Man") == "TWFu");
  check("sizefmt", base::sizefmt(1536, false) == "1.50KiB");
  // configuration file
  {
    char path[] = "/tmp/kat_cfg_XXXXXX";
    int fd = mkstemp(path);
    const char *text = "[mysqld]\nport = 3306\nmax_allowed_packet = 16M\nskip-networking\n[client]\nuser=root\n";
    if (fd >= 0 && write(fd, text, strlen(text)) > 0) {
      close(fd);
      base::ConfigurationFile cfg(base::AutoCreateNothing);
      bool loaded = cfg.load(path);
      check("config-load", loaded && cfg.section_count() == 3 && cfg.key_count() == 4);
      check("config-int", cfg.get_int("port", "mysqld") == 3306);
      check("config-size-suffix", cfg.get_int("max_allowed_packet", "mysqld") == 16 * 1024 * 1024);
      check("config-string", cfg.get_value("user", "client") == "root");
      check("config-flag", cfg.get_bool("skip-networking", "mysqld") == false && cfg.has_key("skip-networking", "mysqld"));
      cfg.set_value("port", "3307", "mysqld");
      check("config-set", cfg.save(path) && base::ConfigurationFile(path, base::AutoCreateNothing).get_int("port", "mysqld") == 3307);
    } else {
      check("config-load", false);
    }
    unlink(path);
  }
  // MySQL parser
  check("parser-valid-select", syntaxErrors("SELECT a, b FROM t WHERE x = 1 ORDER BY a", 80029) == 0);
  check("parser-invalid", syntaxErrors("SELECT FROM WHERE ,,", 80029) > 0);
  check("parser-version-gate", syntaxErrors("SELECT 1 FROM t WINDOW w AS ()", 80029) == 0 &&
                                 syntaxErrors("SELECT 1 FROM t WINDOW w AS ()", 50700) > 0);
  check("parser-stmt-select", firstRule("SELECT 1") == "select");
  check("parser-stmt-create", firstRule("CREATE TABLE t (id INT PRIMARY KEY)") == "create");
  check("parser-stmt-insert", firstRule("INSERT INTO t VALUES (1)") == "insert");
  return 0;
}
