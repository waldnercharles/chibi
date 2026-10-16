# Asset and behavior source, revision 4

Executable behavior source uses `.chibi`. Components, prototypes and relationships
also have authored `.chibi` declarations; scenes and project configuration use KDL.
The shared C frontend retains syntax snapshots and resolves typed semantic
records. The compiler lowers those records directly into verified execution
modules; visual editors consume their authoring projections.

The [versioned contract snapshot](language-contracts/v1.md) pins source evidence,
existing C conformance cases and unresolved migration semantics separately at its
recorded revision, before the version-25 retained-binding migration. Its source
audit is not fresh runtime or cross-backend validation.

## Declarative documents

Project manifests are `project.kdl`, version 26. Scenes, KDL prototypes, event
declarations, models, shapes, materials, fields, shader libraries, scenarios and
editor configuration use KDL 2. Behaviors, behavior libraries and systems use
readable `.chibi` source. RML and RCSS remain separate markup formats.

```kdl
version 26
name "My Game"
content "content"
start_scene "scenes/Main.kdl"
```

KDL documents contain named fields, objects and arrays. Objects use named child
fields or properties. `(array)` keeps arrays distinct from scalars and objects,
including empty and one-item arrays; `(object)` marks an empty object. Boolean
and null literals are `#true`, `#false` and `#null`. The shared value codec keeps
native numeric and reflected value meanings unchanged.

```kdl
version 1
name "Main"
(array)entities {
    - {
        name "Ship"
        prototype "Ship"
        components {
            "Projectile" age=0.0 lifetime=2.0
        }
    }
}
```

Reflected string fields containing shader/material parameter objects or mesh
part arrays also use KDL value syntax. Object payloads contain their named
fields, such as `Radius 25`; array payloads use `(array)-` with values or item
nodes. Escape their text when embedding them in another KDL document. These
payloads are declarative configuration, not executable behavior source.

Authored asset declarations use the same name-first headers as executable source:
`Health :: component { ... }`, `Enemy :: prototype { ... }` and
`Targets :: relationship { ... }`. A composed prototype names ordered bases,
such as `Elite :: prototype(Enemy, Armored) { ... }`. See
[authored prototypes and relationships](runtime-authoring.md#authored-prototypes-and-relationships)
for sparse defaults, instance-local links, relationship traits and typed pair edits.

## Executable source

Semicolons are optional at the end of a line and before a closing brace.
Separate multiple statements on one line with semicolons. Parenthesized calls,
arrays and struct literals may span lines. An operator at the end or beginning
of a line continues the expression.

`chibi_ctl --format` writes the shortest spelling that analyzes to the same
program: it drops semicolons, argument labels before the first skipped
parameter, literal arguments equal to their defaults, inferable declaration and
argument types, redundant parentheses and casts, handler aliases (the payload is
`event`), empty `else` blocks, `-> ()` and `return()`, and writes `return(x)` as `return x`. Each rewrite is kept only
when the re-analyzed program is unchanged; source that does not analyze is only
re-indented. Parentheses that group `&&` under `||`, or one bitwise operator under another,
stay. Ranges are written `0..n - 1`.

Named call arguments use `Value: expression`. An explicit argument type includes
its value or source, for example `Value: float = amount` or
`Source: ref int from count`. A bare identifier after `:` is a value expression.

```code
FreeMotion :: system(position: Transform, velocity: Velocity)
    without Collider, Body
{
    position += velocity * dt
}
```

A file under `content/components` declares one component instead: its fields
and their types, ranges and units. Scripts check those units, and the engine
fields' units and `dt`'s seconds. Proven unit mismatches and malformed referenced
unit metadata are compile errors. See "Declare components" in
`runtime-authoring.md`.

A behavior, library or system is a readable program. Its declarations and
statements resolve into an owned typed semantic program. Direct lowering produces
an immutable verified execution module; backend code and live instances have
separate lifetimes. Source does not require graph node, pin or connection records.
Library calls resolve against the project catalog; native calls resolve against
registered engine capabilities. Unknown names and incompatible types are errors.

```code
Projectile :: behavior {
    elapsed := 0;
    lifetime := 3;
    on Tick(dt) {
        elapsed += dt;
        self.Projectile.Age += dt;
        if (elapsed >= lifetime) {
            destroy_entity(self);
        }
    }
}
```

Behavior-root declarations own persistent instance state. `elapsed := 0;` infers
`number`, backed by double precision, so subsequent assignments can contain fractions.
Boolean, string and homogeneous array defaults also infer their type. Empty arrays
and nominal values require an explicit type. `elapsed: number = 0;` spells out the
type; native storage types such as `float` and `int64` remain available when precision
or a reflected interface needs them. Formatting keeps a declared type unless inference produces the same storage.

An explicit retained binding puts its lifetime after the name and colon:

```chibi
visits: retain int = 0
elapsed: retain = 0
```

`retain` selects private persistent storage belonging to the enclosing behavior
attachment or system registration, including inside named functions. Calling a
helper does not create another retained owner. All matches of one system share
its retained values; a second registration has independent values. Imported
library state inherits the consuming owner and keeps its own module namespace.
`retain = value` infers the value's type with the same rules as `:=`.
Behavior and library root declarations such as `elapsed := 0` are also retained.
There is no `retain(system)` form.

An ordinary system-body declaration is local to its invocation. Each query match
has independent locals, which survive `await` and `yield` until the invocation
finishes. Use those locals for temporary coroutine progress, components for
longer-lived entity state, and retained bindings for private owner-wide state.
System-retained values cannot contain task handles; keep those handles local to
the invocation instead. Pass system invocation locals to helpers and handlers as
parameters; they cannot capture those locals.
Formatting may remove an explicit root `retain` when the analyzed program stays
the same; a callable's `retain` and system ownership remain meaningful.

Historical project upgrades preserve behavior retention and convert old
system-owned `shared` or `retain(system)` bindings to plain `retain`. Old per-match
system state cannot be converted by changing its spelling: migration stops with
a diagnostic so the author can put durable entity state in components or
temporary progress in coroutine locals. The project version remains 26 because
no checked-in project uses the affected system declarations. Hand-made projects
already at candidate version 26 must review system `retain` declarations manually
and remove any old `(system)` suffix. Rebuild prepared code after upgrading.
The old `state` and `shared` prefixes are no longer declaration syntax.
The `state.name` expression still selects a persistent binding when a local
shadows its name.

`::` defines a compile-time constant, function or union type. A constant such as
`BaseDamage :: 12` has no writable instance storage. Its initializer can use literals,
other constants and built-in operators, including constant arrays and reflected
structs. Ordinary procedures can also run in compile-time contexts when all their
inputs and operations are available there. Runtime dependencies are errors. The compiler evaluates
these expressions once when it builds the module. `:=` continues to infer a mutable
value: persistent state at program scope, a local inside an executable block.

The graph derives variable operations from these declarations. Declaring
`level: entity` and assigning `level = globals.Level` produces a **Set Level**
node with one entity-valued **Value** input. It stores the named scene entity in
this behavior instance's `level` variable. The assignment does not change scenes
or rewrite the global binding. The same mechanism produces **Set Score** from
`score = 10`; no native node registration or C code is needed.

In Studio, add a variable in **Members**, then edit its **Name**, **Type** and
default in **Properties**. The node chooser offers its **Get** and **Set**
operations. Dragging the variable onto the graph creates a Get; Alt-dragging
creates a Set. These edits update the same source and use normal undo.

An authored function is the reusable way to create a custom operation. Its
parameters become inputs and its results become outputs. A call uses the same
definition wherever it appears. Small pure calls can be folded into their
consumer's input without changing the source or the number of evaluations.
Use **Open Function** on a call or its folded input to inspect the definition.
**Inline Function into Caller** instead replaces a supported call with its body
in the source as one undoable edit. The definition remains available for other
calls. **Fold into Input** and **Show Input Nodes** only change presentation.

Adding a standalone variable **Get** creates a named local read, such as
`levelValue := state.level`. Its execution pins show when the read occurs; its
output retains that value. Choosing the variable as an existing input's source
reads it when that consumer runs. The `state` qualifier identifies the declared
state even when a local parameter has the same name. A newly added **Set** uses
a zero default where one can be written, or the variable's current value for
types such as entities; edit its Value input to choose the intended source.

Language declarations, bindings and member names use identifiers. Quotes denote
string values, including user-facing text and literal union alternatives.
Prototype, component and relationship symbols produce typed reference values:

```chibi
asset: Prototype = MovementEmitter
component: Component = Health
relation: Relationship = Targets
Forward :: (value: Prototype) -> Prototype { return value }
emitter := await spawn_prototype(Forward(asset))
```

The reference category survives locals, parameters and results. A string containing
`"MovementEmitter"` is not a prototype reference, and a component reference cannot
be passed where a prototype is required. Missing, ambiguous and wrong-kind symbols
are errors. Source member paths use registered component and field spellings;
completion supplies the available names.

Values include booleans, numbers, strings, arrays, sets, maps, nominal handles and
reflected structs. Arrays use `number[]`; collection types include `set<int>` and
`Map<string, int>`. Literal arrays use `[1, 2]`; reflected struct literals use `{x: 1, y: 2}`.
Collection natives such as `array_add` return a new value: write
`hair = array_add(hair, h)`. A pure native called as a statement is an error.
Runtime strings cannot contain embedded NUL bytes. Explicit scalar conversions use
`cast<number>(value)` or `cast<float>(value)`. They remain available at numeric
boundaries; ordinary arithmetic uses its inferred or expected type. A cast to a
value's own type, such as `cast<entity>(get_global_variable("Player"))`, only
supplies the type a polymorphic call needs.

### Array storage

The runtime automatically packs eligible array elements into contiguous fixed-width
storage. Eligible types are booleans, signed and unsigned integers, F32/F64 values,
enums, handles, and plain fixed-layout records, including vectors. A record is
eligible only when every field, including nested records, has a validated fixed
layout and needs no managed storage. Strings, names, managed record fields and
nested collections use boxed elements. Arrays containing null record elements
also stay boxed. Mutable native builders may remain boxed until an owned snapshot
is made.

Packing changes physical storage, not element order, indexing, value types, copy
ownership or iteration semantics. Inspection and persistence support both layouts.
Maps use separate associative storage; they are not packed arrays.

The focused numeric-loop check compares boxed and packed inputs for ten numeric
types, with 64 elements and 16 trials per representation, through the VM and the
available compiled backend, native SLJIT or Wasm. It checks equal results and
reports payload bytes and elapsed time. This small workload does not establish a
universal speedup.

### Maps and iteration

Maps retain separate key and value types. A complete type can be explicit or
inferred from a nonempty initializer:

```chibi
scores: Map<string, int> = {"Ada": 3, "Bo": 5}
inferred: Map = {"Ada": 3}
more := {"Ada": 3}
empty: Map<string, int> = {}
scores["Ada"] = 4
if map_has(scores, "Bo") { score := scores["Bo"] }
scores = map_remove(scores, "Bo")
for name,score in scores { record_score(name, score) }
```

`map_has` guards a lookup. A missing key fails the lookup; it does not synthesize
a zero value. Assignment inserts or replaces a value. Replacement keeps the key's
position; removal followed by reinsertion appends it. Iteration takes a snapshot
in insertion order. Map equality compares associations independently of order.
Dynamic duplicate keys keep the last value at the first position. Keys must be
supported finite scalars, strings, names, enums or handles. Nested maps and arrays
retain their complete value types. An empty literal needs a concrete contextual type.

Array iteration binds the item first and the optional zero-based index second.
String iteration yields one string per Unicode scalar, not one byte or one
grapheme cluster. Both take a value snapshot before entering the loop:

```chibi
for item,index in items { total += item * index }
for character in "Aé🐈" { append_character(character) }
for item: ref in items { item += 1 }
```

Reference iteration writes through to a mutable owned array binding. Other
bindings copied from the array keep their snapshots. The borrowed source cannot
be accessed or replaced directly inside the loop; use `item`. Its body cannot
suspend, start tasks, perform opaque host operations or call mutating procedures.
`break` and `continue` preserve writes already performed. This form rejects
temporary arrays, reference parameters, maps, strings and paired item/index
bindings. The implementation stores logical references and resolves each write
against the source binding; source code never receives a raw element pointer.

Vector receivers support `velocity.norm()`, statically resolved to the existing
vec2 normalization operation. `vec2_norm(velocity)` names that operation directly.
Neither form introduces object allocation or virtual dispatch. Normalization retains
the native operation's zero-vector behavior.

A declared numeric result or argument type supplies context for an otherwise
untyped arithmetic expression, so ordinary arithmetic does not require casts.
Typed operands keep their precision: a double
operand also gives otherwise untyped literals double precision before any
conversion to a float destination. Use `cast<double>` to request that precision
explicitly. A decimal point alone does not require double precision.

Host events infer their registered payload. `on Tick(dt) { ... }` binds the
registered Tick delta to `dt`; `on Tick { ... }` needs no explicit payload list.
`event.dt` also reads that payload, independently of the general frame clock.
A handler may name its entire payload with `on Tick as tick { ... }`. Event
payload members are read-only unless their reflected signature supplies a reference.

A behavior, library or system has an implicit invocation owner, `self: entity`.
Library calls use their caller's owner. Component paths read the
owner or another entity handle and follow reflected members:

```code
Emitter :: behavior {
    on Tick {
        self.ParticleEmitter.Active = true;
        self.ParticleEmitter.Rate += event.dt;
        parent := self.ParticleEmitter.Parent;
        health := parent.Health.Current;
    }
}
```

These paths lower to registered component accessors. They do not need binding
strings, casts or a dynamic property interpreter. Assignment and compound
assignment target a writable reflected component field on the exact addressed
entity. Reads can traverse nested reflected structs; assigning a nested struct
subfield is currently rejected. Component and field names that cannot be resolved
produce source diagnostics. Prototype membership improves completion ranking;
it does not guarantee that a component is present on a live entity.
A component path used as a condition tests presence instead: `if self.Dash`,
`!self.Dash`, `self.Dash == nil` and `&&`/`||` operands ask whether the
entity has the component, so `if self.Dash && self.Dash.remaining > 0`
reads the field only when it exists.
`chibi_ctl --format` spells reflected accessor calls as paths: `get_dash().Value`
becomes `self.Dash`, `has_dash()` in a condition becomes `self.Dash`, and a
statement `set_dash_remaining(v)` becomes `self.Dash.remaining = v`. It also
spells supported state reads as `state.x` and operator functions such as
`add(a, b)` and `less(a, b)` as operators. Each rewrite is kept only when the
program analyzes the same. Awaiting a component accessor does not wait.

Ordinary blocks support assignments, compound assignments, `if`/`else`, `switch`, `while`,
`for i in first..last` with inclusive bounds, `for item in array`,
`break`, `continue` and calls. An ordinary call invokes its native entry and then
continues with the following statement, including entries that emit no outcome.
`await` follows one native `Then` or `Completed` outcome instead; a suspended
capability resumes that continuation when its native wait completes. Waiting suspends
that behavior continuation; the engine thread keeps processing other work. Calls without
one conventional completion produce a diagnostic when awaited. Native sequence
branches use `sequence { branch { ... } branch { ... } }` and run in their native
order. Labels and `goto` express shared or irreducible control flow when structured
blocks cannot describe it.

A `switch` evaluates its value once and executes the matching case. Cases never
fall through, so they do not need `break`. `case:` is the default. The switch
keeps its outer braces; braces around each case body are optional:

```code
switch level {
    case 1:
        damage = 10
    case 2: {
        damage = 20
    }
    case:
        damage = 30
}
```

Project version 24 migrates existing `function` declarations and `match` statements
to these forms. Those old keywords are no longer runtime syntax.
Version 26 removes the named `fn`, `pure` and `foreach` spellings. Current reserved
words include `union`, `switch`, `is`, `and`, `or`, and `where`.
If a version 23 project uses one as an identifier, rename it and its references
before migrating; the migration does not rename identifiers automatically.

An unawaited call continues before its native completes, so using its data
result is an error. An awaited call with one data result can declare a
block-scoped result:

```code
on Begin {
    emitter := await spawn_prototype(MovementEmitter);
        emitter.ParticleEmitter.Parent = self;
}
```

The name becomes available after completion and remains inside its lexical block.
Capturing a shared action instance is rejected because later invocations could
replace its output. Scopes with explicit labels cannot declare awaited results,
because a jump could bypass their initialization. An ordinary block declaration
such as `x := calculation;` evaluates once at that execution point using a native
Snapshot node. Later reads use the captured value.

### Unions and patterns

Named tagged unions declare alternatives with optional owned payload fields.
Literal unions restrict a value to the listed strings. Type alternatives use `|`.

```code
AttackState :: union { Idle | Windup(remaining: number) | Active(damage: number) }
Status :: union { "Yes" | "No" }

phase := AttackState.Windup(0.3)
switch phase {
    case .Idle: ready = true
    case .Windup(remaining): countdown = remaining
    case .Active(damage): health -= damage
}

result: number | Cancelled = 0
if result is number num_result {
    total += num_result
}
extracted := result as number
if extracted {
    total += extracted
}
```

`is` and `is not` test types or union alternatives without converting values.
Matching branches narrow the original symbol; the opposite branch removes the
tested alternatives. A terminating branch also permits the surviving path to
keep its proof. Assignment or calls that may change the value invalidate that
proof. Pattern bindings belong to the matching arm or positive condition.
Switches over finite unions must cover every alternative or include `case:`.
Union declarations require a value, and union function results must be initialized
on every return path. Guard an optional union before switching over it.

`as T` safely extracts `T?`: a mismatch is `nil`, and a match preserves zero,
false, and empty text as present values. It does not perform numeric conversion.
An optional condition tests presence; inside that branch the value has type `T`.

Union values retain immutable snapshots of their payloads in execution-owned
storage. Trivial payload fields live inline in the tagged record; creating that
record uses arena storage. `Cancelled` is a shared payload-free singleton.
Union values can be local values, parameters, results and behavior state. ECS
component fields currently reject unions because ECS storage does not own their
managed payloads. Recursive owned union payload types are rejected; use task or
entity identities for references to independently owned work or entities.

### Tasks

A function waits when its body contains a retained `await` or `yield`.
Compiler-proven synchronous component awaits do not make it wait. Calling such a
function without `await` starts a task: it runs inside the call until its first
suspension, then runs beside the caller and returns a task handle. Awaiting a
call gives its result directly. Awaiting a `task<T>` handle gives `T | Cancelled`.
The successful alternative contains the original result, including zero, false,
and empty strings. `Cancelled` is a distinct singleton, never a default result.
It is reserved for cancellation and cannot appear in a task's successful result type.
`Cancelled` is also a semantic builtin: a bare declaration cannot reuse its name.
Qualified names such as `Outcome.Cancelled` and component fields named `Cancelled`
remain legal; this restriction does not make it a grammar keyword.

```code
Turret :: behavior {
    pulse: task
    ready: bool = false

    Charge :: (time: double) -> (power: double) {
        await delay(time)
        yield
        return(time * 2)
    }

    on Begin {
        pulse = Charge(1)
        cancel pulse after 2
        result := await pulse
        if result is Cancelled {
            ready = false
        } else {
            ready = result > 0
        }
    }
}
```

- `yield` resumes on the next update.
- `cancel t` stops a running task and the tasks it started. Awaiters receive
  `Cancelled` and can continue; cancelling the awaited target does not by itself
  cancel the awaiting caller.
- `cancel t after n` schedules cancellation without waiting. Its deadline is the
  task's original start time plus `n`, measured in the same game-clock seconds as
  `delay`. The start time is recorded before the task's initial synchronous work.
  Negative or NaN durations count as zero; positive infinity sets no finite
  deadline. Repeated deadlines keep the earliest. An overdue pending task stops
  at a safe point; already performed effects remain. Completion removes the deadline, and
  cancelling a completed task leaves its successful result intact.
- A completed result remains available while a live handle or waiter can observe
  it within its owner. Copied handles name the same task. Awaited values have their
  own snapshots and remain valid after the task handle is dropped. Locals stop
  retaining results when their scope exits, including `break`, `continue`, and
  jumps to an enclosing scope; references held by other tasks keep their values.
- Dropping the last handle does not cancel a running task. Unobserved completed
  results are reclaimed at owner safe points, so repeated fire-and-forget work
  does not accumulate results each frame. Collection visits live frames, typed
  value banks, state, captured locals, and nested result values; task-slot storage
  is reused up to the owner's peak number of simultaneously retained tasks.
  Collection indexes live tasks once, then resolves each handle in logarithmic
  time; chains of task results are visited iteratively. Primitive collections
  are skipped because their elements cannot contain task handles.
- A task is true while it is running, so `if t` and `if !t` test it. An unassigned
  `task` state names no task; awaiting it yields `Cancelled`.
- Handles remain bound to the entity or system registration that started them;
  copying a handle to another owner does not grant that owner access to the task.
  Tasks stop when their owner is removed. A running task never blocks its owner's
  events and ticks.
- Calls into a library start tasks in the same way, such as `Store.Work()`.
- An explicit `task<T>` annotation declares the successful result type. A `task`
  state infers its successful result from the functions assigned to it.
  A state holding different function signatures, or a plain `task` parameter,
  can be awaited only for completion.
- Multiple function results form the successful tuple alternative. Narrow away
  cancellation before accessing fields: `r := await t; if r is not Cancelled {
  use(r.b) }`.
- Tasks retain references to state, components, and locals. A captured local is
  shared with its caller while the caller runs and remains alive after it returns.
  Each declaration creates a new local, including declarations inside loops.

The runtime retains call frames and captured locals across supported native waits
and cancels owned invocations when their entity or system registration is
removed.

Native calls use their registered source names. Named
arguments follow `name: expression`; ordinary statement calls may use positional
arguments. Explicit types are needed for polymorphic values that reflection
cannot infer. Multiple results use named projections, such as
`nearest.Entity`. No result is selected by guessing the first output.

A local declared inside an executable block evaluates its initializer at that
statement and retains the value for that activation:

```code
nearest := find_nearest_entity(Owner: self, Radius: 500)
```

A computation that should be recomputed is written where it is used or as a
function. Assignments belong in executable blocks. Stateful capabilities shared by several paths
have one named action instance. Methods invoke its named entries, and handlers
consume its named outcomes. The instance keeps its state across method calls and
events. An invocation runs any emitted outcome handler before its next statement.
Action completion handlers use `after action.Outcome`, or `after action` for a
single conventional completion. `on` handles events. For example, opening a gate
and entering it can occur in the same handler:

```code
SharedGate :: behavior {
    visits: retain int = 0;
    action latch = gate();
    on Tick() { latch.open(); latch.enter(); }
    after latch.Exit { visits += 1; }
}
```

A named function uses `Name :: (parameters) -> result_type { body }`. An optional
`=>` can precede its brace body:

```code
DamageFor :: (level: int) -> number { return level * 2 }
OtherDamage :: (level: int) -> number => { return level * 2 }
```

A single result may be named with `-> (Damage: number)`, and multiple results retain
that parenthesized form. Anonymous function values and closures are not supported
by the current callable model.

Function parameters and results can carry the same erased dimensional units as
component fields:

```code
Distance :: (speed: u/s, elapsed: s) -> u {
    return speed * elapsed
}
ChargeRate :: (charge: number charge, elapsed: s) -> number charge/s {
    return charge / elapsed
}
```

Known standalone unit names and compound units use `number` storage. An explicit
numeric storage type can precede the unit, as in `float u/s` or `number charge`.
Unknown bare type names remain errors; use the explicit form for a custom single
dimension. Units are checked at calls, defaults, returns and reference writes,
including named multiple results and imported signatures. Native callable
metadata can supply the same contract. Incompatible known dimensions are compile
errors, and unit-only signature edits invalidate affected compiled callers.
Unannotated parameters/results remain dimensionally unknown; this does not infer
unit-polymorphic signatures or permit cyclic library publication. Numeric
inference and runtime representation stay unchanged.

Functions declare data parameters and results. Ordinary functions infer their
execution continuation and effects. Purity is inferred through local and imported
calls, including recursion, writes and suspension. Starting a task is an effect.
Query filters additionally reject reads of retained or per-invocation values,
even through an imported helper. Calls use the function name directly.
Qualification selects an external library, such as `MovementActions.ApplyForce(force)`.
Reference parameters use `ref`; a call passes a writable value directly, such as
`Nudge(self.Velocity)`, and the function writes through it. There is no address-of
operator. Other parameters are copies and can change without changing the caller.
All authored functions use the shared call-depth budget of 64. Source metadata
cannot override that limit or declare a custom owner type. Descriptions belong
in comments rather than executable metadata declarations.
The bounded [qualified-module composition specimen](module-composition.md) checks
existing discovery, lexical/qualified binding, cycle rejection, state visibility
and independent catalogs. It does not introduce import aliases or new visibility
syntax.

Argument-bound type parameters preserve generic relationships, for example
`Identity :: (item: $T) -> T { return item }`. Later uses of `T` must match the
bound type. `$N: int` binds a compile-time value argument. A function
gives its results with `return x`, or `return(a, b)` in signature order. A bare `return` gives
each result's current value.

A function without `->` infers its results from its first `return`: `return x` gives one
result named `Result` with the type of `x`, and `return(Hit: h, Distance: d)` gives one result
per label. Later returns must match. A function that never returns a value has no results.
Recursive functions, and functions that call each other in a cycle, declare their results.

### Compile-time values and constructed types

`$T` binds a type from a runtime argument; `$N: int` binds a value that must be
known at compilation. Specialization keeps concrete storage types and reuses the
same instantiation for identical bindings:

```chibi
Identity :: (value: $T) -> T { return value }
Scale :: (value: $T, $N: int) -> T { return value * N }
Factorial :: (n: int) -> int {
    if n <= 1 { return 1 }
    return n * Factorial(n - 1)
}
Size :: Factorial(5)
```

An ordinary procedure runs in a compile-time context when its inputs and body
are available there. It needs no `pure` or `comptime` marker. Runtime storage,
host effects and unsupported operations cannot enter that evaluation. The evaluator
supports scalar and type values, authored calls, local variables, assignment,
`if` and `while`. Arrays, `for` loops, closures and native calls are not supported
in compile-time execution. Each evaluation has a 65,536-step budget and a
128-call depth limit, separate from the runtime call-depth limit of 64. Expression
and statement nesting are each limited to 256 levels.

Qualified library calls also specialize generic functions and construct types,
for example `Units.Quantity(double, 1, -1)`. Specializations are materialized in
the defining library, with that library's source ranges and lexical bindings;
their bodies are not copied into the caller's analysis. Nested imported requests
resolve before executable generations are published. A failed specialization
does not publish a callable generation. Body edits rebuild dependent runtime
callers; changed compile-time results or constructed layouts invalidate cached
analysis as semantic catalog changes.

Libraries can construct record types from compile-time type and value parameters.
For example, dimensions can live in type arguments while the runtime value stores
only the numeric payload:

```chibi
Units :: library {
    Quantity :: struct($DataType: Type, $Length: int, $Time: int) {
        amount: DataType
    }
    ProductType :: (A: Type, B: Type) -> Type {
        return Quantity(A.DataType, A.Length + B.Length, A.Time + B.Time)
    }
    Multiply :: (a: $A, b: $B) -> ProductType(A, B) {
        result: ProductType(A, B) = {amount: a.amount * b.amount}
        return result
    }
}
```

`Quantity(double, 1, -1)` multiplied by `Quantity(double, 0, 1)` produces
`Quantity(double, 1, 0)`. The resulting record stores one double; dimension
exponents are compile-time metadata. The procedure constructs the return type
using ordinary type operations. This is separate from the existing erased unit
annotations on native/component fields described above.

When several graph calls share a pure generated expression body, formatting
emits one ordinary pure function and calls it from each original site. Typed
functions also preserve expressions whose internal precision or capture types
would be lost in a shorter formula. An action with a consumed data result can
use an ordinary function to retain its result type. The operations, parameters,
results and input references remain executable code. Editing that function now
owns its body; the old generated formula no longer regenerates it automatically.
A generated action may mark its caller capture as writable even though its
function entry takes a value snapshot and its body writes a named state directly.
The ordinary function preserves that value parameter and concrete state write;
the generated caller-only reference flag is no longer retained.

Macros declare semantic entry and exit paths:

```code
macro Choose(flag: bool) -> () {
    exit Accepted;
    exit Rejected;
    entry Enter {
        if (flag) { return.Accepted(); }
        else { return.Rejected(); }
    }
}
```

Additional event scopes use `events ScopeName { ... }`. Disabled actions are
explicit source declarations, such as `disabled action gate = multi_gate();`.
Disabling behavior is executable source semantics.

Systems declare their required and optional components in the header. Their
ordinary body runs once per matched entity per tick:

```code
EmitterControl :: system(emitter: ParticleEmitter, trigger: Aim?) without Collider {
    if (trigger) {
        emitter.Active = trigger.Firing;
    }
    emitter.Rate += event.dt;
}
```

Required parameters determine query membership. `?` makes a component optional;
`without` excludes the listed components. Optional members require a presence
guard. `if (trigger)`, `if (!trigger)`, `trigger != nil` and `trigger == nil`
select the branch in which the component is present. Reading an optional member
outside that branch is a compile error. An `await` invalidates the presence
proof: guard the component again before using it after suspension. Presence
checks lower to native Has Component operations, rather than testing the
component's value. A component cannot appear in both the parameter list and
`without`, including an optional parameter.

An explicit query declaration also supports `all`, `any`, `not` and `has`, such as
`query all(has(Transform), not(has(Collider)));`.
When both a header and an explicit query are present, the query intersects the
header's required and excluded components. A parameterless system must write
`()`; its body still has an implicit Tick, including an empty body. Systems
without declared membership use the runtime's inferred component query.

The header form `Move :: system(position: Transform, velocity: Velocity)
where not Frozen { ... }` supports Boolean membership filters and
relationship-sourced bindings. `join aim: Targets.Transform?` groups a binding
that follows an exclusive relationship; the existing comma-separated binding
has the same meaning. `team: Team from ancestor(ChildOf)` selects the nearest
matching ancestor. Sources also accept `from self` and `from Targets`. Header
filters can test `self.Frozen`, `Targets.Frozen` or
`ancestor(ChildOf).Frozen`; `has(Targets)` tests relationship existence without
repeating the root's body. Access is inferred from the body. See
[query headers and relationship sources](runtime-authoring.md) for source
selection, explicit relationship loops, and their lifetime restrictions.

### Shared-target reductions

A synchronous system can contribute to a numeric component field without reading
and writing the destination immediately:

```chibi
Damage :: system(projectile: Projectile) {
    reduce projectile.Target.Health.Damage += projectile.Damage
}
Closest :: system(candidate: Candidate) {
    reduce min(candidate.Target.Threat.Distance, candidate.Distance)
}
```

The destination must be a writable numeric ECS component field. Contributions
are evaluated when the statement runs, then published after the system batch
finishes and before the next dependent system prepares its query. A producer
cannot ordinarily read or write a component it reduces; this includes reachable
helpers, bindings and value/change filters. Component-presence tests are allowed.
The restriction lets separate systems calculate contributions to shared targets
concurrently. Eligible reduction rows can also execute on workers with private
contributions merged in row order. The runtime requires sufficient rows/workers,
streaming execution and verified reduction-only effects; ordinary writes,
observation, inspection or profiling retain the serial fallback. See
`execution_program_run_system_parallel` in `src/execution/program.c` and its
caller in `src/runtime/behaviors/source.c`.

Ordinary `reduce` replays contributions in stable system/query order, including
integer wrapping at the destination width, clamping each bounded-field update,
and normal floating-point rounding. Min/max use the existing comparison-and-select
rules: the incoming value wins ties and unordered NaN comparisons. Units are
checked as for assignment. A failed system discards its contributions; earlier
successful systems stay published, and later peers' contributions are discarded.
This is not a rollback of ordinary writes. Debug pauses retain the unfinished
reduction dispatch and resume it before dependent work proceeds.
Reloading a paused system cancels its unfinished log. A scheduling change while
paused restarts the current system phase with fresh dispatches, including systems
already run in that phase; completed earlier engine phases stay complete. Scene
reset and engine teardown also cancel held dispatches.

Opt in to regrouped sums when the changed numeric behavior is acceptable:

```chibi
Damage :: system(projectile: Projectile) {
    reduce unordered projectile.Target.Health.Damage += projectile.Damage
}
```

`unordered` combines contributions per target within each system, then adds the
subtotal to the field. This reduces log entries and publication work when many
rows share targets. It can change floating-point rounding, NaNs and signed zero,
so use ordinary reductions for calculations that need the default reproducible
order. Integer addition still wraps at its declared width. Unordered sums require
fields without a stored range contract; clamping once would differ from clamping
each update. A producer cannot mix unordered sums with ordered reductions on the
same component. Min/max remain ordered. Publication occurs once per aggregated
target field, so its write-revision count differs from the ordered form.

Reduction logs and prepared query rows use temporary memory proportional to the
work. Idle buffers have bounded retention; the retention limit does not limit
valid query size. Reductions avoid conflicting direct writes, but do not guarantee
a speedup: small workloads can cost more to collect and publish than a serial
ordinary update.

### State lifetime and live reload

State belongs to a behavior instance and persists across event executions and
suspension. Task, entity and array state needs no default; it starts empty.
A nonrestart publication preserves matching state by stable variable identity,
sharedness, scalar storage, container and nominal type name. New or nonmatching
state starts from its new default; removed state disappears. A changed default
does not replace a matched value unless explicitly restarted. This state matcher
does not compare reflected layout hashes; installed-schema preflight is a
separate check, not a promise of arbitrary reflected-layout migration.
Publication occurs outside active dispatch, cancels the old continuations and
tasks, and replaces the program; it does not wait for suspended work to finish.
Invalid drafts retain the applied source/runtime generation.

Explicit system `retain` belongs to the registration, persists across completed
invocations and survives having no query matches. Removing or re-adding a match
does not reset it. Imported library retention follows the same owner. Ordinary
system-body declarations are invocation-local: each complete query tuple has
independent local variables and suspended work, but no private retained bindings.
Removing a structural tuple cancels that work and discards its locals; a new
match starts a fresh invocation. Entity generations participate in tuple identity.
Suspended system executions resume their continuation rather than starting a
second invocation for that tuple. A completed invocation may start again on a
later eligible update. A value predicate becoming false does not itself cancel
already suspended work; check changing gameplay conditions inside the coroutine.
System publication uses the same matching-state replacement path. Query-managed
rows are retained only when the query generator shape matches; later membership
refresh still releases rows that no longer match. In the preview caller,
`preview_sources_stage` sets restart only for the explicitly requested
`restart_path`; dependent replacements preserve matching state. Explicit restart
resets retained state for the owner. Components keep their independent ECS
lifetime and are not reset merely because a system query stops matching. These rules come from
`src/authoring/preview_source_publish.c` and `src/execution/program.c`, not the
retired native application-DLL reload mechanism.

### Named scene entities

The built-in `globals` namespace exposes entities named by authored
`GlobalBinding` components. It is available in behaviors, libraries and systems:

```chibi
CameraFollow :: behavior {
    player: entity
    on Begin {
        player = globals.Player
    }
    on Tick {
        self.Transform = player.Transform
    }
}
```

Names are discovered from the project's prototypes and scenes before Play.
`globals.Player` has type `entity`; it needs no string lookup or cast.
Completion after `globals.` lists the declared names, and an unknown name is a
source error. Definition, references and rename connect a member to its authored
bindings. A name may be reused in different scenes; two entities cannot publish
the same name in one running scene.

Each read resolves the binding in the current scene. A declared name absent from
that scene is a runtime error. Assigning the result to a behavior variable,
as above, captures that handle rather than maintaining a live binding; it does
not retarget the variable when the scene changes.

Namespace members are read-only references: assign to an entity's writable
component, such as `globals.Player.Transform`, rather than to `globals.Player`.
The namespace itself is not a value. These bindings do not declare shared scalar
variables or persistent save data.

### Input actions

A project's input map, `content/input.kdl`, names each action, the Control
labels that hold it, and whether it also holds the Aim of input owners:

```kdl
Boost {
    (array)sources "Shift" "Pad Left Shoulder"
}
Fire {
    (array)sources "Mouse Left" "Pad Right Trigger"
    firing #true
}
```

A map holds at most 256 actions. Each action is an event that entities with
Input receive when one of its sources goes down, so an action cannot take the
name of a host event such as `Tick`, and no other global may be named `input`.
A project event may share an action's name; pressing the action raises it. Scripts read an action through the `input` global:
`input.Boost.held` is true while any of its sources is down, and
`input.Boost.pressed` is true for the one fixed step after one went down, even
for a tap released before that step. Action names are typed members, so
`input.Bost.held` is a compile error, and completion after `input.` lists the
map's actions. A pause or focus change keeps held actions, so resuming with the
same controls held presses nothing again.

Migrating a version 15 project moves its ActionBinding components into the
map, leaving out disabled and retained ones. Which action a script's
`ActionBinding` or `PressedChanged` meant depended on its entity, so a script
that still uses one stops the migration until it is rewritten with
`input.<Action>`. So does an existing `content/input.kdl` that is not an input
map, until it is fixed or moved.

### Maintained ECS relationships

A root-level `bind` maintains a writable component field from a deterministic
value expression:

```code
LinkedEmitter :: behavior {
    bind self.ParticleEmitter.Active = input.Boost.held;
}
```

Bindings are retained relationships, not declaration-time snapshots. The runtime
maintains them at deterministic safe points around input, behavior execution,
structural command commits and system phases. It resolves current entities and
components again at each safe point, including when a parent handle changes.
An unavailable source makes an available destination zero or null; an unavailable
destination remains dormant until it exists again. Initial subscription and
retargeting establish the current event baseline without inventing a transition.
Component writes are observed at maintenance points. Behavior callbacks may
suspend using normal behavior continuations.

A system maintains relationships for its matched entities. While an entity's
system invocation is active or suspended, relationship values and subscription
baselines still update, but change callbacks are dropped without accumulating a
backlog. A suspended change callback becomes that entity's single system
invocation; Tick does not run concurrently with it. These callbacks use the
existing coroutine runtime and do not introduce a separate scheduler. Losing
query membership removes that entity's registrations along with
its retained state. Reactive declarations belong to behavior or system roots,
not callable libraries or nested function bodies.

A binding expression cannot assign, suspend, use randomness or invoke a capability
with side effects. These checks include named computations and callable bodies.
Within an asset, duplicate binding destinations and dependency cycles are rejected,
and bindings run in dependency order. At each world maintenance epoch, ownership
checks also reject identical or overlapping destination fields across behavior
and system managers for the same entity generation. Cross-asset dependency cycles
are not analyzed globally. Unchanged destination values are not written.
The compiler and execution runtime implement the relationship; `.chibi.meta`
cannot provide it. The authoring graph projects the resolved binding and its
editable value and target. Compiler-generated maintenance work is not an
independent editable graph operation.

### Authoring metadata and reconciliation

Project manifests use version 26. Executable files contain no generated identity
annotations or embedded authoring footer. Optional `<asset>.chibi.meta` JSON stores
identities, layout, comments, watches and breakpoint settings. It cannot supply
executable values or connections. Deleting it leaves the code independently
compilable and loses only presentation/debug identity.

Disk metadata is bound to exact source bytes. Stale metadata is ignored. Matching
ambiguity during an edit does not invalidate executable code; uncertain identities
are replaced, and runtime state compatibility decides whether a restart is required.

Source collections retain syntax, resolved facts, diagnostics and executable
modules for each generation. Graph views retain a presentation of those facts.
Graph edits produce source edits followed by analysis and preparation; source
edits refresh the projection. Invalid drafts preserve the applied generation.
Highlighting, completion, diagnostics, hover,
definition, references, rename and call hierarchy consume shared semantic APIs
outside Studio. Their UI availability and verification are recorded in
`docs/authoring-navigation.md`.

Completion shows source-style signatures and prioritizes names in scope,
receiver members and values compatible with the current argument. Regular and
Vim editing preserve indentation on new lines and indent inside brace blocks.

A connected input can retain a binding or literal for later disconnection:

```text
reroute_data(In: fallback(driver, binding: component("Movement"), default: {speed: 0}));
```

`fallback` preserves the input's authored precedence: the connected driver wins, then the binding, then the literal default. Removing the wire restores the binding; removing the binding restores the default. It does not test truthiness or catch runtime errors. The `binding` and `default` clauses are optional, but at least one must be present. A binding with a retained default can use `fallback(binding: speed, default: 0)`; a binding must be writable. Defaults must be compatible literals.

### Automatic performance diagnostics

Ordinary code needs no `hot` or `noalloc` annotation. Successful source preparation
exposes nonfatal `authoring_source.performance` diagnostics for allocation, copying,
and values retained beyond their producing expression or frame. The shared
`behavior_function_costs` API analyzes finalized normal instructions and follows
linked callees, including library bodies. `execution_module_costs` also includes
entry points, binding bodies, and imports. Evidence retains the originating
function, instruction, capability, and source location.

“Proven” means the generated body contains the identified operation; a conditional
branch may never execute it. An allocation is a dynamic storage request, which
an arena or pool may satisfy without a fresh heap allocation. Scalar moves do not
count as managed copies. “Possible” covers nullable payloads, cache misses,
runtime callbacks, and native code without a verified cost contract. Pure native
functions are not assumed allocation-free. Folded constants and scalar arithmetic
carry no such body costs. These summaries do not estimate elapsed time, operation
counts, or algorithmic complexity, and exclude backend compilation and initial
activation setup. Analysis
is bounded to 1,024 definitions/modules and 200,000 instructions; exceeding a
budget returns conservative possible costs and an incomplete summary.

Normal native, Wasm and bytecode bodies omit source watch and pause operations.
Native and Wasm inspection code is compiled on first use and cached for the
retained definition. Concurrent native first-use inspection can execute bytecode
until compilation publishes the complete code and resume map. Pauses and watches
preserve the canonical instruction locations. Detaching inspection after a debug
pause keeps that saved continuation canonical until its next real wait or
completion, preserving values while temporarily forgoing optimization of that
continuation.

Normal VM and compiled bodies also remove construction, snapshots and tag/payload
operations for nonescaping unions of plain scalars, including nested unions and
aliases whose incoming values agree at forward control-flow joins. Isolated native
calls with only plain scalar inputs and outputs preserve unrelated virtual values;
the calls, side effects and errors still execute. Different values merged at a
join, backedges, managed payloads, escaped values, authored calls, context callbacks
and suspension retain their owning representation. Analysis has a bounded scratch
budget and conservatively retains allocations when it cannot prove safety.
Inspection retains the canonical
operations. Numeric interval analysis removes proven redundant clamps and tests,
and can compare original integers when widening them to `number` was exact.
It preserves floating-point rounding, NaN and signed-zero behavior; `n := 0`
still infers `number`. Generic component reads do not acquire range proofs from
metadata alone.

Read-only plain-array traversals can borrow their existing storage during a
synchronous invocation, including loops that update unrelated scalar state.
The compiler rejects borrowing when addresses escape, collection storage can
change, raw header or pointer identity is observed, an opaque helper runs, or
execution can suspend. Runtime guards check the actual array and element headers
and exclude overlap with writable context, state, result, host-value, saved
activation and VM scratch banks.
Matching arrays avoid snapshot allocations and element copies; checking their
headers still takes linear time. Other inputs retain the canonical deep copy and
its errors, including mixed element headers supplied by native callers. Debug
inspection retains owned snapshots, and prepared modules recompute the proof
instead of trusting saved optimization flags. Purity alone does not establish
that a helper cannot mutate an aliased collection.

The same proof follows synchronous authored helper chains. Eligible array
parameters become internal call-scoped read-only views automatically; no new
parameter syntax or author annotation is required. The callee cannot mutate,
return, retain, expose collection identity, start tasks, or suspend with a view.
Unsupported or recursive call chains keep owning copies. Runtime checks cover
actual argument headers, ancestor scratch and bound library state, and recognize
only the engine's exact safe callbacks. Custom callbacks and debugging keep
snapshots. Generated C conservatively retains caller snapshots around helper
calls, while its shared call driver can still borrow verified helper arguments.
These restrictions govern when the optimization applies; ordinary array helpers
retain their existing value semantics when it cannot apply.

Preview source preparation reuses unchanged analysis and executable generations
through `authoring_sources_prepare_incremental`. Exact source identity/text,
semantic catalog metadata/defaults, backend and native binding generation must match.
Editing a library body rebuilds its transitive callers. Generations own their
data independently of older collections; declaration discovery and parsing still
run. Default comparison evidence is capped at 1 MiB and depth 64; unsupported
defaults, managed struct pointers or padding can conservatively prevent reuse.
Custom binding providers must supply a nonzero immutable generation to opt into
reuse. This is an in-process cache, with no persistent artifacts.

World-owned strings use a hash table with exact text comparison and stable
allocations until world teardown. A null input interns as an empty string.
Lookup has expected constant probe count at a bounded load factor; adversarial
collisions can still require a linear scan. This structural improvement and the
removed instrumentation work are not a measured speedup claim.
