# One authored C translation unit validated

This checkpoint extends [the CLI and C preflight report](CLI-AND-C-PREFLIGHT.md). It establishes compilation of **one** genuine authored `src/types/text.c`, not the full C host or an integrated public-head build. Frozen source remains `a661626d48172e3bfcda82c9dcde3018edf53eef`, tree `1ecc136fc8ab2a059430cfb6cefcd88217cf3710`.

## New accepted evidence

- GNU23 Release preprocessing passed independent review: 976 macros, seven ordered include directories and 63 active dependencies from 70 candidates. The staged source/header set contained 26 files totaling 101,336 bytes. Outer/native times were 1.011280/0.032257 seconds, both exit zero.
- GCC `-###` print-only validation passed independent review with exact ordered operands. Outer/native times were 0.728358/0.011711 seconds, both exit zero. This stage printed commands only; it did not establish compilation or assembly.
- Real object compilation subsequently passed independent review after all 32 actual positive/negative controls were accepted. The genuine `[[maybe_unused]]` syntax, GNU23 Release flags and original warning policy, including `-Werror`, remained intact. Outer/native times were 0.995336/0.131956 seconds, both exit zero, with no captured diagnostics.

The resulting object is 7,896 bytes, ELF64 little-endian x86-64 `ET_REL`, with nine required defined global functions. SHA256: `e2f4e86674e8d845390ad2170d506a0fad84193b9f7cbd856b5bb21dfd001baa`. It remains inert data: never linked, loaded or executed. Whole-owner ECHILD and EOF checks passed; stronger per-Capture descendant proof remains unproven.

The adjacent [JSON](text-c-validation-results.json) records exact timings, source/object identities and receipt hashes. Actual-compile acceptance SHA256: `f269096b629170d3cf5b9e8822ae7b40cf089b07239af1c7bec38340d33335e2`.

## Unchanged limitations

The other 87 translation units, 16 archives and host link/runtime remain unpassed. CLI r3 remains failed and preserved; CLI r4 and SDK work remain held without reruns. PR #1331, terminal-unknown c5 bootstrap and the native 16-byte union investigation remain held. These scoped results do not establish exact-public-head integration, merge readiness or release readiness. This checkpoint contains no binaries or private proof archives.
