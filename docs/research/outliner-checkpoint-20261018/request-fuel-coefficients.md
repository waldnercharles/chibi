# Outliner request fuel: static bounds and unresolved canonical weights

This is a review artifact, not an implementation or test result. Inspection was
read-only; no producer, fixture, build, preparation, or execution was run for
this work. The only file written is this document.

The shipped sources support a finite, checked formula for **authored
checkpoints across the whole synchronous request**, including helpers. There
is no saved canonical `BF_CHECK` census in the inspected artifacts. Therefore
the numeric checkpoint coefficient is unresolved; it must not be replaced by
the earlier 33,111 top-frame observation or an arbitrary large allowance.

## Source evidence and scope

Inputs inspected in `/workspace/chibi-studio-outliner`:

| File | SHA-256 |
| --- | --- |
| `projects/studio/content/behaviors/OutlinerRows.chibi` | `510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450` |
| `projects/studio/content/libraries/OutlinerRowPolicies.chibi` | `e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5` |

Also inspected `src/editor/studio/studio_scene_read.{c,h}` and
`studio_outliner_projection.c`; engine `src/language/lower.c`,
`src/execution/{definition.h,function_builder.c,opt.c,row_builder.c}`, and
`src/runtime/text_format.c`. The inspected lowerer SHA-256 is
`2658fc232b983e2d91a983998e3ad5e306963a428f9687e11c23ca83d778cc97`.

`Refresh` and `RevealObject` each execute exactly one Capture, one Project and
at most one Publish. Reveal first assigns `Collapsed = false`; Capture reads
that updated protected state. They have no authored loops outside Project.
Their successful-publication branch also assigns `ShownOpen`; that tail needs
fuel. Other controller events are outside this proposed opt-in policy.

Project's reachable authored library contains twelve definitions: Project,
Page, ContainsFolded, FoldByte, Depth, Icon, Drop, IntRange, SelectState,
BehaviorCount, Title and LastIndex. No recursion occurs in this source.
Native array/text/Capture/Publish callbacks are not additional authored
invocations. The 22 library named-constant evaluations documented by the
existing preparation fixtures occur during compilation, outside the request.

## Exact dimensions available from the owned capture

Use the successful `studio_scene_read_facts` belonging to the current request,
not a caller-supplied summary or a later live scene read:

| Symbol | Owned fact / definition |
| --- | --- |
| N | `actor_count`; both `nodes` and `actors` have N entries |
| P | `prototype_count` |
| C | `component_count`, concatenated prototype component spans |
| F | `field_count`, concatenated actor assignable-field spans |
| A | `attachment_count`; duplicates and nonmatching entities remain entries |
| q | `strlen(facts->filter)` in bytes |
| l | `strlen(facts->scene_path)` in bytes |
| n[i], p[i] | actor name and actor prototype-name byte lengths |
| t | checked sum of all `n[i] + p[i]` |
| r | retained RevealObject payload byte length; Refresh uses the empty string |

Capture allocates and populates exactly N/P/C/F/A output elements, plus one
Scene record. Prototype spans are built by appending each prototype's
components. Actor field spans are built by appending that actor's assignable
fields, so they are disjoint. All registered attachments are captured, in
registry order. These statements follow `studio_scene_read.c:133-291` and
`studio_outliner_projection.c:364-430`.

The hierarchy snapshot rejects missing/later parents: every parent is -1 or an
earlier actor index. The adapter already rejects N == INT_MAX to leave room
for the virtual root. None of these facts imposes a new scene-size limit.

## Viewport bound, including signed extrema

Let First, Count and Collapsed be the protected values actually read by
Capture. Perform signed comparisons before converting to an unsigned fuel
type. This is Page's result with `shown = N`, which bounds its result for every
filtered `shown <= N`:

```text
start = First > 0 ? First - 1 : 0
start = min(start, N)
amount = Count > 0 ? Count : 0
if Count > 0 and First < 1: amount -= 1
V = Collapsed ? 0 : min(amount, N - start)
```

No subtraction of `First - 1` occurs for INT_MIN. `amount - 1` only occurs
after positive Count, and `N - start` only after clamping. Thus:

| First | Upper emitted actors when open |
| --- | --- |
| <= 0 | `min(max(Count - 1, 0), N)`, with the guarded subtraction above |
| 1 | `min(max(Count, 0), N)` |
| 2 | `min(max(Count, 0), max(N - 1, 0))` |
| INT_MAX | 0 for the captured N <= INT_MAX - 1 |

Count <= 0 gives V = 0; Count == INT_MAX still clamps to available actors.
`rootShown` is true only for First == 0, but a negative First has the same row
amount bound. Collapsed gives V = 0 while **all N actors are still matched**
and Title still executes. Reveal's pre-Capture state write means it cannot use
a previously collapsed V = 0 allowance.

## Matching: candidate starts, comparisons and helper multiplicity

For each of both actor strings with byte length n, define:

```text
T(n,q) = 0                     if q == 0 or q > n
         n - q + 1             otherwise
R(n,q) = q * T(n,q)
T = sum_i (T(n[i],q) + T(p[i],q))
R = sum_i (R(n[i],q) + R(p[i],q))
```

ContainsFolded is called at most 2N times. Short-circuiting can reduce this:
for an empty filter the name call succeeds and the prototype call is skipped.
T bounds all outer-loop iterations, and R bounds all attempted byte-pair
comparisons, including the final mismatch. Each attempted pair invokes
FoldByte twice, so **2R** is necessary; counting only matching-loop bodies
misses those calls. Successful inner-body iterations are also <= R. This
remains conservative when either string matches early. For nonempty queries,
repeated prefixes such as text `aaaa...a` and query `aaa...ab` exercise the
full candidate-by-query comparison bound. The plan's `S` equals `2N + T + R`.

The formula does not rerun matching in native code to compute the allowance.
One byte-length pass over the owned strings suffices. Validate conversion of
lengths to the domain used by the native byte operations; those operations
reject lengths greater than INT_MAX. Every sum/product is checked before use.

## Visible-row helper bounds

Let `cmax` be the maximum captured prototype componentCount (zero for P = 0),
and `fmax` the maximum captured actor fieldCount (zero for N = 0). Then:

```text
Cv = V * cmax
Fv = min(F, V * fmax)
Av = V * A
```

Cv is needed because many actors may reference the same prototype: the total
captured C is not an upper bound on repeated visible-row component scans.
There are at most V component IntRange calls and V Icon calls, each together
processing <= Cv items. Field IntRange always executes for each visible actor,
even when Scene.editable is false; its total is <= Fv. Drop scans <= Fv items
only when editable, but its invocation/setup still exists when not editable.
Drop has five fixed suffix branches/format calls after its scan; those are
covered by its per-invocation checkpoint weight, not another F multiplier.
SelectState executes once per row. BehaviorCount executes once per row and
examines all A entries, even if few match. Row assembly, key formatting and
the row append happen at most V times.

For hierarchy work, compute ancestor counts once in actor order:

```text
ancestors[i] = parent[i] < 0 ? 0 : ancestors[parent[i]] + 1
h = max_i ancestors[i]
D = V * h
```

Then D bounds all Depth loop iterations; the returned visual depth is
`ancestors[i] + 1`. This native bound computation is O(N) with a checked N-entry
temporary vector and uses the already validated hierarchy. Without the
vector, `D = V * max(N - 1, 0)` is safe but looser. An optional tighter bound
is `min(V*h, sum_i ancestors[i])`; it is not required. The chain extremum has
h = N - 1; the flat extremum has h = 0. No unknown ancestor is skipped.

## Title, reveal and native callbacks

Title is invoked once, independently of V and Collapsed. For empty path it
returns `Scene`. Otherwise LastIndex scans the full original path for `/`,
then scans the sliced title for `.`. The latter has at most
`u = min(l, 127)` bytes: truncation precedes extension removal.

Both needles are exactly one byte. Across the two LastIndex calls there are
at most `l + u` outer iterations and at most two successful inner-body
iterations, because each first successful match returns immediately. Thus
`4 + l + u` bounds their combined invocation-plus-loop multiplicity (two
invocations, two successful inner iterations, and all candidate starts).
Leading-dot, no-dot, no-slash, trailing-slash, empty-title and 127-byte
truncation cases cannot exceed it. Using just 127 for the entire path scan
would be unsound.

The actor scan can test reveal state up to N times; the first successful
reveal match disables later comparisons. Empty-string comparison and
`entity_decimal`/`text_equal` are native, with no authored loop. Decimal entity
text is at most 20 bytes (`text_format.c` uses a 21-byte buffer). r is relevant
to retained payload storage; it does not multiply authored checkpoint count.
Likewise t and q describe copied text and matching dimensions, not independent
per-byte authored loops beyond T/R above.

**Native work is not bounded by this checkpoint account.** In particular,
`text_bytes` calls `strlen` on every `text_byte_at`, not just at initial length
queries (`src/runtime/text_format.c:384-437`). For one n-byte text, attempted
pair lookups can therefore scan up to `R(n,q) * (n + q + 2)` bytes including
terminators, in addition to length/setup scans. LastIndex's one-byte lookups
can similarly cause quadratic native length-scanning work in l, despite
linear authored checkpoint count. Native Capture also includes parent lookup,
prototype lookup and schema traversal. The account cannot interrupt these
callbacks. Scalar append reduces array copying; it does not change this text
behavior or create a wall-time guarantee.

## Conservative grant formula with a canonical weight

Let K be the **checked sum of the number of canonical `BF_CHECK` sites** in
the frozen prepared closure reachable from Refresh and RevealObject,
deduplicating identical function bodies. Include called/defined/imported
bodies; do not use `runtime_body`, compact instructions, a `fast` alternative,
effect summaries, raw instruction count, source lines, or dynamic top-frame
steps. Including both handler bodies in K is conservative even though one
request enters only one. Reject an empty/missing/incomplete census instead of
inventing K. The engine must actually debit every canonical-equivalent
checkpoint under opt-in, including folded/root-step paths and helpers.

For these exact sources, a deliberately conservative multiplier is:

```text
W = 8 + 3*N + 8*V + T + 3*R + D
      + 2*Cv + 2*Fv + Av + l + min(l, 127)

total_limit = 10,000 + K * W
```

All operations, including multiplication by K and addition of the existing
bootstrap 10,000, require checked arithmetic in the account's `uintptr_t`
domain. Overflow fails the request before Project/Publish; do not saturate,
wrap, silently omit a term, or treat zero as unlimited. The grant is an
absolute total on the existing account and does not reset spent work. The
bootstrap is deliberate slack for already-spent entry work; it is the current
source allowance, not a newly chosen scene threshold.

W comes from the following finite census of source control-flow visits:

| Body | Upper bound on invocations plus all loop iterations |
| --- | --- |
| selected controller handler | 1 |
| Project | `1 + N + V` |
| Page | 1 |
| ContainsFolded | `2*N + T + R` |
| FoldByte | `2*R` |
| Depth | `V + D` |
| Icon | `V + Cv` |
| Drop | `V + Fv` (or `V + editable*Fv`) |
| IntRange | `2*V + Cv + Fv` |
| SelectState | V |
| BehaviorCount | `V + Av` |
| Title | 1 |
| LastIndex | `4 + l + min(l,127)` |

Summing this table gives W. A particular static checkpoint can execute at most
once on a loop-free invocation path, plus once per relevant loop iteration;
counting every loop iteration in the closure is conservative for every site,
including nested loops and inlined helpers. Multiplying the sum of static
sites K by total visits W therefore overcounts safely. Source uses only
structured loops/branches with this acyclic call graph. This argument is not a
generic cost proof for arbitrary authored programs.

A smaller weighted sum can multiply each table row by its own canonical
checkpoint count K_f. Only use that after verifying how prepared bodies
represent outlined/inlined helpers; attribution by source/frame must not
accidentally charge a helper loop as a once-per-caller block. The common K
formula avoids that attribution requirement, at the cost of a looser cap.

P, C, F, A and t remain useful exact snapshot evidence. There is no separate
authored full-P/full-C/full-F loop or per-byte snapshot loop to justify adding
`ksnapshot*(N+P+C+F+A)` or `kt*(t+q+r)` as checkpoint counts. Such added slack
would not meter the native capture/copy cost. C/F/A affect the explicit helper
bounds above; P identifies component spans.

## What remains unresolved before a numeric production allowance

1. Obtain K from the **exact final prepared canonical closure**, after scalar
   append changes and any fuel ABI changes. No such artifact was found; the
   runtime owner also reported none known. The existing scale log only has
   top-frame steps and backend starts. Do not claim this document supplies K.
2. Bind the workload proof to the prepared generation / exact shipped source
   identities. Producer safety validation permits safe edited policies; it
   does not prove these loop bounds for an arbitrary additional loop. Edited
   policies may still receive a finite allowance and exhaust normally, but
   this formula cannot promise their completion without a new bound review.
3. Independently verify that request accounting charges the canonical checks
   in VM, actual compiled and generated backends, including every helper and
   the post-Publish tail. A mismatch is an engine accounting defect, not a
   reason to inflate coefficients.
4. Run only a separately admitted finite coefficient/overflow fixture. No
   measurement is admitted or performed by this artifact.

A narrow census proposal is one fixed source-collection preparation containing
the two exact source files and their existing fixed event declarations, then
one read-only traversal of the two modules and reachable canonical bodies:
print body identity, source hash, static BF_CHECK count, calls and the checked
K sum. It creates no program/world/mount and invokes no controller or library
function at runtime. Existing fixtures document 22 named-constant evaluations
for the single library generation; those preparation evaluations must be
listed separately. The expected source definitions are eight controller
handlers plus twelve library functions, and the reachable request closure
excludes six unrelated handlers. Verify these expectations rather than using
them as unchecked traversal limits. A test-owned engine driver may use
`execution/definition.h`; an SDK consumer must not reach private headers.
The eventual census source and exact preparation route still need independent
admission before the first shared-60-second measurement. No C draft was added
because that preparation route/ownership is not yet fixed.

For later arithmetic coverage, keep a fixed table of empty/flat/chain facts,
First values INT_MIN/0/1/2/INT_MAX, nonpositive and maximal Count, collapsed
Refresh versus Reveal's forced-open state, repeated prototype spans, disjoint
fields, duplicate attachments, repeated-prefix text, q=0/q>n, and paths at
0/1/127/128 bytes with slash/dot extrema. Exercise checked add/multiply overflow
with synthetic dimensions without allocating a corresponding scene. These
are proposed finite categories, not an authored or run test workload.
