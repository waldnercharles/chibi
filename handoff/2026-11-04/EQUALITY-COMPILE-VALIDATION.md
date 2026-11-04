# Strict equality-unit compile regression

## Change and result

The compiler-host build stopped at GCC 14's `-Werror=dangling-pointer` diagnostic on the address of the local equality graph being stored in thread-local state. The source audit found no supported returning path that escapes the matching cleanup. That supports a control-flow diagnostic limitation; it does not establish GCC's internal cause or a prior runtime defect.

The minimal `pin_value_equal` cleanup separates nested comparisons from the root that owns the graph. Nested calls retain the same pair lookup and comparison. The root inserts its first pair when needed, compares, then unconditionally clears TLS and frees the table. It adds no allocation, recursion, helper, API, layout or warning exception. The first insertion into an empty graph cannot be a repeated pair.

A fresh paired compilation of both complete real `src/types/array.c` files passed its expected outcomes under the captured GNU23/O3/`-Werror` command, changing only isolated source/include/output paths:

- Original source: exit 1 after 1.168 seconds, the sole original local-to-TLS dangling-pointer error, no object.
- Candidate: exit 0 after 1.321 seconds, empty diagnostics, a 77,616-byte ELF64 x86-64 relocatable object.
- Whole phase: 5.349 seconds inside its original 60-second bound, including admission and full pre/post input checks. Both commands retained 15-second limits. The unchanged owner reached ECHILD with no signals, errors or surviving children.

These are correctness-gate durations, not a performance comparison. No object was linked or executed.

## Review and regression route

Independent review covered the complete changed source and its equality callers, the exact paired command/controller, 25 staged headers and inherited GCC/runtime/Python/search closure. Terminal review independently reread the exact diagnostic, object, output identities, cleanup and original deadlines. No blocking finding remained for this compiler-only checkpoint. Later handoff-only commits do not change the reviewed source attribution.

The real strict compilation is the regression: it fails on the original complete unit and succeeds with the cleanup. The existing graph already builds this unit in `chibi_types` with the strict flags. Its smallest named replay target is `chibi_types` under an explicit GCC14 Release profile. Normal Clang CI is not evidence for this GCC-specific result. No toy warning example or duplicate equality implementation was added.

## Retained evidence and limits

`equality-compile-results.json` records exact source/object/review digests and the verified recovery archive. Every archive member was reread and the original evidence remained unchanged. Installed tool/runtime facts and the original source in the separately retained host-failure archive remain external prerequisites.

Focused scalar/array behavior assurance is separately prepared and unarmed at this checkpoint. Linking, a fresh complete host build, graph-c runtime, actual Windows behavior and exact-head integration remain outstanding. No broad types selector or excluded union leak investigation was run. PR #1331 remains explicitly not ready; no merge, release or remote publication is claimed.

Subsequent focused runtime assurance is recorded in EQUALITY-RUNTIME-VALIDATION.md. It preserves this compiler-only result and all original failures.
