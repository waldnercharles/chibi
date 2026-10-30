# Inferred shared captures: concrete language proposal

Status: **proposal, not implementation or final language approval**. Prepared
15 October 2026 against engine main
`dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce`.

## Recommendation and decision boundary

Use inferred **shared lexical bindings**: the enclosing execution and every
closure over one mutable declaration instance read and write the same variable.
Returning a closure preserves that binding automatically. Ordinary value
assignment still copies values under the existing ownership rules. Authors do
not select allocators, allocate environments, or free captures.

The owner approved inferred shared mutable bindings on 15 October 2026 and
required proper capture-cycle reclamation, allowing it in a separate
implementation PR. No collector or reclamation algorithm was selected.
The owner also requested optional `fn`, an optional arrow form and inferred
results. The exact grammar, canonicalization, escape admission, concurrency and
owner/reload behavior remain proposals requiring scoped decisions.

Two design boundaries remain to settle; cycle completion is a required
separate work item:

1. **Requested syntax direction:** support `fn(value: int) { return ...; }`,
   `(value: int) { return ...; }` and `(value: int) => expression`, with inferred
   results and an explicit `->` fallback. Recommend fn-free canonical expressions
   through checked semantic minimization; keep `fn` in bodyless callable types.
   The exact grammar, extra arrow-block combinations and combined Format policy
   below remain recommendations, not parser implementation or blanket approval.
   Contextual parameter inference is separate and unselected.
2. **Useful lifetime:** recommend synchronous callable bodies, including safe
   returned closures and values held by an existing suspended task, confined to
   one execution owner/generation. Invalidate on successful `.chibi` replacement
   or owner removal; recreate registrations. Native retained callbacks remain a
   separately admitted concrete consumer. Preserve returned scalar/string/array
   captures, callable copy/drop and `MakeCounter`'s callable-bearing named-result
   tuple. Initially exclude captured explicit refs (independently invalidated
   targets), task-handle captures (additional task/result ownership edges), and
   general callable-bearing containers/records until their copy, write, graph
   traversal and teardown paths are covered. Ordinary array captures remain
   supported. No task-producing lambda, cross-thread transfer or direct host
   reentry in this first contract. These admission limits are proposed, not
   implied by the approval of shared mutation.
3. **Cycle completion, separate PR:** support capture cycles and reclaim them
   correctly without manual cycle breaking or waiting for owner destruction.
   Atomic cycle rejection may be an initial safety limitation, never the finished
   contract or grounds for closing this requirement. The reclamation design
   remains to be specified and reviewed; no collector has been selected.

Everything else below makes the choices and required work precise. It does not
ask the owner to choose stack versus heap or a refcount algorithm. The cycle
completion direction does not settle the pending syntax or lifetime choices.

## Existing foundations and limits

At the pinned source, [grammar.js][grammar] supports `Name :: (args) -> Result
{ ... }` with optional named `fn`, but no lambda expression or callable value
type. [semantic_expression.c][names] explicitly diagnoses enclosing-local
access by a nested named function. This proposal leaves that named-function
rule unchanged; a new expression is what captures.

[program_tasks.c][boxes] already reuses a box for explicit task references to
the same attached local and redirects it to retained storage at frame retirement.
[intrinsics.c][writes] stages detached managed-root writes and compacts retained
payloads. These are valuable existing ownership mechanisms, not proof of a
lexical closure type, escape analysis, cycle handling or callable ABI. Neither
creating a separate snapshot per lambda nor merely adding a function pointer
would implement this proposal.

## 1. Optional `fn`, arrow bodies and inferred results

All lambda expressions and callable types below are **proposed, not accepted by
the current parser or executed fixtures**. Ordinary declarations, calls,
parentheses, control flow and named-function result inference are existing
foundations. The owner's latest request is to make `fn` optional, also offer an
arrow form, and infer results where possible. These are complementary forms,
not an unresolved choice of only one spelling.

### The same capture in three useful forms

Each line is an alternative spelling of the same declaration; do not place all
three declarations in one scope:

```chibi
keep := fn(value: int) { return value > limit; }
keep := (value: int) { return value > limit; }
keep := (value: int) => value > limit
```

All three infer one boolean result and capture the same mutable `limit` binding.
None implies a snapshot, allocation strategy, purity or a different lifetime.
For new source, recommend the fn-free block for statements and the arrow for a
single result expression:

```chibi
count: int = 0
increment := () {
    count += 1
    return count
}
read := () => count
count = 10
increment() // 11
read()      // 11; outer count is also 11
```

An explicit result remains available without bringing `fn` back:

```chibi
keep := (value: int) -> bool => value > limit
increment := () -> int { count += 1; return count; }
```

### Recommended grammar boundary, still requiring implementation review

Use this schematic contract, not a patch to the Tree-sitter grammar:

```text
lambda-expression := [fn] parameters [-> results] lambda-body
lambda-body       := block | => expression | => block
callable-type     := fn parameters [-> results]
```

Recommend allowing `fn` on either body form and an arrow before a block as well,
so optional spelling does not create a new semantic distinction:

```chibi
keep := fn(value: int) => value > limit
keep := (value: int) => { return value > limit; }
keep := fn(value: int) => { return value > limit; }
```

These additional combinations are recommendations, not separate owner approvals.
Their cost is parser/CST, diagnostics and tooling coverage; they require no new
capture semantics or runtime representation. If the first implementation admits
only the three requested forms, disclose that narrower surface rather than
silently treating the other combinations as equivalent accepted syntax.

Keep parentheses even for one parameter, and explicit input types in this first
proposal: `(value: int)`, or `()` when there are none. Contextual parameter
inference, `(value) => ...`, unparenthesized `value => ...`, generic lambda values
and extra signature modifiers are not selected by result inference. A later
contextual rule must explain where the expected input type comes from and reject
an unresolved input rather than silently making it dynamic. Do not change
existing named-function parameter rules as a side effect.

A block uses ordinary callable-body statement and return rules. An expression
body has the behavior of a single `return expression` in that body, with the
expression's source range preserved. It does not introduce assignment
expressions, tuple expressions or implicit unpacking of multiple call results.
Use a block for a no-result statement or labelled multiple results. If an arrow
is immediately followed by `{`, it starts a block; returning an existing struct
expression requires parentheses, `() => ({ field: value })`, and that expression
must separately satisfy current aggregate typing rules. This convention avoids
using a colon inside braces to guess whether the author meant statements or a
value. `->` continues to introduce results; `=>` introduces a body, never a type.

Arrow bodies have lower precedence than existing binary operators and extend to
the surrounding argument comma, closing delimiter or statement boundary.
`(value: int) => value + 1 > limit` returns the comparison. Nested arrows associate
right: `(offset: int) => (value: int) => value + offset` returns a new closure
subject to the same proposed escape rules. Parenthesize the whole lambda before
immediate invocation, `((value: int) => value + 1)(3)`, or before using it as the
operand of an outer operator. This fits the existing parenthesized call-target
shape without making the trailing `(3)` part of the body accidentally.

### Result inference and callable types are different

Lambda **expressions** preserve existing function result inference: an omitted
`->` infers results from the first value-return, and later returns must satisfy
those result types under the existing conversion rules. A body with no
value-return has no results. Named-result inference, return consistency and
explicit-result requirements for recursive inference cycles remain unchanged.
The [current function contract][result-inference] and [analyzer][result-analysis]
supply the rule; capture safety is no reason to require every lambda to spell its
results. An expression body inherits the same single-return rules, rather than
using a new best-common-type or union inference algorithm.

```chibi
read := () { return count; }       // inferred result: int
step := () { count += 1; }         // no results
pair := () { return(Count: count, Ready: true); } // named results
```

Recursive or mutually recursive inference that cannot resolve the result uses
an explicit `->` result or a complete predeclared callable type. That fallback
solves a type dependency, not a capture-initialization or ownership cycle. The
[recursive witness below](#recursive-capture-witness-for-the-completed-contract)
initializes the binding first and spells `-> int`; proper reclamation still
belongs to the separately required cycle-support PR.

Callable **types** have no body from which to infer. Recommend retaining `fn` in
type positions to mark this clearly: standalone `fn()` means no results, while a
nonvoid type must spell its results, such as `fn() -> int`. There is no bodyless
`=>` type spelling and no type-position omission of `fn` in this recommendation.
Omitting `fn` in a lambda expression does not settle a second type grammar.

```chibi
Apply :: (value: int, transform: fn(value: int) -> int) -> int {
    return transform(value);
}

// Within a calling function:
limit: int = 3
keep: fn(value: int) -> bool = (value: int) => value > limit
```

Parameter labels document the type; they are not distinct callable type
identities. Calls through callable values are positional initially. Direct named
function calls keep their current argument-label rules. `:=` infers the callable
value type from its expression; an explicit annotation checks compatibility.
Matching `fn(...) -> ...` alone does not prove purity, nonescape or thread safety.
Nested callable result types bind to the right: `fn() -> fn(value: int) -> int`
is a no-argument callable returning another callable. Named-result parentheses,
as in the counter below, delimit multiple callable results explicitly.

### Audit against the current grammar

The pinned [grammar][grammar] has no lambda-expression or callable-type rule.
The following are source-backed ambiguity checks and proposed resolutions, not
claims of a generated conflict-free parser:

- **Ordinary grouping and control headers:** `parenthesized_expression` contains
  one expression. `(value)` remains grouping; `if (ready) { ... }` and
  `while (ready) { ... }` remain control flow. A fn-free block lambda is recognized
  from `()` or its explicitly typed parameter header plus the required body, not
  a heuristic that every `(...) {` starts a closure. `(value) { ... }` is not an
  admitted untyped lambda in this proposal. Typed headers, results and empty
  parameters still need grammar conflict/error-recovery cases.
- **Call and return lists:** `argument_list` already admits named and typed
  arguments and conflicts with grouping. `Use(value: input)` stays a named
  argument, and `Use(value: int = input)` stays a typed argument. A lambda argument
  has its own parentheses: `Use((value: int) => value + 1)`. Likewise,
  `return(Count: count, Ready: true)` remains labelled results, not a lambda or a
  new general tuple expression. Test `return () => count` and block-returned
  lambdas so a return-list parse does not consume their headers.
- **Named declarations:** `Name :: [pure] [fn] (...) { ... }` remains a named
  function with its existing capture restriction. A new lambda rule must not
  reinterpret that declaration as `constant_declaration` containing a closure.
  Use `name := (...) { ... }` for a local capturing value. This request does not
  add arrow bodies to named declarations or relax `::`'s constant restriction;
  diagnose a named arrow declaration rather than silently switching categories.
- **Braces:** current `struct_expression`, statement blocks and callable bodies
  are distinct CST nodes. After a lambda header or `=>`, bare braces are its
  callable body. Parenthesized braces in an expression body stay a struct
  expression. Preserve nested declaration, `return`, comment and source-map
  ownership; do not classify a body from its first field/statement alone.
- **Operators and nesting:** add `=>` as one token; do not reinterpret existing
  `=`, `>=` or `->`. Test comparisons inside bodies, lambda arguments before a
  comma, nested arrows, immediately invoked parenthesized lambdas and nested
  callable result types. A returned multi-result call retains today's explicit
  labelled-return requirement; punctuation must not conceal unpacking.
- **Newlines and incomplete edits:** the [terminator scanner][terminators]
  considers parser validity, newlines, comments and continuation characters.
  Existing checks for `=` and `(` do not establish correct lambda boundaries.
  Add line/comment breaks around headers, `->`, `=>`, bodies and enclosing commas;
  preserve explicit semicolon boundaries and the next statement after a lambda.
  Missing `)`, `=>` operands or closing braces must stay local invalid drafts
  without swallowing the next declaration or publishing partial edits.

### Canonical source and the two formatting passes

Recommend **fn-free canonical expressions**, consistent with removing redundant
optional keywords. New single-result examples use `(value: int) => expression`;
new statement bodies use `(value: int) { ... }`. Use one space after `:`, around
`->` and `=>`, and before the opening block brace; keep the opening brace on the
header line and apply existing indentation/width settings inside it. Keep an
explicit result annotation when required or deliberately supplied.

The current [layout SDK][layout] is whitespace-only and checks exact CST/leaf
preservation. It cannot remove `fn` by changing that contract. Extend the separate
semantic minimizer with a checked lambda-keyword rewrite, then let a combined
Format/canonicalize command run minimization followed by layout. That is the
recommended path to automatic optional-keyword removal; preserving tokens in the
layout pass is not a decision to preserve redundant `fn` permanently.

Before removing an expression's `fn`, reparse/reanalyze and verify the same
callable signature, resolved captures, binding identities, result conversion,
effects and ownership behavior. Preserve every comment and compose the semantic
edit map with the layout map for one editor undo and correct source/debug
positions. If removing the token would lose a comment, create a parsing ambiguity
or exceed the available equivalence proof, retain it with a truthful diagnostic.
Never remove the `fn` from the proposed callable **type**, where it is required.
Do not infer a named-function rejection policy from this lambda-only proposal.

No automatic block-to-arrow rewrite is part of keyword removal. Even a body that
looks like `return expression` can have comments, labelled results or source-map
obligations. Preserve the chosen body form by default; any later conversion needs
its own equivalence/comment checks. For an accepted `=> { ... }` form, preserve
that arrow too unless a separately checked canonical rewrite removes it. The
combined command's exact options/default and additional arrow-block admission
remain scoped tooling/syntax recommendations, not implemented behavior.

Callables remain opaque runtime values: no pointer conversion, serialization,
structural equality or new C function-pointer ABI is implied. Copying a callable
retains the same environment/bindings; it does not clone their current values.
Do not add capture lists, a `shared` capture keyword, borrowed/owned surface types
or a new `lambda` keyword; system `shared` storage has a different meaning.

## 2. Binding identity, initialization and scope

- Resolve captures to lexical symbol identities, never name strings or a scan
  of an entire caller frame. A mutable local is shared even if this particular
  closure only reads it. Outer writes, sibling writes and explicit aliases to
  that same local must all reach it.
- Each execution of a declaration creates a fresh binding. Two calls of a
  factory are independent. Reentering a block or executing its declaration on
  another loop iteration creates another binding; an escaping closure keeps
  the old one. Source symbol ID alone is not a runtime declaration instance.
- Range/collection loop variables have one binding per iteration; retain their
  existing nonwritable status. A variable declared outside the loop is one
  shared binding, not a copy per iteration. Existing [loop analysis][scope]
  marks iteration variables nonwritable; new closure lifetime behavior must be
  tested rather than inferred from stack-slot reuse.
- A local/parameter/result capture must be definitely initialized when the
  lambda expression executes. No forward or self capture of an uninitialized
  local, even if the closure would be invoked later. Parameters start
  initialized; named results need an earlier definite assignment. Capture does
  not run a local initializer early or again. If existing name resolution
  resolves a later shadowing declaration, diagnose the unavailable capture;
  do not silently fall back to an outer declaration of the same spelling.
- Shadowing and nested transitive capture preserve the resolved declaration.
  An intermediate lambda can relay an outer binding to a lambda it creates;
  it must not substitute a fresh copy. A lambda parameter/local of the same name
  shadows normally. Capture creation executes no user getter or capture
  expression because this proposal has no capture-list expressions.
- Constants remain nonwritable. Persistent state and entity `self` retain their
  existing resolved meanings and ownership; neither becomes a lexical local
  or an implicit whole-object capture. This proposal does not extend the
  unresolved library/shared receiver table or make locals persistent between
  independent calls.

Minimal negative example (proposed diagnostic: capture before initialization):

```chibi
read := fn() -> int { return later; }
later: int = 3
```

This is an explicit new lambda admission rule. The existing declaration pass
predeclares callable-body locals; that source fact is not a definite-initialization
proof and must not accidentally admit this case.

## 3. Distinguishing examples

All examples containing lambda expressions or callable types are proposed source,
not passing fixtures. Calls below show expected results, not executable assertion syntax.

### Two closures, outer writes, and return

```chibi
MakeCounter :: () -> (inc: fn() -> int, get: fn() -> int) {
    count: int = 0
    increment := fn() -> int { count += 1; return count; }
    read := fn() -> int { return count; }
    count = 10
    increment() // 11; an outer read of count is now also 11
    return (increment, read);
}

// Within a calling function:
counter := MakeCounter()
counter.get() // 11 after MakeCounter's frame has returned
counter.inc() // 12
counter.get() // 12, not 0, 10, or 11
other := MakeCounter()
other.get()   // 11; independent of counter
again := counter.inc
again()       // 13; copying a callable does not copy count
counter.get() // 13
```

Counter creation itself increments once only to demonstrate the outer-visible
write. A fixture must inspect `count == 11` inside the factory too; an after-return
result alone would miss a split between outer storage and the closure cells.

### Synchronous filter predicate with explicit input and inferred result

`CountMatches` is a proposed authored helper, not a claim that a native `filter`
or `sort` callback API already exists:

```chibi
CountMatches :: (values: int[], keep: fn(value: int) -> bool) -> int {
    count: int = 0
    for value in values { if keep(value) { count += 1; } }
    return count;
}

// Within a calling function:
limit: int = 3
matching := CountMatches([2, 5, 8], (value: int) => value > limit) // 2
```

The lambda parameter `value: int` is explicit; its boolean result is inferred
from `value > limit`. The bodyless `keep` parameter type spells `-> bool` because
it has no body from which to infer. `CountMatches` invokes the callback during
its call and never stores it; implementation still has to verify transitive
synchronous/nonescape and effect requirements. No contextual parameter inference
or production collection API is silently introduced.

### Shared array binding versus independent array value

```chibi
items: int[] = [1, 2]
saved := items
read := fn() -> int { return items[0]; }
write := fn(value: int) { items[0] = value; }
write(7)
read()         // 7; outer items[0] is also 7
// saved[0] is still 1
items = [9, 10]
read()         // 9; captures the binding, not the old array payload
saved[0] = 4
read()         // still 9
```

Current [managed snapshots][values] remain the value boundary. A returned array
or an ordinary value copied out of the captured cell is independently owned as
before. Sharing the `items` variable does not turn `saved := items` into a
reference assignment. Strings, admitted reflected structs and nested managed
values obey the same distinction. Component/resource handles remain identities,
not resource snapshots. Capturing only `items[0]` internally is invalid unless
replacing `items` and all relevant aliases remain observationally equivalent.

### Loop bindings

A C fixture should retain three callbacks created by
`for i in 0..2 { callback := fn() -> int { return i; } ... }` and later observe
`0, 1, 2`. The fixture's retention operation is a test harness capability, not
an invented production API. A second fixture increments `count` declared
outside that loop and checks all three callbacks read its final value `3`.
This separates iteration identity from an accidental copy-everything strategy.

## 4. Nonescape, escape and ownership

A closure's meaning does not depend on compiler placement:

- A verified synchronous consumer may use a stack environment or direct/inlined
  access. Its whole transitive call path must neither retain, return, enqueue
  nor suspend with that value; opaque/native consumers are not assumed safe.
  Mutation is allowed by shared-binding semantics, subject to the particular
  consumer's effect contract. A sort comparator can still require no mutation
  that changes its ordering relation during a sort.
- Returning, keeping in longer-lived storage, or carrying a closure through an
  enclosing task's suspension requires an owned environment and stable shared
  cells before the shorter lifetime ends. The compiler may allocate eagerly or
  promote; all holders and outer aliases must switch together. Copying the
  payload on promotion is permissible storage relocation only if it preserves
  one binding, prior independent snapshots and visible effects/failures.
- Creation of two closures over one local must reuse that logical cell, including
  when one escapes first and another is created later. Release a binding only
  when its declaring execution and all retaining holders are finished. Failure,
  return, scope exit, cancellation and owner teardown must all release ownership.
- Capture only required bindings. Do not retain a whole caller activation or its
  large unrelated values merely for convenience. Grouping/splitting environments
  is an optimization subject to lifetime/accounting checks, not a new language
  category the author must manage.

The recommended complete owned-local/result slice includes ordinary local and
parameter captures, including scalar, string and array values, returned closures,
callable copy/drop, and the callable-bearing **named-result tuple** used by
`MakeCounter`. Its tuple representation and all relevant ownership paths are
required scope; it must not be accidentally excluded as a general container.

General user-authored containers/records containing callable values are a separate
admission: require every owning copy, write, graph traversal and teardown path
to understand them; until then diagnose that type use. This restriction concerns
callable-bearing contents, not ordinary arrays or strings captured by a returned
closure. No new authored-record syntax, dict, registry or generic callback facade
is required by these examples.

Captured explicit `ref` values (including ref parameters, collection-element
refs and ECS component refs) are initially rejected. Sharing a compiler-managed
local binding is not permission to retain an arbitrary alias with another
lifetime. Ordinary values obtained by reading a ref may be captured; that read
uses current value rules. Supporting captured refs later needs checked path,
structural-invalidation and suspension rules, not merely a box pointer.

Task handles also need explicit admission: they have owner-local task/result
roots, not just integer identity. Initially reject task-valued captures until
that ownership graph participates in cycle/lifetime checking. An enclosing task
retaining a closure over an ordinary scalar is still supported; a closure
retaining the task back through its handle is a different ownership edge.

## 5. Required cycle support, staged separately

The owner's latest reply requires handling capture cycles properly and permits
a separate implementation PR. The end state must admit cycles in the supported
closure graph and automatically reclaim unreachable cycles without manual frees,
manual edge breaking or waiting for owner destruction. This does not select a
collector or broaden the pending callable-container/ref/task admission rules.

Existing boxes have holder counts; the inspected code does not provide a general
cycle collector for lexical closure graphs. Reference counting alone cannot
reclaim `cell -> closure -> cell`. Owner destruction as the only cleanup is
insufficient: a long-lived editor owner can repeatedly replace callbacks. The
detached-payload compaction fix does not collect such cycles.

### Recursive capture witness for the completed contract

```chibi
// Within a calling function; proposed syntax, not executed:
factorial := fn(n: int) -> int { return 1; }
factorial = fn(n: int) -> int {
    if n == 0 { return 1; }
    return n * factorial(n - 1);
}
factorial(5) // 120 once the cycle-support contract is implemented
```

The binding is initialized before the recursive closure captures it, preserving
the no-uninitialized-capture rule. The recursive result type is explicit. The
assignment creates the ownership cycle `factorial cell -> closure -> factorial
cell`. After the last outside holder leaves scope, correct reclamation must not
require an explicit reset of `factorial`. This example requires both selected
shared binding identity and the separately required cycle implementation.

### Initial limitation if cycle support lands later

An initial slice that cannot safely own cycles must reject their creation with
a precise diagnostic; a dynamic check must fail before publishing the store,
preserve its previous target and release the unpublished candidate. It must not
accept a leak or silently snapshot the binding. Such rejection is temporary and
cannot pass the completed cycle-support acceptance gate.

One possible interim mechanism checks whether the candidate's owned closure/cell
graph reaches the destination cell. It must cover indirect aliases and every
admitted callable-bearing write, regardless of stack/owned placement; budget
exhaustion fails before the store. Earlier unrelated expression effects are not
rolled back. Unverified native mutation or untraced managed shapes remain
unadmitted. This is a proposed staging mechanism, not existing code, a selected
final collector or a measured cheap operation.

An ordinary acyclic wrapper need not wait for the cycle PR:

```chibi
original := fn() -> int { return 1; }
wrapped := fn() -> int { return original() + 1; }
wrapped() // 2
// original = wrapped creates a cycle: temporarily rejected until cycle support
```

The separate PR must specify roots, traversal of every supported ownership edge,
safe reclamation boundaries, work/budget bounds, failure behavior and interactions
with active calls, cancellation and generation invalidation. It must demonstrate
that unreachable cycles are reclaimed while reachable closures and independent
managed snapshots remain valid. General tracing, cycle-aware reference counting
or another sound mechanism are implementation alternatives, not owner selections.
Do not add a global registry or require authored allocation/free merely because
cycles need a solution. A broad ban on callable-valued captures is also not the
finished contract. Keep this requirement open until its own reviewed, admitted
implementation and reclamation evidence are complete.

## 6. Tasks, concurrency, entities and `.chibi` replacement

The explained design direction is internal escape, lifetime, alias and race
analysis, without Rust lifetime annotations or Rust-style exclusive borrowing
for ordinary shared captures. This is not approval of a particular reclamation
strategy. A safety check may reject an unsupported operation; it must not
quietly change a captured mutable binding into an immutable snapshot.

**Tasks and concurrency.** Lambda bodies initially run synchronously: no `yield`,
`await`, task creation or transitive suspension-capable callee. A synchronous
closure held by an already supported enclosing task may survive that task's
`yield` using owned storage. On resume it sees writes made since suspension;
there is no snapshot-at-yield rule. Task cancellation releases its holders;
other same-owner holders can remain valid. This does not change the existing
rule that dropping a task handle does not cancel the task.

Confine closures and their shared environments to one execution owner and its
serial scheduler context. Reject cross-owner transfer and cross-worker invocation;
no implicit locks or atomic `count += 1`. A callback with shared captures is
ineligible for parallel system-row execution until alias/effect analysis proves
the required isolation. Unknown effects use the existing conservative gate.
The noescape check is not a substitute for this gate. Cooperative task
interleaving is observable, even without simultaneous threads. Direct synchronous
native-to-authored reentry remains disallowed by the current host contract.

**Owner versus resource.** A captured local can outlive its declaring frame,
but that says nothing about a captured entity/service handle's target. A copied
entity ID does not keep the entity alive. Revalidate owner/entity/resource
generation and acquire current component storage when used; never retain an ECS
address across structural changes or suspension. A resource disappearing while
the execution owner survives produces the existing checked capability failure.
No closure creates a new meaning for `self` or a new owning entity reference.

Recommend that every owned closure belongs to the current execution instance
and code generation, even if its captures are only integers. Library-defined
code does not thereby acquire a global lifetime; use the owner associated with
its invocation. Persistent library/system state remains separately resolved and
must not be copied into the lexical environment. Removal of the execution owner
invalidates all its closures/registrations; a stale handle cannot resurrect it.
At the safe teardown boundary release their code/environment/managed roots;
externally retained stale tokens may keep only the minimal invalid-handle
bookkeeping, not an indefinitely pinned old payload or generation.
This is simpler than allowing pure-data closures to cross owners, but it is a
visible restriction and part of decision 2.

**Authored replacement.** On a successful `.chibi` publication that replaces the
owning execution generation, stop old callbacks at a scheduler-safe boundary,
cancel its suspended work, invalidate queued registrations and rebuild them
from the newly applied source. Direct invocation of a stale closure fails before
entering its body; a stale queued callback is discarded with the registration's
existing cancellation/invalidation outcome. Do not retarget an old environment
to new code based only on matching names or a layout guess. Do not serialize
closure environments into project files/checkpoints or migrate them as ordinary
persistent state.

Unrelated owner publications need not invalidate this owner's closures. A
changed imported dependency that causes this owner to be replaced does.
Successful publication may reset local callback state, while ordinary compatible
persistent state still follows the existing matcher. A failed draft/compile/stage
keeps the applied generation and its callbacks unchanged. [Existing replacement][reload]
already retires old tasks rather than waiting for them; this recommendation
extends that lifetime boundary to new closure values. It does not claim current
code already manages callback registrations.

A small illustrative trace starts from a fresh `MakeCounter()` result: its
`get()` returns `11`; one task holding it yields; a sibling `inc()`
returns `12`; the resumed task's `get()` returns `12`. A failed source stage
leaves `get()` returning `12`. Successful replacement of the owning generation
makes the old callable fail before body effects. This is a proposed oracle,
not an executed or admitted test.

Pinning old code/environments would preserve old callbacks across replacement,
but risks accumulating generations and retaining obsolete behavior. It is an
alternative requiring a concrete consumer, not the default here. Native C
application hot reload was already removed; none of this reinstates it or
removes `.chibi` live publication.

## 7. Costs and observability

- Shared capture introduces aliasing. A read-only lambda cannot fold its capture
  to a creation-time constant if another writer can change that binding.
- Nonescaping direct calls may avoid environment allocations; escaping captures
  may allocate cells/environments, retain code and managed roots, and add indirect
  access/dispatch and retain/release work. No fixed allocation count is promised.
- Moving ownership at frame exit may snapshot managed payloads under existing
  storage rules. Repeated aggregate replacement may allocate/copy; preserving
  prior independent snapshots still matters. Count payload copying separately
  from closure/environment allocation.
- Any interim cycle prevention adds checks on relevant callable-bearing writes.
  Completed cycle support also requires root/traversal/reclamation work, whose
  costs depend on the unselected mechanism. Both need explicit work budgets;
  neither rejection nor collection is assumed free.
- Tooling should list resolved captures, writability, owner, proven nonescape or
  owned lifetime, and retained-size/copy evidence. Static estimates are not
  timings. Debug inspection must not change semantics or silently choose a
  different backend and then claim compiled-path evidence.

The current Studio asset comparator/filter is a useful first synchronous
consumer: retain its kind/name comparison and filter results, eliminate the
hidden comparator global through explicit environment lowering, and use checked
index-generation access or supported owned values rather than a captured raw
`reference_index *`. A retained UI callback requires a separately named consumer
and registration lifecycle. Neither consumer supplies evidence of a speedup
until measured against the actual native workload.

## 8. C/pico_unit regression plan

No parser generation, build, runtime test, benchmark or production change ran for
this proposal. The following cases are **planned, not implemented or passed**.
Use the existing `test/language` executable and a focused `--closures` dispatcher
if the existing owner selector cannot keep this group independently meaningful;
no new linked executable per probe, Python/Node test suite or new supervisor.
A C fixture may drive the real Wasm backend through the existing host harness;
that does not turn the assertions into JavaScript tests.

1. **Syntax, inference and canonicalization:** all three requested forms produce
   the same signature/captures/results; each additionally admitted combination
   needs positive coverage and every unadmitted form a local diagnostic. Cover
   each ambiguity boundary listed in section 1: grouping/control headers,
   named/typed call arguments and return lists, `::` named declarations,
   aggregate versus block braces, precedence/nested arrows, nested callable types,
   comments/newlines and incomplete edits. Keep named-function capture rejection.
   Test inferred-int and inferred-bool expressions, no-result blocks, labelled
   results, incompatible later returns, contextual-input rejection, explicit
   recursive-result fallback and standalone `fn()` versus `fn() -> int` types.
   Verify layout-only CST preservation separately from canonical removal of an
   optional expression `fn`; required type markers remain. Check binding/effect
   equivalence, comment retention, idempotence, composed source maps and one undo.
   A block must not silently become an arrow expression. Forward/self/uninitialized
   capture, shadowing and nonwritable captures still need negative cases.
2. **One binding:** exact outer/sibling/after-return counter results above; two
   independent factories; closure copy/drop; nested transitive capture; creation
   of another sibling after the first escapes; outer explicit-ref alias sees
   the same permitted scalar local.
3. **Declaration instances:** three loop/block executions retain distinct
   bindings; a single outside-loop mutable binding remains shared; reassignment
   does not create a new binding. Test early return and failed construction.
4. **Value boundaries:** scalar/string/array/admitted nested reflected values;
   array example above, returned independent snapshots, overwrite with a much
   smaller payload, and invalid descendant paths after root replacement.
5. **Placement equivalence:** one source workload through verified nonescaping
   consumption and an owned returned-closure route produces identical values,
   order and failures. Unknown retention, captured refs, task-producing bodies,
   host reentry and wrong-owner/thread invocation are negative cases.
6. **Cycles and cleanup:** the required separate PR must execute the recursive
   witness (`factorial(5) == 120`), mutual and indirect capture cycles, and reclaim
   unreachable graphs after their last outside holder leaves scope while the
   owner remains alive. Reachable siblings and independent snapshots must stay
   valid. Cover every admitted callable-bearing edge/store route, budget/failure
   paths, cancellation and generation retirement. Repeated create/drop and
   overwrite return to the same accounted live state. If an earlier slice uses
   rejection, separately check target preservation and unpublished-candidate
   release; those temporary negative cases do not satisfy final cycle support.
7. **Suspension and lifetime:** one existing task holds a synchronous closure
   across a synthetic-clock yield; another same-owner invocation changes the
   cell; resumed read sees the change. Cancel/drop/owner removal release roots.
   Entity/resource removal fails checked access without dangling pointers.
8. **Publication:** failed stage preserves applied closures; successful owning
   generation replacement invalidates old ones before body effects, cancels
   suspended work, discards stale queued work once, recreates approved
   registrations and preserves compatible ordinary persistent state.
9. **Consumer parity:** fixed asset-kind/name/filter inputs and index replacement
   reproduce the native comparator/filter decisions; a separately admitted
   small retained-callback fixture proves registration cleanup, not a mock
   declaration that all Studio integration now works.

Keep fixtures in memory with fixed small bounds (for example two owners, three
loop iterations, at most 32 repeated replacements and one 8 KiB managed value),
no rendering, filesystem/network, sleeps, project clones or long soak. These are
illustrative workload bounds proposed for review, not fixed cases, measured
runtimes or a new product cap. Before running them, instantiate each case
with fixed input sizes, graph bounds, budget/exhaustion inputs and a precise
oracle. Attach each case to its source contract and exact backend; run reference
bytecode, actual SLJIT and actual Wasm. A native build of a Wasm-named selector is not Wasm
execution. Add necessary prepared-schema compatibility/failure cases when the
actual callable encoding is designed; unknown/incompatible layouts must reject
before publication, and source effects/escape proofs must be reverified.

Independently review the selector's workload and oracle using the
[testing instructions](../testing.md), execute the selected checks, and keep
build/configure costs separate.
Unavailable affected-backend/publication coverage blocks a completion claim.
Existing capture-compaction cases in `test/language/capture.c`, reached from
`--program`, remain required regression evidence, not substitutes for these new
lexical cases. Independent exact-head review and applicable builds/checks still
apply before integration.

[grammar]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/language/grammar.js#L279-L312
[names]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/language/semantic_expression.c#L540-L565
[scope]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/language/semantic_statement.c#L833-L890
[boxes]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/execution/program_tasks.c#L148-L230
[writes]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/execution/intrinsics.c#L575-L610
[values]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/types/array.c#L150-L259
[reload]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/execution/program.c#L2553-L2828
[result-inference]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/docs/code-language.md#L483-L486
[result-analysis]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/language/semantic_statement.c#L460-L505

[terminators]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/src/language/src/scanner.c#L33-L111
[layout]: https://github.com/waldnercharles/chibi/blob/dd0b2048116dc8f1ecc17603b19ac8a3c4bd84ce/docs/source-formatting.md
