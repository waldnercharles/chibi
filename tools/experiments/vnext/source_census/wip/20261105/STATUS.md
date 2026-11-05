# Inactive source-census implementation checkpoint

This directory preserves work in progress. **The patch is not applied to the
repository's active compiler, backend or tests. It has not been generated,
compiled, tested or independently accepted.** It does not change the production
compiler, default backend, seed pins, toolchain pins or merge requirements.

The base is commit `383fe844f8fa0353a6cfa3faeca1808ee6d49d74`, tree
`73b5dc67f3b9c0e6104426017b59adcd7939c4c6`. The patch contains sixteen source,
test and documentation files. Its exact identity and reconstructed candidate
tree are in `manifest.json`; independent fresh-index application and whitespace
checks passed. The active version-3 implementation is left unchanged.

## What the candidate contains

- Canonical borrowed-buffer nominal/intrinsic catalog admission and nonescape
  restrictions, wrapping subtraction, and sparse loop-write analysis.
- Cyclic typed-IR verification, reverse-postorder backend lowering, checked byte
  loads behind real control flow, and rare pointer/length lowering rows.
- Source/packing/backend test additions and the real census caller's preparation.
- An AOT host that reads one opaque frame without counting its bytes in C, and
  an independent Python reference oracle with authored boundary tests.

The design keeps ordinary Value/Instruction rows at 8/24 scalar bytes. Pair rows
are intended to be 12 bytes. These source assertions are not runtime validation.
Model remains the single schema authority. Its literal-return descriptors avoid
the retained compiler's unsupported string-constant form.

## What remains

The candidate requests transport version 4, but generated bindings deliberately
remain version 3 and the new layout digest is not approved or recorded. The
64-block cap is retained, so the complete census caller is not admitted. The
patch is therefore not a runnable feature checkpoint.

Next are nine ordered independent engineering reviews, reviewed generation,
actual parser/IR inventories, complete cyclic/mutation and work-count tests,
128-block boundary admission, all existing scalar cases, and real guarded-memory
and sixteen-position JIT/AOT ABI tests. The reference's large-domain tests are
opt-in and have not run. Native harness integration, differential census results
and any performance claim remain pending. Do not interpret authored tests or
a clean patch application as passing tests.
