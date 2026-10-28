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

## Remaining integration

Next production work still needs a reviewed mechanical text API and a coherent
owned/current-generation scene read boundary covering prototypes, attachments,
selection, view and rename state. The held snapshot work is not imported or
duplicated here. Current-SDK Studio integration, virtual-root/fold/reveal/UI
publication, history/commands and rendered/input parity remain open. No project
version change or migration is needed for this test-only specimen.
