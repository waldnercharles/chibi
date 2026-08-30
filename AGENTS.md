# c-blueprints agent rules

Spartan C. Less code is a feature: delete dead code and unused abstraction.

- Flatten: guard clauses and early returns; no `else` after `return`/`break`.
- Small single-purpose functions; minimal public surface and parameter count.
- Fail fast: `assert` invalid state, no defensive `NULL` checks, abort on OOM via
  `xmalloc`/`xrealloc`. Root-cause pointer bugs; never mask them.
- Types: `int` for domain values and indices, `size_t` for sizes and size-taking
  APIs, unsigned only for bit manipulation; `bool` not `int`.
- No `(void)` on parameterless functions; omit the unused parameter name instead
  of `(void)param`.
- Use the shared vector (`apush`/`acount`/`afree`/`asetlen` from `core/array.h`),
  not a hand-rolled grow-by-doubling.
- `"local.h"` for project headers, `<sys.h>` for standard/third-party.
- Keep ownership and lifetime explicit; `static` for file-private.
- Prefer direct calls or `switch` over function-pointer tables and indirection.
- Follow existing naming and clang-format; short names only in tight local scope.
- Name magic numbers and invariants (`#define`/`enum`/`static const`).
- New nodes must be added to the appropriate registry pack and every UI context
  allowlist/filter; verify they are discoverable by name in the spawn menu.
- UI/UX changes: run `build/app --headless --screenshot`, inspect `/tmp/c-blueprints-screenshot.png`, and report the captured dimensions. Use `--frames N` when a longer settle period is needed.

## Agent skills

### Issue tracker

Issues live in GitHub Issues for `waldnercharles/c-blueprints`; use the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the default labels: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, and `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

This repository uses a single-context domain-doc layout rooted at `CONTEXT.md`, with ADRs at `docs/adr/` when needed. See `docs/agents/domain.md`.
