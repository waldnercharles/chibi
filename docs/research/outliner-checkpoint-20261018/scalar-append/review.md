# Scalar append candidate

Engine worktree: `/workspace/chibi-outliner-m3`. Exact candidate hashes are in
`final.sha256`: 17 production files plus `test/language/scalar_append.c`.
No language syntax, semantic/package files, public context/storage layout,
canonical/prepared opcode format, scene limit or execution budget changed.

The optimization derives a proof for plain unnamed I32 local self-appends.
Straight-line add/snapshot/same-local assignment pairs cannot have another CFG
entry. A bounded monotone provenance proof protects all private headers and
backing pointers, rejects raw writes and escapes, and permits checked scalar
reads. Canonical snapshots outside that one assignment retain their existing
copy semantics. Synchronous authored arguments cross the existing copied-or-
independently-proved-readonly boundary. Nonprivate pure native calls may be
variadic; no unknown native consumer receives a private header.

Each local accumulator shares a private geometrically grown tail across its
append sites. Exact latest-header/count/backing/capacity/arena matching permits
writing only the new suffix. Every append creates a new immutable logical
header; older or foreign prefixes allocate independent backing. Extra managed
roots are included in both trace and visit paths, and host storage accounts for
the private cache. Fresh invocation, reset/rebind, inspection/deoptimization and
arena change clear the cache. Normal synchronous caller re-entry preserves it.
The existing nominal-record row optimization is unchanged.

Independent production reviews: audit_auto_merges and outliner_contract.
Contract reviewer accepted final row_builder hash `0fff8d7d...`, array hash
`293bf522...`, fixture hash `065e51cb...`, and admitted one standalone first
measurement under one shared hard 60-second deadline. No execution has been
performed by the implementation or fixture agents.

Finite fixture workload: six pico_unit cases; eight source/module preparations;
24 top-level calls; 402 synchronous readonly helper calls; 818 authored array_add
executions; twelve static reproofs of copied instructions, never executed; seven
direct private-tail appends; one checked-overflow rejection; two fixed garbage
collections. Maximum authored loop count is 128, each authored frame has a
20,000-check budget. ONE executes one format_text and one text_byte_length per
call (12 calls total), preserving the same module and invocation counts.
TWO additionally executes ten length calls and eight get calls across both
backends. One direct reader case makes ten native calls on one string element:
two successful gets, four invalid gets, two nonempty lengths and two empty
lengths. No additional authored preparation or invocation is introduced.

Oracles require exact derived plans and runtime flags, exact array values,
linear payload-copy bounds, actual compiled entries without bytecode fallback,
independent escaped snapshots after host mutation, same-address arena reuse,
inspection and binding fallback, rejection of branch-into-pair/raw-pointer-write
IR, retained old-prefix forks, extra-root trace/visit survival, and overflow
output/cache preservation. Teardown frees tail_data and tail_function vectors
before the arena even if a REQUIRE fails.

Remaining validation: parent build and admitted standalone run, unchanged
nominal row-builder regression, then the existing fixed Studio scale workload
with actual optimization/copy-cost assertions. Actual pause/resume and
cross-arena resume have static review but no dedicated new execution case.
Prepared decoding rederives the plan by the existing finalize path; no new
prepared-image case was added. The existing 10,000-check source budget remains
a separate blocker for the 4,096-actor synchronous projection. This change
makes no claim of general growable-array language semantics or real-time bounds.

## First measurement correction

The parent's admitted first scalar run on the original hashes failed four of
five cases (68 assertions, 0.159s): derived plan counts were zero, and ALIASES
used untyped numeric literals where generic array_add requires an int. No
assertions were weakened. The unchanged nominal row-builder, row-array and
borrow routes passed independently under ASan/UBSan (8/9820, 13/18008 and
12/661 cases/assertions respectively; each below one second).

Source inspection established that lower.c emits SOURCE_WATCH after an append
expression and SOURCE_POINT before its assignment retain. The initial pair
scanner rejected these canonical instructions even though runtime_code removes
them. The correction skips only explicit observer/POINT/SOURCE_POINT/SOURCE_WATCH
instructions whose runtime_keeps value is false. Branch targets into their
positions remain forbidden, and inspection still selects canonical execution.
ALIASES now declares typed int locals for the unchanged 101 and 202 values.
The exact plan/copy/backend/value assertions and finite workload are unchanged.

On a failed plan-count assertion only, derived() now reports counts plus at most
256 combined host/instruction records; it creates or executes no extra input.
The two corrected files are frozen at row_builder `5e6331a6...` and fixture
`2cbc69d6...`. Formatting and diff-check passed. Both outliner_contract and
audit_auto_merges independently accepted these exact revisions and admitted
one unchanged standalone rerun under the shared hard 60-second ceiling. The
parent's rerun remains pending; the implementation agent has performed no
builds or test executions.

## Reader and reuse correction

The second parent run activated append plans and passed alias/collection cases.
The remaining fixture mistakes were a helper using native length (deliberately
opaque to the separate readonly proof) and an incorrect zero-nominal-copy oracle
for Format Text. The helper now uses guarded raw indexing and still produces
exactly 2*i+1 while requiring readonly_collections. ONE now requires exactly one
decimal text plus NUL copy and the 13-byte "const char *" name copy performed by
its native result snapshot. Packed payload limits remain unchanged. The third
run passed four of five cases (1,494 assertions, 0.169s) but exposed canonical
fallback after same-address arena reset: packed bytes 17,152 exceeded 4,352.

The runtime guard had inspected every data-bank shape, including unrelated
native string results with owned names that a subsequent fresh entry overwrites.
It now checks only banks declared as plain unnamed I32 arrays, a conservative
superset of all private-prefix banks admitted by the whole-body proof. The
checks inspect type/container/null name/null record without dereferencing stale
arena metadata. Callback identity checks and fresh-entry cache invalidation are
unchanged.

Shipped Project also reads its private matches using get/length. Their existing
kernels and exact owned-HOST versus snapshot-CALL behavior moved to types-owned
callbacks. Runtime exports keep their names, signatures, error strings and copy
semantics. The scalar proof recognizes exact canonical reader callbacks only
for plain I32 arrays and plain I32 index/results with nonzero linked capability
ID/ABI. Prepared linking validates ABI; this proof does not compare capability
IDs to hardcoded Get/Length IDs. Unknown callbacks remain rejected, and no
change was made to the independent readonly-helper proof.

New fixture checks retain exactly three append plans while observing length/get
results. Copied-IR proofs require the original reader to pass and reject both an
equivalent but noncanonical callback and an incompatible index shape. Direct
reader checks verify owning copies, the additional public snapshot, exact copy
counts, independent retained strings, invalid-index errors and unchanged output,
and empty/nonempty length. The existing same-address reuse oracle is unchanged.
The final hashes are recorded in final.sha256 (row 9bd60ffc..., fixture
f2f35c74...). Independent review and the parent's admitted execution are pending;
no build or test was executed by the implementation agent.

## Native reader default-name correction

The fourth parent measurement passed five of six cases (770 assertions,
0.143s), including same-address arena reuse and the direct reader contract.
TWO stopped at the unchanged three-plan assertion. Native get declares its
Index as builtin int; capabilities/definition.c preserves that name in the
declared default and lower.argument_defaults copies it. Generic arrays and
results instead use lower.initial and remain unnamed. The reader predicate had
rejected the harmless builtin index name.

The corrected predicate permits exactly "int" only on canonical get's I32
scalar index. All array/result names, records, non-I32 shapes and unknown
callbacks remain rejected. The fixture asserts those actual physical names and
adds a custom IndexAlias rejection on a copied descriptor. This adds two static
reproofs (14 total); all authored, module and runtime counts are unchanged.
The frozen files are row_builder e5f379006f46b3df4d15f97e4aa5396b73dcc65926878b803a4fcdfb605eb520
and scalar_append 13e3a724b3cf310255d9a685368bd542e8c96a363e8b2536ac6d2d441f341e0a.
Both independent reviewers were activated on these exact files. Formatting and
diff-check passed; no build or test was executed by the implementation agent.
Project source inspection found private matches used only by canonical
get/length and parents passed to copied-or-proven-readonly Depth. Actual Project
plan activation and the existing packed-copy bound still require measurement.

## Accepted native candidate and source freeze

Both independent reviewers accepted exact row_builder e5f37900 / fixture
13e3a724 and admitted the unchanged runtime workload with 14 static reproofs.
The parent reports native21 build passed in 2.2s, scalar run5 passed all six
cases with 1717 assertions in 0.189s, and census2 passed one case with 393
assertions in 0.484s. The census reports Project appends=2, IntRange appends=1,
and unchanged K=249. These executions were performed by the parent, not this
reviewer. Source is frozen for the instrumented SDK9 package. The installed-SDK
4096-actor/32-row scale run and final backend/ABI validation remain pending.
