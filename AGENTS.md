## C Guidelines

- Prefer `make build` when building, and `make test` to test.
- Keep code small: remove dead code/abstractions; use small functions, narrow APIs, few parameters, guards, and no `else` after `return` or `break`.
- Fail fast: `assert` invalid state; use `xmalloc`/`xrealloc`; fix pointer bugs at the source; avoid defensive `NULL` checks that mask defects.
- Types: `int` for domain values/indices; `size_t` for sizes; unsigned for bits; `bool` for booleans.
- Signatures: `f()` for no parameters; omit unused parameter names.
- Vectors: `core/array.h` (`apush`, `acount`, `afree`, `asetlen`).
- Includes: `"project.h"` for project code; `<system.h>` otherwise.
- Keep ownership/lifetimes explicit; mark file-private symbols `static`.
- Prefer direct calls and `switch` statements to function pointers.
- Follow existing names and clang-format; keep short names local; name constants and invariants with `#define`, `enum`, or `static const`.
