// mayhem/casmine_compat.h — force-included (-include) into the casmine/spec compiles only.
// testing/test-suite/casmine/helpers.h names a bare `nullptr_t`; GCC's <stddef.h> puts it in the global
// namespace, clang's does not. This makes the upstream sources build unmodified under clang.
#pragma once
#include <cstddef>
using std::nullptr_t;
