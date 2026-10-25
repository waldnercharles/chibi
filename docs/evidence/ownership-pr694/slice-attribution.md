# One authored string operation: allocation attribution

2026-10-25. Exact runtime/compiler source: PR694
`f4458b21f67badda787970218e3f8058d44cccf9`, plus reviewed diagnostic commits
`3a84078a48c43ba1c32fc9a1e8310411af793140` and
`dd51c1d55944941ff8f46da32b964189c648c9ee`. This checkpoint changes only test
instrumentation, dispatcher registration and evidence. No runtime behavior,
GC policy, ownership flag, capability ABI, seed or release is changed.

## Actual operation and boundary

The compiler import path is concrete:

1. `tools/compiler/source_snapshot/Snapshot.chibi:376` slices an import literal
   from document text.
2. `tools/compiler/package_routes/Routes.chibi:165` passes it to
   `compiler_json_string`. `src/toolchain/compiler_json.c:30–42` parses the
   explicit byte length, copies decoded bytes into runtime-owned text, then
   frees the JSON document. That decoded-output copy is required by this
   current parser ownership contract.
3. `Routes.chibi:175` extracts the collection name with
   `strings.Slice(path, 0, colon)`, then compares it to configured roots.

The new `test_language --slice-attribution` pico_unit case invokes the shipped
`core:strings.Slice` export, with `("core:scalar", 0, 4)`, corresponding to
step 3. It executes bytecode and compiled/SLJIT backends. This is **not** an
execution of the complete ImportRoute, JSON decoder or self-compiler, and it
establishes no frequency or memory dominance in a full compile.

Existing source comparison remains applicable: accepted main
`957968675031431c14b1abf1b5f8823aebd71097` and the observed local M2 snapshot
`b1fcb5e006d8e2e16b4a39dddff6aafbd92075d1` have the same native text-slice
implementation and unmarked `NATIVE_TEXT_SLICE_CALL`. Count/SplitKeeping's
repeated suffix slicing was already fixed on these sources; see
[text-followup.md](text-followup.md). HasPrefix still calls Slice, so substituting
HasPrefix is not an established allocation-free fix.

## Measurements and literal lifetime checks

Four producer forms, one call each per backend (8 calls total):

- `form=0`: owned 11-byte `core:scalar`.
- `form=1`: borrowed stack descriptor and bytes, with no backing owner.
- `form=2`: owned text shared across arenas, with original producer destroyed.
- `form=3`: nested owned windows of `[core:scalar]`; both earlier descriptor
  arenas are destroyed before the call. The surviving window has length 11
  and is not terminated at its boundary.

Every result is checked against the independent literal `core`, including
length, bytes and trailing NUL, after input storage is destroyed/borrowed bytes
are overwritten. It is retained into another arena, execution storage is
freed, and the literal plus `pin_text_cstring` result is checked again. These
are ordinary arenas; no collection occurs during or after execution. Earlier
collectable probes remain separate evidence, not coverage of this call's roots.

The existing native measurement binding delegates unchanged and preserves the
production descriptor flags. An inner copy scope isolates the native primitive;
the outer scope excludes it. A pass-through `context.copy_text` callback counts
that separate path without changing results. Preparation/input construction and
post-call detached retention are outside the copy scopes.

| Observed per call, identical on both backends | Bytes / count |
| --- | ---: |
| Native copied payload, including trailing NUL | 5 B |
| Native arena descriptor occupancy delta | 24 B |
| Native separate cleanup allocation delta | 32 B / 1 node |
| Native unique external owner allocation | 21 B |
| Cleanup nodes before native primitive | 1 |
| Cleanup nodes at authored return | 4 / 128 B |
| Unique retained resources at authored return | 2 |
| Ordinary owned input: unique external allocation at return | 49 B |
| Nested window input: unique external allocation at return | 51 B |
| Outer copy scope's VALUE header moves | 5 |
| Outer text bytes, owned inputs | 0 B |
| Outer text bytes, borrowed input | 12 B |
| `context.copy_text` calls / cleanup bytes | 0 / 0 B |

The native delta is one `pin_string` descriptor (only its allocation occurs
inside this primitive) plus its ordinary cleanup node. Total post-call arena
used bytes are 265 (nested source: 267), including retained external bytes;
this is not total physical storage or RSS. The 49 B is input owner 28 B plus
result owner 21 B, counted once each despite four attachments. Native input
from the borrowed producer has already been stabilized; do not label that
native argument as still borrowed.

One ingress attachment exists before the native call. The native adds one.
Two more exist at return; source inspection identifies the defensive native
result copy below as one such path. The final attachment is not independently
attributed to a particular instruction in this checkpoint. The zero callback
count rules out `context.copy_text`/BF_COPY_STRING here. The header count alone
must not be used to label all descriptor creation sites.

## Smallest concrete follow-up proposal — held, not implemented

Investigate declaring **only `NATIVE_TEXT_SLICE_CALL`** as `owned_result=true`
through the existing native-definition contract, preserving `text_slice_call`'s
current `pin_value_text` payload construction. The native currently always
constructs independent, terminated text in supplied storage (or canonical empty
text). Its false flag at `src/runtime/text_format.c:498` triggers a defensive
`pin_value_collection_copy` at `src/execution/function.c:844–855`, which shares
the already-owned backing but creates another descriptor and cleanup attachment.
The proposal targets that redundant same-value transfer, not the payload copy.
On the measured ordinary-arena path, the predicted saving is one 24 B descriptor
and one 32 B cleanup node per call; **this is not a measured optimization result**.

Two gates remain before implementation:

- **Managed/nested return lifetime:** native allocation uses `value_storage`,
  while the defensive copy can target `managed_heap->storage`. Activation
  payload and managed heap can differ (`program_activation.c:272–274`), as can
  frame and parent result storage (`call.c:354–358`). Prove the existing
  owned-result protocol keeps text descriptors rooted through those boundaries,
  or retain the copy there. This probe has no managed heap and cannot supply
  that proof. Do not compensate with a GC-policy change.
- **ABI ownership:** `native_definitions.c:118` propagates the call flag;
  `native_capabilities.c:323` validates declaration/binding agreement;
  `capabilities/catalog.c:651` hashes it into the capability ABI. Any flag
  change needs the separate owner's integration/rebuild decision. Nothing is
  being integrated or regenerated by this checkpoint.

Regression plan for that separate candidate: rerun this same literal/lifetime
probe and compare native bytes unchanged versus the predicted single attachment
reduction; add an actual managed caller and nested return with distinct producer,
result and heap storage, free producers, and verify retained literals before/after
collection only at an already supported fully rooted post-call boundary. Cover
empty and clamped bounds, borrowed descriptors/PIN_NAME at the native API (NAME
is not a string-typed authored argument), nested nonterminated windows, embedded
NUL and byte-invalid UTF-8. Check C-string rejection/termination and formatted
literal output. Exercise native declaration/ABI mismatch rejection and both
backends. Preserve production binding flags in any measurement wrapper.

## Why generic reuse is not justified

`pin_value_text_share` already shares owned bytes. Both share and slice still
create independent descriptors/retains. Ownerless/NAME inputs copy. A descriptor
in another arena or on the heap cannot be reused after its producer expires;
`pin_text_destroy` also independently frees heap descriptors. Mere
`arena_contains(destination, descriptor)` does not prove a backing retain is
attached there: descriptors are public and can have been copied as borrowed
headers. There is no existing owner-safe generic descriptor-reuse API here.

Nested slices retain the original whole backing. Unconditionally replacing
native Slice with shared windows would leave descriptor/cleanup pressure,
retain larger documents, and change the result's guaranteed NUL termination.
`pin_text_cstring` correctly rejects embedded NUL and materializes terminated
text when necessary; its returned raw pointer acquires no independent lifetime.
The apparent snprintf text branch at `text_format.c:238` is unreachable for
scalar text through `fmt_render_text`; it is not evidence of a current bug.

The owner count is atomic, but arena allocation/cleanup is not thereby thread
safe, and retaining requires a live source. No new cross-thread publication or
cache is proposed. Owned backing is not universally immutable in current source:
`execution/program.c:777,789` and `state.c:201–207` mutate PIN_STR selection state;
`preserve_state` can snapshot it into another arena using shared text. No harmful
execution schedule was demonstrated. This is a reason to avoid blanket global
interning/reuse claims, not a newly established runtime defect.

## Reproduction, validation and limits

Same Mac/toolchain and warning exceptions as [text-followup.md](text-followup.md):
macOS arm64, Clang 22.1.8, CMake 4.4.3, Ninja 1.13.2, Debug, dead stripping,
no sanitizers. Fresh isolated libraries rebuild every consumer of the previously
expanded profiling structs. No library is installed into the main/M2 lane.

```sh
cmake -S test/language -B scratch-profile/route-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang \
  -DCHIBI_SDK_BUILD_COMPILER=ON -DCHIBI_SDK_BUILD_NATIVE=OFF \
  -DCHIBI_SDK_BUILD_DESKTOP=OFF \
  -DCHIBI_THIRD_PARTY_CACHE=/Users/cwaldner/Documents/Codex/2026-10-24/task/chibi/.git/chibi/third_party \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip \
  '-DCMAKE_C_FLAGS=-Wno-error=missing-field-initializers -Wno-error=unused-parameter -Wno-error=unused-function -Wno-error=unused-variable'
cmake --build scratch-profile/route-build --target test_language --parallel 2
python3 tools/test_policy.py --timeout 15 native /usr/bin/time -l \
  scratch-profile/route-build/test_language --slice-attribution
python3 tools/test_policy.py --timeout 15 native /usr/bin/time -l \
  scratch-profile/route-build/test_language --package-core
```

Configuration: 3.067 s. Initial driver build: 21.621 s, maximum child RSS
173,391,872 B, two jobs; subsequent test-only edits rebuilt incrementally.
CMake embeds source bytes and builds the C stage-zero SDK; this focused target
has no authored compiler/asset generation or stale-seed dependency. No checks
were disabled. Each execution is capped at 10,000 VM instructions; each process
at 15 s. Fixture preparation is two small files per backend, not self-compilation.

[slice-attribution.txt](slice-attribution.txt): **1 pass / 180 assertions**,
0.61 s process elapsed, 0.697 s policy wall time, maximum RSS **21,528,576 B
(20.53 MiB)**, including fixture preparation and JIT. It is not per-call RSS.

[slice-regression-core.txt](slice-regression-core.txt): **3 pass / 6 fail /
3226 assertions**, 0.33 s process, 33,947,648 B RSS. Unchanged reviewed
`dd51c1d` in a separate detached worktree reproduces the same failed assertions
and totals: [slice-regression-core-baseline.txt](slice-regression-core-baseline.txt),
0.94 s process, 33,718,272 B RSS. Failures include array-return calls without a
managed heap and the existing pure/effect expectation; this checkpoint does
not repair that separate fixture migration. The focused scalar probe passes.
Full engine/material/project/language suites were not run, consistent with
this lane's explicit bounded scope.

Independent source/receipt review found no blocking diagnostic issue and required
the managed/nested lifetime gate above. Prior receipts remain unchanged. Generated
build directories and the baseline worktree are removed after validation; scratch
logs remain under `scratch-profile/`. No optimization, full compiler memory claim,
merge, PR, pin, seed or release is included.
