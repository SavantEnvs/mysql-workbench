// Fuzzes base::ConfigurationFile, the my.cnf/INI parser behind MySQL Workbench's server-config editing:
// the input is written to a harness-owned mkstemp scratch file, loaded, queried, mutated and saved.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "base/config_file.h"
#include "base/log.h"

namespace {
// Upstream logging dereferences a null impl unless some Logger instance exists.
base::Logger gLogger(false, "");

std::string tmpDir() {
  const char *t = getenv("TMPDIR");
  return (t && *t) ? t : "/tmp";
}

bool writeAll(int fd, const uint8_t *p, size_t n) {
  while (n > 0) {
    ssize_t w = write(fd, p, n);
    if (w <= 0)
      return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1)
    return -1;
  uint8_t ctl = data[0];
  data++;
  size--;

  std::string path = tmpDir() + "/fuzz_cfg_XXXXXX";
  int fd = mkstemp(&path[0]);
  if (fd < 0)
    return -1;
  bool ok = writeAll(fd, data, size);
  close(fd);
  if (!ok) {
    unlink(path.c_str());
    return -1;
  }

  {
    base::ConfigFileFlags flags = base::AutoCreateNothing;
    if (ctl & 1)
      flags = flags | base::AutoCreateSections;
    if (ctl & 2)
      flags = flags | base::AutoCreateKeys;
    base::ConfigurationFile cfg(flags);
    if (cfg.load(path)) {
      volatile int sink = cfg.section_count() + cfg.key_count();
      (void)sink;
      cfg.get_value("port", "mysqld");
      cfg.get_int("max_connections", "mysqld");
      cfg.get_bool("skip-networking", "mysqld");
      cfg.get_float("x", "mysqld");
      cfg.get_includes("mysqld");
      cfg.set_value("port", "3307", "mysqld");
      cfg.set_bool("skip-networking", ctl & 4, "mysqld");
      cfg.create_key("fuzz", "1", "pre", "post", "mysqld");
      if (ctl & 8)
        cfg.delete_section("mysqld");
      cfg.save(path);
    }
  }
  unlink(path.c_str());
  return 0;
}
