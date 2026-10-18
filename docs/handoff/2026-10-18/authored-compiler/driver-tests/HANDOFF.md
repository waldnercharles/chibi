> Historical technical record, with coordination-only wording removed. Read the top-level README.md for current dependency heads, publication status, omitted reference copies and the parser admission update. Historical source-only status and earlier hashes describe preparation, not a passing build or runtime result.

# Bounded authored compiler regression packet

Status: local source-only, unexecuted, unadmitted. No configuration, compilation,
product test, remote mutation or publication was performed. This packet is
frozen for combined independent review, not a passing runtime result.

## Deliverables and integration

- `bounded-compiler-tests.patch` adds four test/build files and minimal
  registration hunks. SHA256:
  `73a30da998b1991b75b2628bc84f15b0427aae0d38649b5d2fa48a2d29297f7e`.
- `candidate/test/language/bounded_compiler_chibi.c`: one C/pico_unit case.
- `candidate/test/language/bounded_compiler_chibi.h`: dispatcher declaration.
- `candidate/test/language/bounded_compiler_chibi_source.h.in`: embedded,
  explicit 18-file compiler closure; no user source enters stage zero.
- `candidate/test/language/bounded_compiler_chibi.cmake`: private language-test
  sources and generated-header registration. It does not modify the separate
  runtime-only compiler-host target or its catalog.
- `registration-only.patch`: additive dispatcher, CMake and standalone policy
  command registration. No aggregate membership, deadline or admission change.
- `source-manifest.json`: exact source hashes, sizes, dependencies and workload.

Registration hunks were prepared against PR447 exact
`bdbb896c48aadd271ee63a6562447ccb95d7ffa1`. They are NOT full replacements for
the current files. Full historical CMake/main/policy projections were deliberately
removed from candidate. Integrate only after combining genuine PR445
`1338151c084c1a633f881153084bc83773df27fa`, PR446
`70ff5bf54a1125a110be6a26a7e226a4b5d542ba`, PR447 and the current target's
changes. Preserve PR446's analysis_state source embedding and snapshot-reader
selector, PR447's scalar selector, and current PR444 semantic additions. None of
those components has been validated by this task. No held dependency is cleared.

The native bridge's four files are a separate required packet under
`../native-bridge/candidate`. The test clones its single definition/binding to
count callbacks and passes its immutable service using trusted Plan.Ready export
metadata. It does not add counters or test hooks to product runtime APIs.

## Exact proposed workload

- One case; one 18-file, 16-package compiler closure prepared per backend.
- Current measured closure: 179,213 bytes. Each file remains below 32 KiB and
  this new fixture independently caps the total below 256 KiB. The old
  snapshot-reader fixture and its stricter caps are unchanged.
- 36 stage-zero preparation parses; 54 top-level scalar constant initializers
  per backend, hence 108 expected initializer VM entries at the existing
  bootstrap budget. The suite records actual bootstrap run counters; these
  expected entries are source-counted, not observed execution evidence.
- 39 fixed source requests per compiler backend: 14 accepted, 25 rejected.
  Total 78 Compile calls at supplied context budget 3,000,000.
- 86 offered source-file instances, including deliberate rejected inputs with
  four files, 1,025 bytes, a 241-byte path, and mismatched/empty arrays.
- 70 private raw-parser calls expected across both compiler backends. The raw
  parser wrapper rejects any text over 1,024 bytes. Total source parses are 106
  including the 36 bootstrap preparation parses. No C source semantic oracle
  parses or analyzes any user-source specimen.
- Exactly 28 successful payload callbacks; every source/ABI rejection requires
  a zero callback delta. Seven direct native bridge rejection checks use one
  detached actual authored plan: wrong tag, mismatched result type, zero module,
  zero function, empty entry, nonunion plan, and a variable-bearing plan.
- Exactly 56 artifact loads and target invocations: all 14 successful cases
  from each compiler backend run once on both BYTECODE and SLJIT, each at budget
  128. Backend counters require exactly one selected target entry and zero
  fallback/other backend entries.

All loops are source-bounded. Accepted requests offer at most three files,
1,024 bytes each, 24 lowered expression rows and 64 traversal entries. Total
offered source text per corpus is checked below 16 KiB. The one expression
over-limit specimen contains 25 rows. Artifact bytes are capped at 65,832;
28 copied artifacts remain live until checks complete, at most 1,843,296 bytes.
Only one call's input/producer/detached arenas and one target host/load-copy/JSON
result are live at a time. Two compiled compiler closures are retained during
source requests. The suite also temporarily retains one detached lowering plan
for the seven bridge failure checks. It reports attempted counts on failure.

Context instruction budgets reset/inherit in child activations, so they do not
constitute an aggregate work bound. The complete fixture, including bootstrap,
JIT, all source requests, serialization, loads, invocations and cleanup, needs
one unchanged production shared 60-second supervisor deadline after independent
exact-source/resource admission. Configuration/build timing stays separate. A
timeout is a blocker, not permission to split or repeat the workload.

## Coverage and lifetime contracts

Accepted specimens cover repeat determinism, changed source producing a
different runnable value, Boolean operators/equality, signed I32 minimum,
wrapping addition, arithmetic, bitwise operations, all comparisons, three-file
header/comment-only siblings, the exact source cap and 24 expression rows.
Rejected specimens cover syntax, mixed package headers, extra declarations and
statements, imports, wrong scalar types, empty/oversize/mismatched arrays,
parameters, non-pure or multiple functions, unsupported literals/calls/casts,
untyped binary arithmetic, library wrappers, expression overflow, invalid roots,
paths and target ABI. Diagnostics must be nonempty and identical across compiler
backends, including complete source location fields.

Every call's source/path argument arena, activation and producer storage retire
before its detached result is examined. All compiler owners and detached result
arenas retire before artifact loading. Each host receives its own load-buffer
copy, which is freed before invocation. The host itself retires before its
returned JSON is checked. Artifacts match byte-for-byte across compiler backends
and unchanged repeats; IDs match the existing native identity hash oracle.

The consumers are fresh compiler_host API instances in this same C test process.
This is not isolated runtime-only-process or linked-boundary evidence. Existing
runtime-only product boundary audits remain separate required gates.

## Checks performed and pending

Performed: read actual AGENTS.md and docs/testing.md; inspect actual fixture,
snapshot/reader/lowering, host and bridge interfaces; inspect all three authored
driver files against the corpus; format the new C/header with clang-format
22.1.8; pass its dry-run; pass `git apply --check --whitespace=error` against
the exact PR447 reference files. Initial patch-format issues were corrected
before the final checks. No test executable or compiler was invoked.

Pending: mechanical combined integration preserving all current/dependency
changes, independent contract/resource review, stage-zero parsing of every
dependency and authored file, exact integrated build, first-run admission,
all runtime assertions above, applicable admitted groups/product boundary
checks, approvals and explicit holds. In particular, source inspection identified
PR447 Frame.branch as a source grammar gate because branch is reserved; this
packet does not edit or silently repair that held dependency. Refresh the source
manifest whenever any compiler/dependency/test source changes.
