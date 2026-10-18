# Recover the parser-only proposal

The original 18-source diagnostic was admitted but not built or run. This compact
export preserves its C source and original command scripts, not an immediately
executable or independently admitted replacement packet. Do not execute the
scripts in this incomplete layout.

`inspection/source-inputs.json` identifies each parser, scanner, Tree-sitter,
pico and supervisor input by repository, revision, path, byte size, Git blob and
SHA-256. Materialize each at its recorded `local_path` from that exact revision;
do not use a moving branch. Tree-sitter is pinned to
`da6fe9beb4f7f67beb75914ca8e0d48ae48d6406`. Preserve the original production
supervisor, SHA-256
`d6e9085dff6a6e0771f3bb8ab8afcfc4be64bffc7cbaaf0e51c65740e50055dd`.

`corpus-manifest.json` identifies the complete 18-file corpus and ordering:

1. Read the fourteen dependency files from PR #446's immutable head
   `70ff5bf54a1125a110be6a26a7e226a4b5d542ba`.
2. Read corrected `tools/compiler/scalar_lowering/Lower.chibi` from PR #447 head
   `5c27f2b00b4cd4c7385b06d118217c6b8abe8ff8`. Required Git blob:
   `bbe9d91f569caf137e8326db8a86ccdf6d8e8b3c`; SHA-256:
   `1778dd60224e4edb46e87d4563e20e0625b170da933976c317f00340eecfd4fd`.
   `corrected-lower-input.json` preserves the original before/after identities.
3. Copy the three exact `tools/compiler/bounded_compiler/*.chibi` files from
   this checkpoint. Place all files at their manifest `local_path` under the
   diagnostic root and compare complete bytes and hashes.

The omitted generated `inspection/test/language/sources.h` is reconstructed
from those complete corpus files. The exact serialization is:

- Start with the line
  `// Generated from the eighteen complete pinned corpus files; one trailing NUL each.`
- In manifest order, append `static const char source_<source_id>[] = {` and a
  newline. Read the complete source bytes and append one zero byte.
- Emit chunks of 24 decimal byte values, each preceded by four spaces, separated
  by comma-space, with a trailing comma and newline on every chunk.
- Append `};` and a newline after each array. No blank lines between arrays.
- Encode with UTF-8 and LF. The result must be exactly 833,172 bytes, SHA-256
  `33860e5641221da46ec29ef9969739e5b2f72917632f2b54801d93ed39c5e479`.

That serialization was reconstructed and compared byte-for-byte during handoff
packaging without compiling or executing the diagnostic. This is source
recovery evidence only.

The original `frozen.sha256` records the reviewed preparation snapshot, including
historical documentation. Published notes were edited only for handoff clarity
and no longer reproduce those documentation hashes. The omitted references and
local installed toolchain must be restored/verified; review and freeze the
reconstructed packet before invoking anything. Do not remove hash checks or
treat the old admission as permission to execute changed inputs/toolchain.

The preserved original scripts express the proposed commands:

1. `bash syntax-precheck/inspection/build.sh`
2. Only after a successful separately reviewed build, once:
   `bash syntax-precheck/inspection/run.sh`

The single run invokes `--bounded-compiler-source-syntax` under the unchanged
shared production 60-second supervisor. It parses all 18 fixed complete files,
uses one parser/tree/cursor at a time, a pre-parse exclusive 32,768-byte source
guard, and a post-parse 50,000-node traversal guard per file. The latter does
not bound parser allocation. There is no authored, VM, JIT, engine or GPU
execution. Keep build and execution timing separate, retain exact exit codes,
and do not retry or split a failed/timed-out workload without new review.

A parser pass would establish syntax only. It would not finish dependency
runtime gates, semantic/ABI/lifetime review, compiler execution, self-hosting,
or merge eligibility.
