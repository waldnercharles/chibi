# Asset and behavior source — revision 3

Executable behavior source uses `.chibi`; declarative content remains KDL.
The shared C frontend retains syntax snapshots and resolves typed semantic
records. The compiler lowers those records directly into verified execution
modules; visual editors consume their authoring projections.

## Package analysis

The SDK's `script_analyze_package` and `script_declarations_package` accept a
`script_package_options` describing sibling snapshots, native declarations and
resolved imports. Siblings have one matching `package` header.
Functions, constants, records and unions share a package scope. Declarations are
collected before function bodies, so siblings can refer to one another and
functions with declared results can recurse across files.

```code
// arithmetic.chibi
package sample
Add :: fn(value: int) -> int { return value + 2; }
```

```code
// main.chibi
package sample
Main :: fn(value: int) -> int { return Add(value); }
```

Each input supplies a display path, a stable document key and parsed syntax; the
caller supplies a stable package identity. The returned analysis owns its source
texts, paths and keys. `script_range.document` selects the input document, while
byte offsets and line/column values stay local to that document. Lowered execution
origins retain its display path after the analysis is freed. Exported identities
depend on package identity and declaration, not input order or unrelated sibling
files. Debug record ordinals and byte offsets are not durable identities.

`source_package_snapshot_new` in `Chibi::source_catalog` separately resolves a
complete set of source buffers against explicit project and collection roots.
It owns parsed documents, groups sibling files, resolves relative and
`collection:path` imports, and retains file-local aliases and blank import edges.
Its graph reports missing packages, duplicate bindings and cycles with source
locations. Package and document keys use collection-relative paths so relocating
an installation preserves their identities. Graph ordering is deterministic for
the supplied canonical paths.

Resolution is lexical and does not read the filesystem or follow symlinks. A disk
adapter must provide physically canonical roots and paths when containment is
required. Relative imports cannot leave the declaring file's longest matching
collection root. Inputs are bounded to 256 files, 512 KiB each and 8 MiB total;
the public header records path, nesting and import limits.

`authoring_packages_prepare` consumes an owned snapshot and prepares dependencies
before their importers. A file's named aliases expose only the imported package's
functions, types and constants; sibling files do not inherit those aliases.
Blank imports retain dependencies without adding names. A self import refers to
the current package. Exported signatures, layouts and constant values are owned,
and lowered calls link to dependency modules by stable ID. Parameter defaults
must be bounded compile-time values; preparation does not execute host calls to
evaluate defaults. Existing generic execution limits still apply.

Preparation returns a separate valid or invalid candidate, with source-local
diagnostics. Passing a previous candidate permits exact reuse when source
membership/text, native declarations/defaults, backend, nonzero binding
generation and direct dependency generations match. Any changed dependency
invalidates its transitive importers, including blank import edges. Reused
generations and executable modules retain their dependencies independently of
previous owners; a failed candidate never changes the previous one.

Direct semantic callers supply one immutable exports view per imported identity,
reusing that view across aliases. Metadata is available by ID for type checking,
while only explicit export members are visible through an alias. The shared
`script_catalog_snapshot` helper in `Chibi::capabilities` owns metadata names and
defaults, including union payload defaults.

`source_package_snapshot_load` supplies a complete explicit package-source
forest from disk on Linux and macOS. It canonicalizes root directories, reads
bounded regular `.chibi` files through directory descriptors, and rejects
non-hidden descendant symlinks. Hidden descendants are skipped, while explicitly
supplied roots are still scanned. Overlapping roots share no collected files;
longest-root ownership remains the resolver's rule. Loader failures return a
value-owned path/message; source and graph errors retain an invalid snapshot.
The loader does not consult the environment or change cwd. Windows filesystem
loading remains unsupported; callers can still supply owned buffer snapshots.

The SDK installs `core:strings` in `share/chibi/core`, exposed to CMake consumers
as relocatable `Chibi_CORE_SOURCE_DIR`. Callers pass that root explicitly. Its
pure functions use the text and array capability packs. `ByteLength`, `ByteAt`,
`Bytes`, `Slice`, `Compare`, `IsIdentifier` and `BaseName` are the primitives:
byte access returns unsigned 0..255 or -1 outside the string; each `ByteAt`
call measures the string, so a loop over a long one is quadratic, and `Bytes`
returns every byte as an `int[]` in one pass instead; Slice clamps its start/count and owns
its result, byte ranges can split UTF-8 sequences, Compare orders unsigned
bytes, IsIdentifier checks ASCII identifiers, and BaseName uses slash
separators. The rest port Odin's `core:strings` over bytes: `Contains`,
`ContainsAny`, `HasPrefix`, `HasSuffix`, `Index`, `LastIndex`, `IndexByte`,
`LastIndexByte`, `IndexAny`, `LastIndexAny`, `Count`, `TruncateToByte`,
`EqualFold`, `ToLower`, `ToUpper`, `IsAsciiSpace`, `Trim`, `TrimLeft`,
`TrimRight`, `TrimSpace`, `TrimLeftSpace`, `TrimRightSpace`, `TrimPrefix`,
`TrimSuffix`, `Cut`, `Split`, `SplitN`, `SplitAfter`, `SplitAfterN`,
`SplitKeeping` (Odin's `_split`), `SplitLines`, `SplitLinesN`,
`SplitLinesAfter`, `SplitLinesAfterN`, `SplitLinesKeeping`, `Fields`, `Join`,
`Concatenate`, `Repeat`, `Replace`, `ReplaceAll`, `Remove`, `RemoveAll` and
`Partition`, which returns a `Parts` record. Where Odin decodes runes, these
compare, count and offset bytes: an empty separator splits into bytes, `Cut`
takes byte offsets, cutsets are byte sets, and case and spaces are ASCII.
`Repeat` returns an empty string for a count below 1 where Odin panics, and
`Replace` returns only the string. Compiler packages
also use the existing pure generic array capability pack. General authored
generic specialization remains a separate language gap; core does not provide
an imported generic array-wrapper API.

Package globals and embedded game roots remain unsupported. Project loading, formatting and editor
navigation use their existing single-source entry points. The package APIs do
not change the public build command.

### Tests and attributes

An attribute is `@name` before a function declaration, on its own line or the
same one. `@test` is the only attribute; any other name, or `@test` twice, is a
diagnostic. It marks a package-level function that takes no parameters, has no
type parameters and returns nothing:

```chibi
package scoring

Double :: pure fn(points: int) -> int {
    return points * 2;
}

@test
DoublesPoints :: fn() {
    expect(Double(3) == 6, "three doubles to six");
}
```

A test passes when it returns. It fails when `expect(condition, message)` sees
a false condition, which fails the call with `Expectation failed: message` at
the `expect` call's position, or on any other runtime failure, such as an
out-of-bounds read or a checked conversion. `expect` is a core function that is
not `pure`, so a `pure fn` cannot call it and no optimization removes it. Tests
are ordinary functions: they compile into the package like any other and run
only when something calls them.

`chibi test [ROOT]` calls them: it prepares every package below ROOT, runs the
tests of those packages in file and line order, and reports each failure at the
call where it stopped, with its file, line and column. A test runs alone,
without a game: it may call the natives that need none, such as text, array
and math functions, and fails when it waits or starts a task, or after 100
million instructions.

## Declarative documents

Project manifests are `project.kdl`, version 24. Scenes, prototypes, event
declarations, models, shapes, materials, fields, shader libraries, scenarios and
editor configuration use KDL 2. Behaviors, behavior libraries and systems use
readable `.chibi` source. RML and RCSS remain separate markup formats.

```kdl
version 24
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
system FreeMotion(position: Transform, velocity: Velocity)
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
behavior Projectile {
    elapsed := 0;
    lifetime := 3;
    on Tick(dt) {
        elapsed += dt;
        self.Projectile.Age += dt;
        if (elapsed >= lifetime) {
            destroy(self);
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

`::` defines a compile-time constant, function or union type. A constant such as
`BaseDamage :: 12` has no writable instance storage. Its initializer can use literals,
other constants and built-in operators, including constant arrays and reflected
structs; runtime values and function calls are rejected. The compiler evaluates
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
`levelValue := state."level"`. Its execution pins show when the read occurs; its
output retains that value. Choosing the variable as an existing input's source
reads it when that consumer runs. The `state` qualifier identifies the declared
state even when a local parameter has the same name. A newly added **Set** uses
a zero default where one can be written, or the variable's current value for
types such as entities; edit its Value input to choose the intended source.

Names may be identifiers or quoted display names. Prototype references in native
prototype arguments resolve project symbols, such as `spawn(MovementEmitter)`.
Component references resolve reflected symbols. Source member paths use short
component names, such as `Velocity`; exact registered names and keys also resolve.
Reflected fields use source spellings such as `BurstCount` for `burst_count`.
Case and underscore differences are accepted only when the match is unique.
Completion supplies the matching symbol category.
Names with spaces still use quotes; quoted literals also disambiguate a name shadowed
by a local value. Missing, ambiguous and wrong-kind symbols are errors.

Values include booleans, numbers, strings, arrays, sets, maps, nominal handles and
reflected structs. Arrays use `number[]`; collection types use `set<int>` or
`map<int>`. Literal arrays use `[1, 2]`; reflected struct literals use `{x: 1, y: 2}`.
Collection natives such as `array_add` return a new value: write
`hair = array_add(hair, h)`. A pure native called as a statement is an error.
Arrays are values, so every store copies; a local or result array therefore
owns its items. Appending to it with `xs = array_add(xs, x)` or
`xs = append(xs, ys)`, or writing `xs[i] = x`, updates it in place, and building an array that way is linear, as
with Odin's dynamic arrays. A write outside the array fails with "Array index out
of range". Other values holding the array keep their copy.
Runtime strings cannot contain embedded NUL bytes.

Explicit scalar conversions call the type: `double(value)`, `float(value)` or
`byte(value)`. Most code needs none. A literal takes its expected type, an
integer converts implicitly to an integer type that holds every value (`byte` to
`int`, `uint16` to `uint`), and conversions involving `number`, `double` or
`float` stay implicit. Only narrowing between integer types needs a conversion:

```code
low := byte(value)          // checked: fails when value does not fit
bits := byte(wrap: value)   // keeps the low 8 bits: 300 becomes 44
level := byte(clamp: value) // saturates: 300 becomes 255, -5 becomes 0
```

A checked conversion of an integer constant that does not fit is a compile
error; a runtime value that does not fit fails the invocation. `wrap` and
`clamp` apply only between integer types. Floating values convert to integers
by truncating toward zero, clamping at the destination bounds and mapping NaN
to zero. Mixed integer operands, in arithmetic and comparisons, use the wider
type, or `int` or `int64` for a signed and unsigned pair: `byte + int8` is `int`
and `int + uint` is `int64`. `int64` with `uint64` needs an explicit conversion.
Arithmetic narrower than 32 bits, and unsigned ordering and subtraction, are not
yet supported. A constant can declare its type, as in `Mask: uint : 0xFF`. A
conversion to a value's own type, such as `entity(get_global_variable("Player"))`,
only supplies the type a polymorphic call needs.

Vector receivers support `velocity.norm()`, statically resolved to the existing
vec2 normalization operation. `vec2_norm(velocity)` names that operation directly.
Neither form introduces object allocation or virtual dispatch. Normalization retains
the native operation's zero-vector behavior.

A declared numeric result or argument type supplies context for an otherwise
untyped arithmetic expression, so ordinary arithmetic does not require casts.
Typed operands keep their precision: a double
operand also gives otherwise untyped literals double precision before any
conversion to a float destination. Use `double(value)` to request that precision
explicitly. A decimal point alone does not require double precision.

Host events infer their registered payload. `on Tick(dt) { ... }` binds the
registered Tick delta to `dt`; `on Tick { ... }` needs no explicit payload list.
`event.dt` also reads that payload, independently of the general frame clock.
A handler may name its entire payload with `on Tick as tick { ... }`. Event
payload members are read-only unless their reflected signature supplies a reference.

A behavior has an implicit `self: ecs.entity` owner. Component paths read the
owner or another entity handle and follow reflected members:

```code
behavior Emitter {
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
spells `get_variable<"x">()` as `state."x"` and operator functions such as
`add(a, b)` and `less(a, b)` as operators. Each rewrite is kept only when the
program analyzes the same. Awaiting a component accessor does not wait.

Ordinary blocks support assignments, compound assignments, `if`/`else`, `switch`, `while`,
`for (i in first..last)` with inclusive bounds, `foreach (item in array)`,
`break`, `continue` and calls. An ordinary call invokes its native entry and then
continues with the following statement, including entries that emit no outcome.
`await` follows one native `Then` or `Completed` outcome instead; a suspended
capability resumes that continuation when its native wait completes. Waiting suspends
that behavior continuation; the engine thread keeps processing other work. Calls without
one conventional completion produce a diagnostic when awaited. Native sequence
branches use `sequence { branch { ... } branch { ... } }` and run in their native
order. Labels and `goto` express shared or irreducible control flow when structured
blocks cannot describe it.

A typed local's type must begin on the same line as its declaration colon:
`item: Package = {value: 7};`. A complete `name: Type = value` takes precedence
over a label followed by an assignment. Put a label's plain assignment on the
following line to distinguish the two:

```code
ordinary:
result = result + 1;
```

Same-line labels before other statement forms, such as `finish: goto finish;`
or `compound: result += 1;`, remain valid. Inferred `:=` locals and persistent
system-root declarations retain their existing meaning.

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
The new reserved words are `fn`, `union`, `switch`, `is`, `and`, `or`, and `where`.
If a version 23 project uses one as an identifier, rename it and its references
before migrating; the migration does not rename identifiers automatically.

An unawaited call continues before its native completes, so using its data
result is an error. An awaited call with one data result can declare a
block-scoped result:

```code
on Begin {
    emitter := await spawn(MovementEmitter);
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

A function waits when its body contains `await` or `yield`. Calling such a
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
behavior Turret {
    pulse: task
    ready: bool = false

    Charge :: fn(time: double) -> (power: double) {
        await Delay(time)
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
  `Delay`. The start time is recorded before the task's initial synchronous work.
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

Native calls use their reflected names or unique snake-case spellings. Named
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
behavior SharedGate {
    state int visits = 0;
    action latch = gate();
    on Tick() { latch.open(); latch.enter(); }
    after latch.Exit { visits += 1; }
}
```

A named function uses `Name :: fn(parameters) -> result_type { body }`. The `fn`
marker is optional: these definitions have the same signature and behavior:

```code
DamageFor :: fn(level: int) -> number { return level * 2 }
OtherDamage :: (level: int) -> number { return level * 2 }
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
execution continuation; `Name :: pure fn(...)` declares a data-only signature. Calls
use the function name directly. Qualification selects an external library:
`"MovementActions"."Apply Force"(Force: force)`. Reference parameters use `ref`; a call passes any writable value to them directly, such as `nudge(self.Velocity)`, and the function writes through it. There is no address-of operator. Other parameters are the function's own copies, so it may assign them without changing the caller's value.
Qualification has no import aliases or visibility syntax of its own.

Type parameters preserve generic relationships, for example
`Identity :: fn<T: float>(item: T) -> (output: T) { ... }`. A function
gives its results with `return x`, or `return(a, b)` in signature order. A bare `return` gives
each result's current value.

A function without `->` infers its results from its first `return`: `return x` gives one
result named `Result` with the type of `x`, and `return(Hit: h, Distance: d)` gives one result
per label. Later returns must match. A function that never returns a value has no results.
Recursive functions, and functions that call each other in a cycle, declare their results.

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

Additional event scopes use `events "Scope Name" { ... }`. Disabled actions are
explicit source declarations, such as `disabled action gate = multi_gate();`.
Disabling behavior is executable source semantics.

Systems declare their required and optional components in the header. Their
ordinary body runs once per matched entity per tick:

```code
system EmitterControl(emitter: ParticleEmitter, trigger: Aim?) without Collider {
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
`query all(has("Transform"), not(has("Collider")));`.
When both a header and an explicit query are present, the query intersects the
header's required and excluded components. A parameterless system may omit
`()`; its body still has an implicit Tick, including an empty body. Systems
without declared membership use the runtime's inferred component query.

The separate header form, `system Move query position: Transform, velocity:
Velocity where not Frozen { ... }`, supports Boolean membership filters and
relationship-sourced bindings. `join aim: Targets.Transform?` groups a binding
that follows an exclusive relationship; the existing comma-separated binding
has the same meaning. `team: Team from ancestor(ChildOf)` selects the nearest
matching ancestor. Sources also accept `from self` and `from Targets`. Header
filters can test `self.Frozen`, `Targets.Frozen` or
`ancestor(ChildOf).Frozen`; `has(Targets)` tests relationship existence without
repeating the root's body. Access is inferred from the body.

### Shared-target reductions

A synchronous system can contribute to a numeric component field without reading
and writing the destination immediately:

```chibi
system Damage query projectile: Projectile {
    reduce projectile.Target.Health.Damage += projectile.Damage
}
```

```chibi
system Closest query candidate: Candidate {
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
system Damage query projectile: Projectile {
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

System root state belongs to each matched query tuple and persists across ticks.
`shared total := 0;` instead belongs to the system and is shared by its
matched instances. `shared` is rejected in behaviors. Leaving the query releases
the tuple's retained system state; matching again starts from the authored defaults.
Entity generation changes also start fresh state. Suspended system executions
retain their continuation rather than starting a second Tick for that entity.
System publication uses the same matching-state replacement path. Query-managed
rows are retained only when the query generator shape matches; later membership
refresh still releases rows that no longer match. In the preview caller,
`preview_sources_stage` sets restart only for the explicitly requested
`restart_path`; dependent replacements preserve matching state. Explicit restart
resets both instance and shared state. These rules come from
`src/authoring/preview_source_publish.c` and `src/execution/program.c`, not the
retired native application-DLL reload mechanism.

### Named scene entities

The built-in `globals` namespace exposes entities named by authored
`GlobalBinding` components. It is available in behaviors, libraries and systems:

```chibi
behavior CameraFollow {
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
behavior LinkedEmitter {
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

Project manifests use version 24. Executable files contain no generated identity
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

A call passes all of its arguments without copying them, records, strings and
unions included, when the callee and every function it can reach leave storage
they did not allocate untouched. Each loaded definition is checked once:
nothing may write through a value derived from an argument, state or context,
update such a value in place, or write through a reference to it, and the
definition may not store state, send commands, start tasks or suspend. Natives
must declare pure or world-read access. Copying a parameter into a local and
changing the local is allowed, and so is assigning fields of the helper's own
records, text from its arguments included: strings are immutable, so storing
the caller's text leaves the storage the helper's own. Only reading such bytes
back as an address, which no lowering emits, makes the helper copy. A helper
that reads a table therefore costs the same however large the table is.
Debugging keeps the copies.

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
