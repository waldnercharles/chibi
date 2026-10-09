# Asset and behavior source — revision 3

Executable behavior source uses `.chibi`; declarative content remains KDL.
The shared C frontend retains syntax snapshots and resolves typed semantic
records. The compiler lowers those records directly into verified execution
modules; visual editors consume their authoring projections.

## Declarative documents

Project manifests are `project.kdl`, version 13. Scenes, prototypes, event
declarations, models, shapes, materials, fields, shader libraries, scenarios and
editor configuration use KDL 2. Behaviors, behavior libraries and systems use
readable `.chibi` source. RML and RCSS remain separate markup formats.

```kdl
version 13
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
            "GAME.GameProjectile" age=0.0 lifetime=2.0
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
Runtime strings cannot contain embedded NUL bytes. Explicit scalar conversions use
`cast<number>(value)` or `cast<float>(value)`. They remain available at numeric
boundaries; ordinary arithmetic uses its inferred or expected type. A cast to a
value's own type, such as `cast<entity>(get_global_variable("Player"))`, only
supplies the type a polymorphic call needs.

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

A behavior has an implicit `self: ecs.entity` owner. Component paths read the
owner or another entity handle and follow reflected members:

```code
behavior Emitter {
    on Tick {
        self.ParticleEmitter.Active = true;
        self.ParticleEmitter.Rate += event.dt;
        parent := self.ParticleEmitter.Parent;
        pressed := parent.ActionBinding.Pressed;
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
A component path used as a condition tests presence instead: `if self.GameDash`,
`!self.GameDash`, `self.GameDash == nil` and `&&`/`||` operands ask whether the
entity has the component, so `if self.GameDash && self.GameDash.remaining > 0`
reads the field only when it exists.

Ordinary blocks support assignments, compound assignments, `if`/`else`, `match`, `while`,
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
Snapshot node. Later reads use the captured value; a root-level `local`
remains a lazy computation.

### Tasks

A function waits when its body contains `await` or `yield`. Calling such a
function without `await` starts a task: the function runs inside the call until
its first suspension, then runs beside the caller, and the call returns a `task`
handle. `await` on the call or on the handle waits for the function and gives its
result.

```code
behavior Turret {
    pulse: task
    ready: bool = false

    function Charge(time: double) -> (power: double) {
        await Delay(time)
        yield
        return(time * 2)
    }

    on Begin {
        pulse = Charge(1)     // starts a task and continues here
        power := await pulse  // waits for the task's result
        if !pulse {           // a task is true while it is running
            ready = true
        }
        cancel pulse          // stops it and every task it started
    }
}
```

- `yield` resumes on the next update.
- `cancel t` stops a running task and the tasks it started; anything awaiting it
  wakes with default results. Cancelling a finished task does nothing.
- A finished task keeps its results until 32 more of its owner's tasks finish.
  Awaiting it after that is a runtime error, so keep a result you need longer in
  a state.
- A task is true while it is running, so `if t` and `if !t` test it.
- A `task` state needs no default; until assigned it refers to no task.
- Tasks belong to the entity or system registration that started them and stop
  when it is removed. A running task never blocks the events and ticks of its
  owner.
- Calls into a library start tasks the same way, such as `Store.Work()`.
- A `task` state takes the function of the tasks assigned to it, so after
  `pulse = Charge(1)`, `power := await pulse` gives `Charge`'s result. A state
  that holds tasks of different functions, or a task passed in as a plain
  `task`, can be awaited only for completion.
- Awaiting a task whose function has several results gives the same tuple as
  awaiting the call: `(await t).b`.
- A task keeps references to state, components and locals for as long as it
  runs, so `Bump(health)` works. A task shares a local with its caller like a
  closure: each sees the other's writes while the caller runs, and the task
  keeps the local after the caller returns. Each declaration makes a new local,
  so tasks started in a loop each keep their own.

The runtime retains call frames and captured locals across supported native waits
and cancels owned invocations when their entity or system registration is
removed.

Native calls use their reflected names or unique snake-case spellings. Named
arguments follow `name: expression`; ordinary statement calls may use positional
arguments. Explicit types are needed for polymorphic values that reflection
cannot infer. Multiple results use named projections, such as
`nearest.Entity`. No result is selected by guessing the first output.

A root-level `local` names a computation that each activation evaluates once,
when it is first read, and keeps until the activation ends:

```code
local nearest = find_nearest_entity(Owner: self, Radius: 500);
```

A computation that should be recomputed at every read is written where it is
read, or as a function. Assignments belong in executable blocks.

A local is not an eager snapshot. Stateful capabilities shared by several paths
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

Functions declare data parameters and results. Ordinary functions infer their
execution continuation; `pure function` declares a data-only signature. Calls
use the function name directly. Qualification selects an external library:
`"MovementActions"."Apply Force"(Force: force)`. Reference parameters use `ref`; a call passes any writable value to them directly, such as `nudge(self.Velocity)`, and the function writes through it. There is no address-of operator. Other parameters are the function's own copies, so it may assign them without changing the caller's value.
Type parameters preserve generic relationships, for example
`function Identity<T: float>(item: T) -> (output: T) { ... }`. A function
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
system EmitterControl(emitter: ParticleEmitter, trigger: ActionBinding?) without Collider {
    if (trigger) {
        emitter.Active = trigger.Pressed;
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
`query all(has("GAME.GameTransform"), not(has("GAME.GameCollider")));`.
When both a header and an explicit query are present, the query intersects the
header's required and excluded components. A parameterless system may omit
`()`; its body still has an implicit Tick, including an empty body. Systems
without declared membership use the runtime's inferred component query.

### State lifetime and live reload

State belongs to a behavior instance and persists across event executions and
suspension. Task, entity and array state needs no default; it starts empty.
A compatible live update preserves values by stable variable
identity, type, container and reflected layout. A changed default applies to new
instances; restarting applies it to an existing instance. Adding or removing
state, changing its type or losing its identity requires a restart. Updates wait
for active executions to finish. Invalid drafts retain the last valid graph and
runtime generation.

System root state belongs to each matched entity and persists across ticks.
`shared total := 0;` instead belongs to the system and is shared by its
matched entities. `shared` is rejected in behaviors. Leaving the query releases
an entity's retained system state; matching again starts from the authored defaults.
Entity generation changes also start fresh state. Suspended system executions
retain their continuation rather than starting a second Tick for that entity.
Changing a system's source or query currently restarts its registration: pending
continuations are cancelled, and both entity-private and shared state start from
the authored defaults. Compatible behavior instance reload does not preserve
state across this system registration replacement.

### Maintained ECS relationships

A root-level `bind` maintains a writable component field from a deterministic
value expression. A qualified `PressedChanged` handler observes the referenced
ActionBinding rather than the behavior owner's unrelated input:

```code
behavior LinkedEmitter {
    bind self.ParticleEmitter.Active = self.ParticleEmitter.Parent.ActionBinding.Pressed;
    on self.ParticleEmitter.Parent.ActionBinding.PressedChanged(pressed) {
        if (pressed) {
            self.ParticleEmitter.Rate = 10;
        } else {
            self.ParticleEmitter.Rate = 0;
        }
    }
}
```

Bindings are retained relationships, not declaration-time snapshots. The runtime
maintains them at deterministic safe points around input, behavior execution,
structural command commits and system phases. It resolves current entities and
components again at each safe point, including when a parent handle changes.
An unavailable source makes an available destination zero or null; an unavailable
destination remains dormant until it exists again. Initial subscription and
retargeting establish the current event baseline without inventing a transition.
Input delivers queued `PressedChanged(pressed)` edges after its component queries
finish, preserving release and re-press payloads within one update. Other component
writes are observed at maintenance points. Behavior callbacks may suspend using
normal behavior continuations.

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

Project manifests use version 13. Executable files contain no generated identity
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
reroute_data(In: fallback(driver, binding: component("GameMovement"), default: {speed: 0}));
```

`fallback` preserves the input's authored precedence: the connected driver wins, then the binding, then the literal default. Removing the wire restores the binding; removing the binding restores the default. It does not test truthiness or catch runtime errors. The `binding` and `default` clauses are optional, but at least one must be present. A binding with a retained default can use `fallback(binding: speed, default: 0)`; a binding must be writable. Defaults must be compatible literals.
