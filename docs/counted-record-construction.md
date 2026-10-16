# Counted record-array construction

This adds a private execution optimization for the existing counted `array_add`
loop used by a proposed authored Outliner row projection. No new syntax is
introduced. Studio's native producer already allocates its selected visible
slice once; Studio translation and the current-SDK cutover remain separate work.

The optimizer removes both repeated prefix copying inside add and the immediate
whole-array local retention for one proven loop shape. At sixteen twelve-field
rows, the measured whole authored invocation drops from 321 to 65 selected
row-payload copy equivalents. Other collection algorithms and the rest of
L04/P01 remain open. This is not a general uniqueness analysis or an all-loops
linear-construction guarantee.

## Eligibility and fallback

The initial pattern has one ordinary unboxed local initialized by the canonical
empty-array constructor, one I32 counter initialized to zero, an invariant I32
count argument, a counter-less-than-count loop test and unit increment. Each
iteration constructs one flat authored record and assigns the exact canonical
add result back to that local. The complete function proof excludes sensitive
aliases, escapes, reference writes, early exits, extra control-flow edges,
suspension and authored calls. It validates initialization dominance,
per-iteration protocol-bank writes, the increment/commit order and the entire
post-loop suffix. Proof traversal and scratch sizes are bounded.

The proof is derived after current host linking and rebuilt after prepared
rebinding. It checks the actual callback implementation and full value-call
contract; a capability ID alone cannot establish ownership. Canonical and
serialized instructions do not carry this derived permission. The retained
plan stores indices and identity scalars rather than pre-ownership pointers.
A whole-entry guard rechecks the bound function, callbacks, data-bank shapes,
count and disjointness from mutable banks. Missing or incompatible binding,
inspection, resumed continuation or a count outside the finite bound selects
the complete canonical body, including ordinary result retention.

The private item buffer is limited to 64 KiB. It is first allocated at add,
after the first row expression has completed. Zero/negative and very large
counts retain ordinary behavior through the existing body/guard rules. Every
original source budget check remains; the optimization does not inspect a stale
backend-local budget or introduce a new language rejection for a large count.

Bytecode, native SLJIT and generated Wasm can use the derived body. Inspection
and detached debug continuations use canonical execution. Generated C stays
canonical, including emitted callees; no optimized-C claim is made. The public
direct SLJIT entry has the same guard as ordinary compiled dispatch.

## Ownership and error order

A private builder owns one layout and item buffer in the invocation arena.
Each successful append snapshots only the incoming row and produces a fresh
logical array header. Old headers keep their initialized counts and all existing
items unchanged. The new item is published only after its original metadata,
payload and strings are validated and copied. Tracing walks initialized count,
not uninitialized capacity. Complete result headers carry the owned layout/name;
the final public return still takes the ordinary owned snapshot.

The proof, rather than same-arena membership, establishes that this shared
buffer cannot escape or be mutated through an old prefix. Other callbacks,
array operations, aliases and reference-write shapes keep canonical semantics.
The helper skips only a previously proven destination-ownership scan; distinct
incoming descriptors are validated before canonical substitution. Failure leaves
the output and initialized prefix unchanged, though arena scratch may advance.
Removed redundant allocations are no longer allocation-failure points.

Inspection uses unchanged source points and snapshots. The fixed inspector
case stops at declaration, the RHS local read, and post-add/pre-assignment;
it verifies old local count zero and staged result count one at the last stop,
then detaches and resumes the canonical continuation. Error controls compare
exact status/category/source/node/prefix and optional message presence/content
for third-row index and division failures.

No grammar, public opcode, prepared format or project migration changes are
needed. The factored ordinary add callback lives in types/execution, outside the
compiler-only build option. The new helper header is private.

## Measured selected copies

The prior owned-CALL checkpoint is documented in
[ordinary row-array construction](row-array-construction.md). Current focused
source and fixtures passed at `a906c0b68ec629246e7878c0a31689898b5aa30f`.
Native and Wasm normalized counts agree for bytecode and their actual compiled
backend. For the same sixteen-row authored add workload:

| Selected attribution | Previous | Counted builder |
|---|---:|---:|
| Row-payload copy equivalents | 321 | 65 |
| Text bytes | 15,120 | 3,600 |
| Value-header moves | 533 | 261 |

A row payload is 56 native bytes or 36 Wasm bytes. Selected record bytes exclude
metadata. These measurements include constructor, local/argument retention,
callback and return work, with host preparation outside the scope; no estimated
overhead is subtracted. Direct public HOST/CALL add and singleton append remain
unchanged. Generated C retains the canonical 321-row-copy result. Selected
counters do not count every raw header write, allocation or metadata scan.
Arena capacity/block metrics are separate; functional run times are not a timing
benchmark or speedup claim.

## Validation and durable coverage

The five focused routes passed serially at the checkpoint above: generated C
1 case/2,389 assertions, native builder 7/9,751, Wasm builder 7/9,599, and the
existing row-array native/Wasm routes 5/16,113 each. Total: 25 case executions
and 53,965 assertions. Full source/build/preservation postflight matched.

The seven-case builder fixes 26 analyses (24 executable and two catalog-only),
four prepared decodes, 48 invocations, six inspector resumes, six private-helper
entries and two collections per target. Ordinary successful sizes are -1/0/1/4/16.
Two large-count controls per backend retain the 64-source-check budget and
measure exactly fifteen completed rows below 4 MiB arena capacity. This avoids
amplifying an enormous requested count into an eager reservation.

The raw-root collection occurs at count three/capacity four, retaining both old
and new headers before completing the fourth row. A later rootless collection
requires zero live used arena bytes. Repeated calls, producer-text mutation and
results retained after producer/module retirement check ownership without a
repair snapshot. Malformed and foreign incoming layouts must reject before
substitution. Static fallbacks include a retained prefix alias, a distinct
reference-write result, an early exit and a non-unit increment. Callback
replacement, coherent alternate data, missing function identity and prepared
rebinding controls exercise whole-body fallback.

The C fixture compiles one bounded source file with four emitted bodies. It
explicitly exports the actual linked module sibling through the public API;
it does not claim automatic call-graph export. Its fixture-managed one-call
trampoline validates the actual pending descriptor and banks, invokes that
emitted target and resumes the saved continuation. `source_function` stays
unset. Fourteen top-level calls, two nested calls and two resumes span the
bytecode/C paths; the single compiler child has the existing ten-second cap.

The final durable candidate includes the seven builder cases exactly once in
the native language and serial Wasm groups and the C case once in compiler-enabled
`native-tools`. The existing same-child policy oracle requires both row-array
and row-builder memberships exactly once. At clean `631b15fdbc7380304eeb0ebbd67d56038f9a77c1` (tree
`ede5fb62ffb8f454b7fe387dd3e9a9b8e79dea0d`), the complete final gates passed:

| Complete command | Members | Cases | Assertions | Seconds |
|---|---:|---:|---:|---:|
| `group language` | 33 | 423 | 134,404 | 21.200 |
| `group wasm` | 21 | 265 | 130,137 | 3.899 |
| `tool native-tools` | 1 aggregate | 117 | 35,617 | 23.482 |
| `tool language-record-compatibility` | 1 | 1 | 1,380 | 0.067 |
| `tool wasm-record-compatibility` | 1 | 1 | 1,380 | 0.128 |

These are 807 case executions and 302,918 assertions. The final tree changes
only the reviewed durable wiring and guide from the focused checkpoint;
production and language fixtures/binaries are identical. The tooling binary
includes the reviewed membership oracle and one new aggregate call. Independent
postflight matched all 18,272 recorded identities, clean source and no surviving
workload process. Each compatibility route covers 48 native int/string/reflected
struct array callbacks, maximum five items, with no global-service case.

Reproduce through `python3 tools/test_policy.py <command>` with coherent
compiler-enabled/native-disabled language and tools builds, matching C-Wasm
artifacts and pinned Node on PATH. `CHIBI_LANGUAGE_BUILD_DIR`,
`CHIBI_WASM_BUILD_DIR` and `CHIBI_TOOLS_BUILD_DIR` select those builds. Group
membership and the C aggregate guard are checked by the existing policy case;
registering a future standalone selector alone does not add it to these gates.

Native Debug uses assertions, Clang 19, ASan and UBSan; C-Wasm uses pinned
Emscripten 6.0.10 and Node 24.19.0. Every complete command retains its existing
60-second production deadline including children. Leak detection is untested.
Independent CUT62 review found the arena root/reclamation, scratch/plan cleanup,
backend and sanitizer coverage applicable to this diff, with no extra blanket
LSan requirement. This does not establish absence of all heap leaks or transfer
another PR's waiver/hold. No installed compiler-off runtime run or current Studio
render/translation result is claimed. Adversarial canonical-IR proof shapes have
source-review coverage rather than independent IR-mutation executions.

## Preserved corrections

All stopped checkpoints and their exact artifacts/logs remain preserved:

- `8927759e`: intended loops were safely declined because the constructor base
  bank was cleared after use but had no per-iteration prewrite. Native 1 passed,
  6 failed; no optimized entry. The bounded `17b4449b` diagnostic reproduced
  that result and exposed the exact canonical bank chain. The correction proves
  only the unique constructor base's initial declaration, exclusive use and
  pointer-width null clears; field/result freshness remains unchanged.
- `deeef225`: optimized paths measured 65 copies, but three control oracles
  failed. The fixture had confused source checks with IR instructions, required
  a nonnull optional error message, and omitted an intermediate RHS source stop.
  The correction fixes the budget unit, retains strict optional-message parity
  and adds one fixed resume per backend. The excessive-prefix failed result is
  retained, not relabeled as a resource-bound pass.
- `fa0abac6`: the native route aborted under UBSan; its buffered output did not
  identify the reached case or counts. Source and frozen disassembly established
  a genuine guard ABI defect: an AL-only C `bool` return was compared as a full
  32-bit value. Normalizing the return with `SLJIT_MOV_U8` fixes the guard. The
  adverse direct-entry missing-function control is retained and later passes.
- `bc296cd6`: both builder routes passed, then C preparation rejected a fixture
  assumption that a module sibling was a wrapper-owned definition. No compiler
  child or C sample ran. Explicitly exporting the actual uniquely linked sibling
  fixed the fixture without expanding the planned four-body ceiling,
  preparations or calls.

Machine-readable checkpoint, raw-log and review hashes are in
[evidence/counted-record-construction/results.json](evidence/counted-record-construction/results.json). Published-tree/current-main correspondence and actual server
requirements remain separate landing checks.
