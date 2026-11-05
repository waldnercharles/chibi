# Checkpoint summary

This is a newly reconstructed source/design proposal, not recovered historical
source and not a feature implementation. This is a documentation/source checkpoint only.
Compiler execution, implementation and real-tool acceptance remain pending.

- Exact caller: Main.chibi, 7982 bytes / 179 physical lines / 174 nonblank lines.
  SHA-256: 0e44190699438f78b223c71a380a903f71526941f0a5dc700c655b62be341f16.
- Two authored functions, one foreign import, two while loops, 44 peak visible
  Census bindings; ReadU32 has six. Projected blocks: Census 121, ReadU32 1.
- Feature demand: two BorrowedBytes annotations, one byte_length call, five
  byte_read_checked sites, four subtraction operators, seven ReadU32 calls,
  one sixteen-i64 report call. The inventory separates reads, writes and bindings.
- Full original domain preserved: 4096 packages / 16384 files / 16 MiB per file /
  256 MiB originals, plus an explicit one-record / 256-byte generated-target
  allowance justified by the inspected native-driver workload. Frame cap:
  268632348 bytes. Historical closure counts are explicitly not fresh evidence.
- Proposed semantics cover framing, every error, ASCII physical/nonblank lines,
  overflow, nonescape, real foreign-allocation validity, ordinary retained
  nominal resolution, canonical intrinsic descriptors, rare lowered-pair rows,
  binding-ID write-sets, cycle-aware dominance and a derived bounded 128-block proposal.
- Acceptance plan requires retained parser/producer counts, semantic/bridge
  rejection tests, independent byte oracle, sixteen-position JIT/AOT ABI tests,
  actual current closure matching and all outstanding native gates. Current
  compile_both cost stays dual-output; future output-mode selection must precede
  backend work.

Checks performed in this checkpoint are source-only: verified base HEAD/tree,
read relevant contracts, reproduced lexical inventory, inspected the generator
literal's 235-byte result for the documented numeric domains, checked limit and
CFG arithmetic, and author-reviewed proposed files. Parser/producer execution,
native compilation, installs/downloads, oracle execution against Chibi, fresh
closure gathering, independent review, publication and accepted-native claims
were not performed.

Files:
- README.md: design, format, limits, representation and adoption/proof gates.
- Main.chibi: exact proposed caller.
- host-contract.h: proposed ABI declarations, not a host implementation.
- inventory.py and inventory.json: source-only reproducible lexical inventory.
- REVIEW.md: author review and open measurement/acceptance findings.
- SUMMARY.md: this checkpoint summary.
