# Complete the remaining strict JIT compiles

The suspension emitter now computes one saved-value byte offset in SLJIT's
signed word type and uses it for the four floating/integer save/restore
operands. The original unsigned multiplication happens before conversion.
There is no added traversal, allocation or runtime dispatch.

The two existing runner calls convert their callable pointer through uintptr_t
before the function-pointer conversion. This preserves SLJIT's callable pointer,
including a function-descriptor pointer where that ABI uses one, without
extracting a code address, dereferencing a descriptor or clearing tags. It adds
no helper or changes to selection, counters, fallback, scope or ownership.
This remains implementation-defined supported-ABI behavior, not a claim of
universal ISO C or cross-platform runtime portability.

## Exact compile evidence

A fresh six-command diagnostic used the unchanged GCC 14 GNU23/O3/NDEBUG
warning-as-error recipe. The original suspension unit reproduced its four
signedness errors, and the original runner reproduced its two pedantic cast
errors. Neither original produced an object. Both corrected complete units
compiled with empty logs, preserving their 117 and 113 ordered dependencies.

The two remaining units, binary_cache and compiler_main, also compiled with
empty logs and validated dependency lists. The canonical errno-header spelling
missing from the previous diagnostic was prospectively bound to the same
identity and content as three already fenced aliases. This does not amend or
pass the previous failed attempt.

All six commands completed in 4.147271 seconds inside the unchanged 180-second
total, 15-second command and five-second cleanup limits. Actual outer exit was
zero, with clean ECHILD and no errors, signals or surviving owned children.
The four resulting ELF objects were inspected as data, including section,
symbol and relocation structure. Source, tools, search roots, all 713 dependency
occurrences, outputs and timestamps were independently checked. Exact identities
and results are in [jit-portability-results.json](jit-portability-results.json).

These are correctness observations, not benchmark measurements. The arithmetic
models and pointer-token tables are pure checks, not target-ABI executions.

## Remaining gates

This is a narrow source/compile checkpoint. No archive, host link, generated
program or runtime parity test ran in this diagnostic. A fresh registered host
[construction now passes separately](REGISTERED-HOST-CONSTRUCTION.md); earlier
partial builds and these objects were not combined into that host pass. Actual bytecode/SLJIT/native parity, full graph-c, broader C
integration, sanitizer and Windows coverage, exact-head CI and PR integration
remain separate. PR #1331 is still not ready to merge.
