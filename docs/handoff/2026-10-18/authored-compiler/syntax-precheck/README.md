> Historical technical record, with coordination-only wording removed. Read the top-level README.md for current dependency heads, publication status, omitted reference copies and the parser admission update. Historical source-only status and earlier hashes describe preparation, not a passing build or runtime result.

# Complete bounded-compiler closure syntax precheck

Status: prepared source-only proposal. No diagnostic build, parse execution,
authored/VM/JIT execution, publication or admission has occurred. Independent
review must admit this exact packet before either script is invoked.

## Scope and exact inputs

One C/pico dispatcher has exactly eighteen fixed cases, one complete source
parse per case. The corpus is the complete sixteen-package compiler closure:
fourteen exact PR446 reader/snapshot dependency sources, the separately owned
corrected PR447 Lower.chibi, and the three corrected bounded_compiler files.
It totals 179,267 UTF-8 bytes, with a largest file of 30,602 bytes. The corrected
Lower source is SHA256
1778dd60224e4edb46e87d4563e20e0625b170da933976c317f00340eecfd4fd.
The new driver changes are only the two required union separators after the
Rejected variants in Compile.Result and Expression.ScalarExpression. No feature
or workload grows in the actual compiler candidate.

corpus-manifest.json maps numeric source IDs to complete file identities and
provenance. Every decimal-byte C array in sources.h has been independently
decoded against that complete file, including exactly one trailing NUL. None is
a snippet or patched at runtime. The original uncorrected Lower is preserved as
an unused reference for provenance; only corrected Lower is in the corpus.

The parser, scanner, parser header, pico header, Tree-sitter runtime and
production supervisor are copied byte-for-byte from the reviewed
separate scalar syntax diagnostic packet. The source-inputs.json file records their exact
original private repository blob IDs and official Tree-sitter revision
da6fe9beb4f7f67beb75914ca8e0d48ae48d6406. All 53 upstream input file lengths,
SHA256 values and Git blob IDs were rechecked. The generated parser is SHA256
40e28627d8d8401eb2452326bcdec7ee2730743f3cbae5a8579bf3dabda77683.
The production supervisor is SHA256
d6e9085dff6a6e0771f3bb8ab8afcfc4be64bffc7cbaaf0e51c65740e50055dd.
This packet does not edit that supervisor or register a new product selector.

## Finite workload and ownership

- Exactly 18 fixed cases and 18 complete parse attempts, all run even if an
  earlier source case reports a syntax error. No source/case subset argument.
- Each source must be nonempty and strictly below the existing 32,768-byte
  guard before parser creation. No discovery, source file reading or network
  occurs inside the C diagnostic.
- One fresh parser/tree at a time; each case releases parser, tree and cursor
  before its final validity assertion. No retained previous parse tree.
- The unchanged 50,000-node guard is checked after parsing. It bounds the
  cursor walk, not parser allocations or parsing work; no peak-memory result
  is claimed. At most 900,000 visible nodes can be examined across all cases.
- The reviewed nonrecursive cursor/error walk is byte-identical. ERROR trees
  are pruned; the final root error check still rejects any residual error.
- At most 128 numeric location lines per case, 2,304 across the corpus, plus
  eighteen numeric summaries and fixed pico output. No private source text,
  identifiers or raw parser trees are printed.
- One supervised C workload child. The production Python launcher/supervisor
  and its unchanged cleanup helpers are overhead, not extra test workloads.
  No authored source is executed; no VM, JIT, engine, GPU or Wasm is involved.
- The entire 18-case invocation has one unchanged production 60-second
  deadline. Inherited deadlines can only shorten it. No per-case timeout,
  retries, splitting, recovery run or subset reconstruction is authorized.

The separate build uses the installed GCC14, four C translation units, -O0,
-g0, section splitting and --gc-sections, without sanitizers. Build timing is
separate from run timing. The C logic preceding the case declarations is
byte-identical to the reviewed scalar diagnostic. Only the explicit corpus,
case list and selector differ. The scripts additionally verify the installed
compiler/interpreter/shell/hash tool identities in toolchain.sha256; the Python
launcher uses its resolved absolute interpreter path. System toolchain headers,
libraries and compiler subordinate programs are inherited installed environment
dependencies, not privately vendored or claimed as reproducibly pinned inputs.

frozen.sha256 pins every source/header/corpus/build/run input in this packet;
both scripts verify it before acting. The run script also verifies the built
binary's independently recorded hash. Source hashing/header decoding, static
reserved-name checks, shell syntax checks and patch application checks completed;
none executes this diagnostic or validates Chibi semantics.

## Proposed commands, only after independent admission

From the reconstructed diagnostic packet's parent directory:

1. bash syntax-precheck/inspection/build.sh
2. If and only if that build succeeds, once:
   bash syntax-precheck/inspection/run.sh

Retain full exit codes, fixed/numeric logs, separate build duration and supervised
run duration. A failed build returns for review before runtime. Syntax errors
return for source diagnosis; any changed corpus/source requires another frozen
packet and independent admission before a complete invocation. Remove the
binary after the investigation while retaining source and evidence.

A syntax pass establishes only parsing of these exact eighteen complete files.
All real dependency semantic/runtime/binding gates, the compiler's substantive
whole-package source-to-artifact case, applicable builds, exact combined review
and admission still remain required. No self-host, stage-one or stage-two claim.
