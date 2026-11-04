# Portable native default initializers

The shared `NATIVE_PORT` and `NATIVE_CSTRING_PORT` descriptor macros now retain
the brace initializer from each authored `pin_value` compound literal instead
of placing a compound-literal expression in a static initializer. This removes
the strict GCC diagnostic without weakening GNU23, O3, `DNDEBUG`, warnings or
`Werror`. `_Generic` preserves the compatible-type requirement without evaluating
the controlling expression. Descriptor fields, layout and catalogue normalization
are unchanged; static rows add no runtime work.

The public source contract is deliberately narrow: `VALUE` must have the form
`((compatible_pin_value_type){ ... })`. Compatible typedefs work. Arbitrary named
or conditional expressions and extra wrapping parentheses are not generally
supported. The independently inspected in-tree rows use the supported form:
30 literal spellings across ten live definition headers, plus 93 across sixteen
archived runtime headers and one archived studio spelling. Archived private
static-expression consumers are not changed or certified by this correction.

## Focused checks

The five separately bounded strict GCC 14 compilation checks passed:

- Original text unit: reproduced exactly 19 nonconstant-initializer errors and
  produced no object.
- Corrected real text and arrays units: compiled with empty diagnostics.
- Descriptor fixture: compiled with the same strict policy. Twelve synthetic
  rows feed all three static consumers, including the forwarding macro and a
  compatible typedef. Three live text rows also reach the unchanged archived
  graph expression consumer.
- Wrong-type fixture: an unrelated lookalike structure was rejected by exactly
  three `_Generic` errors, one for each shared consumer, with no object.

All phase receipts retained their original deadlines and clean child cleanup.
Independent verification checked exact source, tools, headers, Python/search
inputs, command vectors, artifacts and receipt hashes. Timings in the companion
JSON are bounded correctness observations, not full-build performance results.

The compile-proof archive has 702 members and 1,400,346 compressed bytes, SHA-256
`a96857d070141f36d588125f5a130898e62ee943eb7c63c534006d12705dd2a6`.
Its member bytes, modes and original identities were read back. The archive is
retained locally, not published as a release.

## Standalone runtime proof

The exact object was independently inspected, linked, inspected as an actual
ELF and then executed in separate bounded phases. Its optimized fixture passed
36 synthetic table-row comparisons, three live text rows through the archived
graph expression consumer and the automatic single-evaluation check. It returned
0 with empty output in 0.001715 seconds. O3 may fold checks; this is optimized
program success, not independent instruction coverage or a performance claim.

The 28,504-byte executable is a libc-only x86-64 PIE with the pinned interpreter,
nonexecuting stack, no writable-executable load segment and no dynamic search
override. All four phases had clean child cleanup and unchanged inputs. The
exact-header runtime prerequisite was independently accepted. Source, object,
executable, report and receipt hashes are in
[native-default-initializer-results.json](native-default-initializer-results.json).

The runtime archive contains 143 members and 1,360,545 compressed bytes, SHA-256
`6db41197d8ce6c0a69d9ad6413c37890551b1952dc02435d6587972a92be4a45`.
It retains the unexecuted first launcher proposal and its corrected successor;
no failed or unrun proposal is counted as acceptance. Full archive member and
original-identity readback passed. It is retained locally, not a release.

## Permanent regression integration

`test/graph/CTests.chibi` now calls `NativeDefaultsArePortable` from the existing
`LinkResponses` path, immediately after the array-cookie regression. The 28-line
addition reuses the existing C fixture and process capture API; no new public
test selector or production dependency was added.

A copied source tree with a temporary zero-argument entry compiled the exact
permanent function and exercised it independently. The authored caller passed:
strict GNU23/O3/NDEBUG warning-as-error build, silent fixture execution, and
wrong-type rejection with status 1, an `_Generic` diagnostic and no object.
The temporary entry is not part of the source checkpoint. The whole graph-c
selector has not run.

The compiled caller returned 0 with empty streams in 0.377461 seconds; its entire
owned phase took 4.265770 seconds within the original 80-second total bound and
five-second cleanup reserve. Independent verification accepted the exact source,
compiler, tools, headers, produced fixture, unchanged inputs and clean child
cleanup. The fixture bytes equal the earlier accepted standalone executable.
These three nested outcomes are established by the authored assertions, not
separate captured child receipts: the capture API removes the nested logs.
Timings are bounded correctness observations, not performance measurements.

Exact source, artifact and proof hashes are in
[native-default-regression-results.json](native-default-regression-results.json).

## Remaining acceptance

Full graph-c execution, complete compiler-host construction, actual Windows
execution and exact-head integration remain separate gates. The focused fixture
performs no runtime allocation, retention, destruction or union ownership
investigation. Earlier failed host packets remain failures. After the published
`jit.c` correction, the next strict host build reached 46 successful objects and
stopped on two signedness diagnostics in `jit_memory.c` and `jit_registers.c`.
It produced no archives or host; those corrections are separate work. PR #1331
remains explicitly not ready to merge.
