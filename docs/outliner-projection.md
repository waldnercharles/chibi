# Outliner selected-range projection specimen

The existing twelve-field Outliner row policy is now exercised as authored
`.chibi` source over owned synthetic inputs. The C fixture uses fixed independent
expected values and the normal nested-call runner on bytecode, native SLJIT and
generated Wasm. This is a consumer contract and copy-cost baseline; Studio's
live read boundary, panel assets, commands and rendering are unchanged.

The native source oracle is Studio commit
`07dccf7b5fcfcc879b9d24e95730f4ee1d6a281f`, tree
`2f66ad47f0f13437f9c86dda1d7921ccd0b3c369`: `studio_node_calls.c`,
`studio_outliner.c`, `studio_shell.c`, `studio_drop.c`, the actor/view records and
`kit_icons.h`. The local reviewed Studio tree was identical. Native
`scene_objects` already allocates its selected slice once; this work does not
claim to improve or execute that native producer.

## Policy in authored source

`test/language/outliner_projection.c` contains an `OutlinerProjection` library
using the existing `Row :: struct` syntax. It filters actor name OR prototype
name in scene order, computes Shown, clamps First/Count, and constructs only the
requested matching slice. The row expressions and helper functions implement:

| Field | Contract |
| --- | --- |
| key | Full unsigned decimal entity bits, without floating-point conversion |
| label | Complete actor name |
| icon | First present component icon in native enum priority, or prototype fallback |
| prototype | Raw prototype name, empty for absence |
| drop | Empty when read-only; otherwise ordered lowercase object/behavior and assignable-kind labels |
| depth | One plus parent-chain length, including filtered-out ancestors |
| behaviors | Attachment-list count |
| active | Actor index equals the active index |
| marked | Selected and not active |
| hidden, locked | Corresponding view bits |
| renaming | Outliner rename state and exact full entity identity match |

Icon selection, drop labels, filtering, slicing, depth, selection and rename
policy remain authored. C supplies facts, assertions and two **test-local**
mechanical text bindings: full-width decimal entity conversion and unsigned-byte
case-folded substring matching. Standard entity display currently includes its
nominal prefix; converting through double would lose identity above 2^53.
Neither fixture binding is delivered as a production language capability.

Inputs are nine equally sized typed arrays plus scalar state. Parents are -1 or
an earlier actor index; four preorder actors permit at most three links. Names
and prototype text are non-NULL; absent prototype text is empty and absent or
unresolved metadata has no icon bits. The fixture asserts the C/POSIX locale.
Non-ASCII matching checks exact bytes, not Unicode case folding. Formatting zero
produces "0"; that is separate from an absent actor producing no row or the
native null-actor key helper returning empty text.

## Fixed correctness and lifetime coverage

Each native/Wasm route has two cases, four module preparations and 28 top-level
calls. Twelve requests per backend cover empty input, full/interior/overrun
windows, negative First/Count, First beyond the end, name-only/prototype-only/no
match, non-ASCII text and read-only/selection/rename state. Two further
publications per backend exercise replacement and retirement. Four actor IDs
include high bits, values above 2^53 and equal low words with distinct identities.
Every returned field, Shown, order and glyph byte sequence is checked.

The fixture verifies input arrays and strings remain unchanged. It then poisons
the actual owned input strings as well as their earlier producer, frees those
arenas and retires the source/module while retaining both publications. Every
result layout/name, array header/item buffer, row payload and string belongs to
the result arena before any retirement; no test-side repair snapshot is used.

There are 144 nested helper calls and 144 parent continuation entries per route,
for 316 physical entries: 158 bytecode and 158 actual SLJIT/generated-Wasm. These
are ordinary nested calls, not a live program suspension scenario. Measured
custom counters report 132 matches and 48 authored decimal conversions, plus
two direct zero-handle checks outside invocation scopes. Source/control-flow
accounting additionally gives 88 format calls, 78 index-array adds, 48 row adds
and 56 length calls: 450 authored native callback sites. This last total is not
a general runtime host-event counter and excludes execution intrinsics. The
fixture validates the actual lowered callback contracts and twelve static
source sites separately.

## Observed selected copies

The real policy shape includes helper calls, nested traversal and conditionals,
so the counted-record builder is correctly not selected. No recognizer change
or native precomputation was introduced to force that optimization.

For the full four-row request, bytecode and actual compiled execution agree:

| Selected attribution | Native | C-Wasm |
| --- | ---: | ---: |
| Record bytes | 1,848 | 1,188 |
| Row-payload copy equivalents | 33 | 33 |
| Text bytes | 3,122 | 3,122 |
| Nominal-name bytes | 104 | 104 |
| Value-buffer bytes | 0 | 0 |
| Header moves | 172 | 172 |
| Output-arena used bytes after call | 33,889 | 23,609 |
| Output-arena capacity after call | 40,982 | 28,582 |
| Backing-block allocation delta | 8 | 8 |

Record layouts are 56 and 36 bytes respectively. Copy scopes include actual
constructor, helper argument/return and result-retention work; setup is outside
the scope and no hypothetical helper costs are subtracted. These counters cover
selected C operations, not all memory traffic. Output-arena totals exclude
transient child arenas. The block delta spans the invocation and is a separate
coarse allocator measure. Four actors do not establish large-scene performance,
linear scaling or a timing speedup.

## Validation and remaining integration

The initial standalone pair passed at `6de906a8`: two cases and 10,786
assertions per target, in 0.245 seconds native and 0.291 seconds C-Wasm. The
unchanged specimen then passed inside the complete durable gates at
`f818851614ae4bc2721bb1ddba098ddfb32849e6`:

| Complete command | Cases | Assertions | Outer seconds |
| --- | ---: | ---: | ---: |
| Native language, 34 members | 425 | 145,190 | 20.956 |
| Serial Wasm, 22 members | 267 | 140,923 | 4.131 |
| Compiler-enabled native-tools | 117 | 35,617 | 22.918 |

Each command retained its own shared 60-second deadline including children.
The total is 809 case executions and 321,730 assertions. All 10,347 pre/post
identities remained unchanged. Both language groups include the new selector
exactly once, and the existing same-child tooling assertion now guards that
membership alongside row-array coverage. The tooling duration
above is the final outer supervisor, not an internal fixture's nested timing.
Only this scope/evidence documentation follows the tested code checkpoint;
published-tree correspondence and ordinary server/review gates remain required.

The first native checkpoint `4e50ac79` failed both cases during preparation,
before any authored invocation: its extra accounting oracle incorrectly
compared descriptor object addresses after finalization had copied descriptors
into module-owned storage. The fixture-only correction compares every callback,
flag and nullable service string, with intrinsic ID/ABI validation and the same
five allowed source kinds/static counts. The failed log and artifacts remain
preserved; no production contract or workload was weakened.

Native validation uses ASan/UBSan. Leak detection remains untested; this
test-only change adds no production ownership path. It does not change any
other PR's explicit hold. Raw logs, checkpoint identities and selected
measurements are summarized in [the evidence index](evidence/outliner-projection/results.json).

Next production work still needs a reviewed mechanical text API and a coherent
owned/current-generation scene read boundary covering prototypes, attachments,
selection, view and rename state. The held snapshot work is not imported or
duplicated here. Current-SDK Studio integration, virtual-root/fold/reveal/UI
publication, history/commands and rendered/input parity remain open. No project
version change or migration is needed for this test-only specimen.
