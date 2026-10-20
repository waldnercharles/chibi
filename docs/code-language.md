# Asset and behavior source, revision 4

Executable behavior source uses `.chibi`. Components, prototypes and
relationships also have authored `.chibi` declarations. Scenes and project
configuration use KDL.

The shared C frontend retains syntax snapshots and resolves typed semantic
records. The compiler lowers those records directly into verified execution
modules. Visual editors consume the authoring projections of those modules.

## Package analysis

### Sibling files

The SDK's `script_analyze_package` and `script_declarations_package` accept a
`script_package_options`. This value describes sibling snapshots, native
declarations and resolved imports. Siblings have one matching `package` header.

Functions, constants, records and unions share a package scope. These functions
collect declarations before function bodies. Thus siblings can refer to one
another, and functions with declared results can recurse across files.

```code
// arithmetic.chibi
package sample
Add :: (value: int) -> int { return value + 2; }
```

```code
// main.chibi
package sample
Main :: (value: int) -> int { return Add(value); }
```

### Visibility

A declaration is private to its package, every file of it, until `pub`
exports it, as in Rust and Zig. Other packages see only `pub` functions,
constants, records and unions, as does a host that calls a package function
by name, such as `chibi doc` calling its generator's `Render`. `chibi test`
still runs private `@test` functions. Using a private declaration through an
import alias reports that it is private.

```code
package geometry
Twice :: (value: int) -> int { return value * 2; }
pub Perimeter :: (side: int) -> int { return Twice(side) * 2; }
```

`pub` comes after attributes and before the name. Library, behavior and
system blocks have no `pub`.

### Analysis inputs

Each input supplies a display path, a stable document key and parsed syntax.
The caller supplies a stable package identity. The returned analysis owns its
source texts, paths and keys.

`script_range.document` selects the input document. Byte offsets and
line/column values stay local to that document. Lowered execution origins
retain the display path of that document after the analysis is freed.

### Stable identities

Exported identities depend on package identity and declaration. They do not
depend on input order or on unrelated sibling files. Byte offsets are not
durable identities. A debugger occurrence names a record by its document, kind
and range counted without whitespace, so whitespace-only formatting keeps it
and every compiler computes the same one.

### Source package snapshots

`source_package_snapshot_new` in `Chibi::source_catalog` separately resolves a
complete set of source buffers against explicit project and collection roots.
The snapshot does these things:

- It owns parsed documents.
- It groups sibling files.
- It resolves relative and `collection:path` imports.
- It retains file-local aliases and blank import edges.

Its graph reports missing packages, duplicate bindings and cycles with source
locations. Graph ordering is deterministic for the supplied canonical paths.

Package and document keys use collection-relative paths. Thus the keys keep
their identities when you relocate an installation.

### Lexical resolution

Resolution is lexical. It does not read the filesystem or follow symlinks.

A disk adapter must provide physically canonical roots and paths when
containment is required. Relative imports cannot leave the declaring file's
longest matching collection root.

### Snapshot input limits

Inputs are bounded to these limits:

- 256 files
- 512 KiB for each file
- 8 MiB total

The public header records the path, nesting and import limits.

### Package preparation

`authoring_packages_prepare` consumes an owned snapshot. It prepares
dependencies before their importers.

Exported signatures, layouts and constant values are owned. Lowered calls link
to dependency modules by stable ID.

Parameter defaults must be bounded compile-time values. Preparation does not
execute host calls to evaluate defaults. Existing generic execution limits
still apply.

### Import aliases

- A file's named aliases expose only the imported package's `pub` functions,
  types and constants.
- Sibling files do not inherit those aliases.
- Blank imports retain dependencies without adding names.
- A self import refers to the current package.

### Candidates and reuse

Preparation returns a separate valid or invalid candidate, with source-local
diagnostics.

You can pass a previous candidate. This permits exact reuse when all of these
items match:

- source membership/text
- native declarations/defaults
- backend
- nonzero binding generation
- direct dependency generations

Any changed dependency invalidates its transitive importers, including blank
import edges.

Reused generations and executable modules retain their dependencies
independently of previous owners. A failed candidate never changes the previous
one.

### Exports views

Direct semantic callers supply one immutable exports view for each imported
identity. They reuse that view across aliases.

Metadata is available by ID for type checking. Only `pub` members are visible
through an alias. The view names the package's private declarations only so a
use can say why it fails.

The shared `script_catalog_snapshot` helper in `Chibi::capabilities` owns
metadata names and defaults, including union payload defaults.

### Loading packages from disk

`source_package_snapshot_load` supplies a complete explicit package-source
forest from disk on Linux and macOS. The loader does these things:

- It canonicalizes root directories.
- It reads bounded regular `.chibi` files through directory descriptors.
- It rejects non-hidden descendant symlinks.
- It skips hidden descendants. It still scans explicitly supplied roots.

Overlapping roots share no collected files. Longest-root ownership remains the
resolver's rule.

The loader does not consult the environment or change cwd.

### Loader errors and platforms

Loader failures return a value-owned path/message. Source and graph errors
retain an invalid snapshot.

Windows filesystem loading remains unsupported. Callers can still supply owned
buffer snapshots.

### The core strings package

The SDK installs `core:strings` in `share/chibi/core`. The SDK exposes this
root to CMake consumers as the relocatable `Chibi_CORE_SOURCE_DIR`. Callers pass
that root explicitly.

The pure functions of `core:strings` use the text and array capability packs.

### String primitives

`ByteLength`, `ByteAt`, `Bytes`, `Slice`, `Compare`, `IsIdentifier` and
`BaseName` are the primitives. These rules apply:

- Byte access returns unsigned 0..255, or -1 outside the string.
- Each `ByteAt` call measures the string. Thus a loop over a long string is
  quadratic. `Bytes` returns every byte as an `int[]` in one pass instead.
- `Slice` clamps its start/count and owns its result.
- Byte ranges can split UTF-8 sequences.
- `Compare` orders unsigned bytes.
- `IsIdentifier` checks ASCII identifiers.
- `BaseName` uses slash separators.

### Odin ports

The other functions port Odin's `core:strings` over bytes:

- `Contains`, `ContainsAny`, `HasPrefix`, `HasSuffix`
- `Index`, `LastIndex`, `IndexByte`, `LastIndexByte`, `IndexAny`,
  `LastIndexAny`, `Count`, `TruncateToByte`
- `EqualFold`, `ToLower`, `ToUpper`, `IsAsciiSpace`
- `Trim`, `TrimLeft`, `TrimRight`, `TrimSpace`, `TrimLeftSpace`,
  `TrimRightSpace`, `TrimPrefix`, `TrimSuffix`
- `Cut`, `Split`, `SplitN`, `SplitAfter`, `SplitAfterN`, `SplitKeeping`
  (Odin's `_split`)
- `SplitLines`, `SplitLinesN`, `SplitLinesAfter`, `SplitLinesAfterN`,
  `SplitLinesKeeping`, `Fields`
- `Join`, `Concatenate`, `Repeat`, `Replace`, `ReplaceAll`, `Remove`,
  `RemoveAll`
- `Partition`, which returns a `Parts` record

### Differences from Odin

Where Odin decodes runes, these functions compare, count and offset bytes:

- An empty separator splits into bytes.
- `Cut` takes byte offsets.
- Cutsets are byte sets.
- Case and spaces are ASCII.

Two more functions differ from Odin:

- `Repeat` returns an empty string for a count below 1. Odin panics for that
  count.
- `Replace` returns only the string.

### Generic array support

Compiler packages also use the existing pure generic array capability pack.
General authored generic specialization remains a separate language gap. Core
does not provide an imported generic array-wrapper API.

### Package API scope

Package globals and embedded game roots remain unsupported. Project loading,
formatting and editor navigation use their existing single-source entry points.
The package APIs do not change the public build command.

## Tests and attributes

### The test attribute

An attribute is `@name` before a function declaration. It can be on its own
line or on the same line as the declaration.

`@test` is the only attribute. Any other name is a diagnostic. `@test` twice is
also a diagnostic.

`@test` marks a package-level function that takes no parameters and returns
nothing:

```chibi
package scoring

Double :: (points: int) -> int {
    return points * 2;
}

@test
DoublesPoints :: () {
    assert(Double(3) == 6, "three doubles to six");
}
```

### Test results

A test passes when it returns. A test fails in these conditions:

- `assert(condition, message)` sees a false condition. This fails the call
  with `Assertion failed: message` at the `assert` call's position.
- Any other runtime failure occurs, such as an out-of-bounds read or a checked
  conversion.

Without a message, `assert` reports the condition's source text instead, such
as `Assertion failed: n >= 0`, as Odin's `assert` does. The message must be a
string constant.

`assert` is pure. Thus a function that calls it stays pure, and no
optimization removes the call. When the compiler evaluates a constant, a false
`assert` is a compile error.

Tests are ordinary functions. They compile into the package like any other
function. They run only when something calls them.

### Running tests

`chibi test [ROOT]` calls the tests. It does these things:

- It prepares every package below ROOT.
- It runs the tests of those packages in file and line order.
- It reports each failure at the call where it stopped, with its file, line and
  column.

A test runs alone, without a game. It may call the natives that need no game,
such as text, array and math functions.

A test fails when it waits or starts a task. It also fails after 100 million
instructions.

### Language contract snapshot

The versioned contract snapshot in the repository is
`docs/language-contracts/v1.md`. At its recorded revision, it pins these items
separately:

- source evidence
- existing C conformance cases
- unresolved migration semantics

That recorded revision is before the version-25 retained-binding migration. The
source audit of the snapshot is not fresh runtime or cross-backend validation.

## Declarative documents

### Document formats

Chibi uses these document formats:

- Project manifests are `project.kdl` files at version 29.
- Scenes, KDL prototypes, event declarations, models, shapes, materials,
  fields, shader libraries, scenarios and editor configuration use KDL 2.
- Behaviors, behavior libraries and systems use readable `.chibi` source.
- RML and RCSS stay separate markup formats.

This example is a project manifest:

```kdl
version 29
name "My Game"
content "content"
start_scene "scenes/Main.kdl"
```

### KDL structure

KDL documents contain named fields, objects and arrays. Objects use named
child fields or properties.

- `(array)` keeps arrays distinct from scalars and objects. This includes
  empty arrays and one-item arrays.
- `(object)` marks an empty object.
- `#true`, `#false` and `#null` are the Boolean and null literals.

The shared value codec keeps the meanings of native numeric values and
reflected values unchanged.

This example is a KDL document:

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

### Parameter and mesh payloads

Reflected string fields that contain shader or material parameter objects or
mesh part arrays also use KDL value syntax. An object payload contains its
named fields, such as `Radius 25`. An array payload uses `(array)-` with
values or item nodes.

Escape the payload text when you embed it in another KDL document. These
payloads are declarative configuration. They are not executable behavior
source.

### Asset declaration headers

Authored asset declarations use the same name-first headers as executable
source:

- `Health :: component { ... }`
- `Enemy :: prototype { ... }`
- `Targets :: relationship { ... }`

A composed prototype names its bases in order, for example
`Elite :: prototype(Enemy, Armored) { ... }`.

The repository's `docs/runtime-authoring.md` covers sparse defaults,
instance-local links, relationship traits and typed pair edits.

## Executable source

### Statements and lines

Source uses these statement and line rules:

- A semicolon is optional at the end of a line and before a closing brace.
- Use semicolons to separate statements on the same line.
- Parenthesized calls, arrays and struct literals may span lines.
- An operator at the end or at the start of a line continues the expression.

### Source formatting

`chibi_ctl --format` writes the shortest spelling that analyzes to the same
program. It removes these items:

- semicolons
- argument labels before the first skipped parameter
- literal arguments equal to their defaults
- inferable declaration and argument types
- redundant parentheses and casts
- handler aliases (the payload is `event`)
- empty `else` blocks
- `-> ()` and `return()`

It also writes `return(x)` as `return x`.

The formatter keeps each rewrite only when the re-analyzed program does not
change. When source does not analyze, the formatter only re-indents it.

The formatter keeps parentheses that group `&&` under `||`, or one bitwise
operator under another. It writes ranges as `0..n - 1`.

### Named arguments

A named call argument uses the form `Value: expression`. An explicit argument
type includes its value or source, for example `Value: float = amount` or
`Source: ref int from count`. A bare identifier after `:` is a value
expression.

### Systems and component files

This example is a system:

```code
FreeMotion :: system(position: Transform, velocity: Velocity)
    without Collider, Body
{
    position += velocity * dt
}
```

A file under `content/components` declares one component instead. It declares
the fields of the component and their types, ranges and units.

Scripts check those units. They also check the units of engine fields and the
seconds of `dt`. Proven unit mismatches are compile errors. Malformed
referenced unit metadata is also a compile error.

For more information, see "Declare components" in `runtime-authoring.md`.

### Readable programs

A behavior, library or system is a readable program. Its declarations and
statements resolve into an owned, typed semantic program. Direct lowering
produces an immutable, verified execution module. Backend code and live
instances have separate lifetimes.

Source does not require graph node, pin or connection records. Library calls
resolve against the project catalog. Native calls resolve against registered
engine capabilities. Unknown names and incompatible types are errors.

This example is a behavior:

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

### Behavior state

Declarations at the behavior root own persistent instance state.
`elapsed := 0;` infers the type `number`. A `number` has double-precision
storage, so later assignments can contain fractions.

Boolean, string and homogeneous array defaults also infer their type. Empty
arrays and nominal values require an explicit type.

`elapsed: number = 0;` gives the type explicitly. Native storage types such as
`float` and `int64` are still available when precision or a reflected
interface needs them. The formatter keeps a declared type unless inference
produces the same storage.

### Retained bindings

An explicit retained binding puts its lifetime after the name and colon:

```code
visits: retain int = 0
elapsed: retain = 0
```

`retain` selects private persistent storage that belongs to the enclosing
behavior attachment or system registration. This also applies inside named
functions. These rules apply to retained storage:

- A call to a helper does not create another retained owner.
- All matches of one system share its retained values. A second registration
  has independent values.
- Imported library state inherits the consuming owner. It keeps its own
  module namespace.
- `retain = value` infers the type of the value with the same rules as `:=`.
- Behavior and library root declarations, such as `elapsed := 0`, are also
  retained.
- There is no `retain(system)` form.

The formatter may remove an explicit root `retain` when the analyzed program
stays the same. The `retain` of a callable and system ownership remain
meaningful.

### System invocation locals

An ordinary declaration in a system body is local to its invocation. Each
query match has independent locals. These locals survive `await` and `yield`
until the invocation finishes.

Use this storage for each kind of state:

| State | Storage |
| --- | --- |
| Temporary coroutine progress | Invocation locals |
| Longer-lived entity state | Components |
| Private owner-wide state | Retained bindings |

System-retained values cannot contain task handles. Keep those handles local
to the invocation instead.

Helpers and handlers cannot capture system invocation locals. Pass those
locals to them as parameters.

### Retention migration

Historical project upgrades keep behavior retention. They convert old
system-owned `shared` or `retain(system)` bindings to plain `retain`.

A change of spelling cannot convert old per-match system state. Migration
stops with a diagnostic so that the author can put durable entity state in
components, or temporary progress in coroutine locals.

The project version remains 26 because no checked-in project uses the
affected system declarations. Hand-made projects already at candidate version
26 must review system `retain` declarations manually. They must also remove
any old `(system)` suffix. Rebuild prepared code after upgrading.

The old `state` and `shared` prefixes are no longer declaration syntax. The
`state.name` expression still selects a persistent binding when a local
shadows its name.

### Compile-time constants

`::` defines a compile-time constant, function or union type. A constant such
as `BaseDamage :: 12` has no writable instance storage.

The initializer of a constant can use literals, other constants and built-in
operators. This includes constant arrays and reflected structs. Ordinary
procedures can also run in compile-time contexts when all their inputs and
operations are available there.

Runtime dependencies are errors. The compiler evaluates these expressions once
when it builds the module.

`:=` still infers a mutable value. At program scope, this value is persistent
state. Inside an executable block, it is a local.

### Graph variable operations

The graph derives variable operations from these declarations. If you declare
`level: entity` and assign `level = globals.Level`, the graph produces a
**Set Level** node. This node has one entity-valued **Value** input.

**Set Level** stores the named scene entity in the `level` variable of this
behavior instance. The assignment does not change scenes. It does not rewrite
the global binding.

The same mechanism produces **Set Score** from `score = 10`. No native node
registration or C code is necessary.

### Variables in Studio

To declare a variable in Studio:

1. Add the variable in **Members**.
2. In **Properties**, edit its **Name**, **Type** and default.

The node chooser offers the **Get** and **Set** operations of the variable.
Drag the variable onto the graph to create a Get. Alt-drag it to create a Set.
These edits update the same source and use normal undo.

### Get and Set nodes

When you add a standalone variable **Get**, it creates a named local read,
such as `levelValue := state.level`. Its execution pins show when the read
occurs. Its output keeps that value.

When you choose the variable as the source of an existing input, the variable
is read when that consumer runs. The `state` qualifier identifies the declared
state, even when a local parameter has the same name.

A newly added **Set** uses a zero default where one can be written. For types
such as entities, it uses the current value of the variable. Edit its Value
input to choose the intended source.

### Authored functions

An authored function is the reusable way to create a custom operation. Its
parameters become inputs, and its results become outputs. A call uses the same
definition wherever it appears.

Small pure calls can be folded into the input of their consumer. This does not
change the source or the number of evaluations.

- Use **Open Function** on a call or its folded input to inspect the
  definition.
- **Inline Function into Caller** replaces a supported call with its body in
  the source, as one undoable edit. The definition remains available for other
  calls.
- **Fold into Input** and **Show Input Nodes** only change presentation.

### Identifiers and typed references

Language declarations, bindings and member names use identifiers. Quotes mark
string values. This includes user-facing text and literal union alternatives.

Prototype, component and relationship symbols produce typed reference values:

```code
asset: Prototype = MovementEmitter
component: Component = Health
relation: Relationship = Targets
Forward :: (value: Prototype) -> Prototype { return value }
emitter := await spawn_prototype(Forward(asset))
```

Locals, parameters and results keep the reference category. These rules
apply:

- A string that contains `"MovementEmitter"` is not a prototype reference.
- A component reference cannot be passed where a prototype is required.
- Missing, ambiguous and wrong-kind symbols are errors.

Source member paths use registered component and field spellings. Completion
supplies the available names.

### Value types

Values include booleans, numbers, strings, arrays, sets, maps, nominal handles
and reflected structs. Runtime strings cannot contain embedded NUL bytes.

| Form | Example |
| --- | --- |
| Array type | `number[]` |
| Collection types | `set<int>`, `Map<string, int>` |
| Array literal | `[1, 2]` |
| Reflected struct literal | `{x: 1, y: 2}` |

### Collection natives

Collection natives such as `array_add` return a new value. Assign the result,
as in `hair = array_add(hair, h)`. A pure native called as a statement is an
error.

### Array copies

Arrays are values, so every store copies. Thus a local or result array owns
its items. These writes update a local or result array in place:

- appending with `xs = array_add(xs, x)`
- appending with `xs = append(xs, ys)`
- writing `xs[i] = x`

Building an array this way is linear, as with Odin's dynamic arrays. A write
outside the array fails with "Array index out of range". Other values that
hold the array keep their copy.

### Character literals

A character literal holds exactly one Unicode scalar. Examples are `'a'`,
`'\n'` and `'🐈'`. It takes the escapes that a string literal takes, plus
`\'`.

Like an untyped integer, a character literal takes an integer type from
context when its value fits:

- `ByteAt(path, i) == '/'` compares ints.
- `c: byte = 'é'` holds 233.

Otherwise the literal is a `char`. A `char` is one Unicode scalar in 32 bits,
as Odin's `rune` and Rust's `char` are.

A `char` has integer arithmetic and comparisons. Thus `digit - '0'` is a
`char`. A `char` converts implicitly to `int`, `uint`, `int64` and `uint64`.
`c + n` with an `int` `n` is an `int`, and with a `uint` or `uint64` `n` it has
that type.

`char(n)` converts an integer back. It fails the call with "Value does not fit
in char" when `n` is not a Unicode scalar. `char` arithmetic whose result is
not a Unicode scalar fails in the same way.

### Scalar conversions

An explicit scalar conversion calls the type: `double(value)`, `float(value)`
or `byte(value)`. These conversions are implicit:

- A literal takes its expected type.
- An integer converts implicitly to an integer type that holds every value
  (`byte` to `int`, `uint16` to `uint`).
- An integer converts implicitly to `number`, `double` or `float`, and the
  floating types convert to one another.

A floating value becomes an integer only through a conversion, `int(x)`, as
in Go, Odin and Rust. Narrowing between integer types also needs one:

```code
low := byte(value)          // checked: fails when value does not fit
bits := byte(wrap: value)   // keeps the low 8 bits: 300 becomes 44
level := byte(clamp: value) // saturates: 300 becomes 255, -5 becomes 0
```

### Untyped literals

A literal without a type from context takes one from its spelling, as in Go,
Odin and C#. An integer such as `0` or `0xFF` is an `int`, or the first of
`int64` and `uint64` that holds it. A decimal point or an exponent makes a
`number`: `0.0`, `1e3`.

```code
count := 0       // int
elapsed := 0.0   // number
half := 1 / 2    // 0: integer division
ratio := 1 / 2.0 // 0.5
```

So `elapsed := 0` followed by `elapsed += dt` is an error; write
`elapsed := 0.0`. Beside a typed operand an untyped literal takes that
operand's type, so `count * 0.5` with an `int` `count` is an error and
`number(count) * 0.5` is not. A union with one numeric alternative, such as
`number | Cancelled`, gives a literal that alternative's type.

### Conversion rules

A floating literal is the `double` nearest to it. A `float` literal is the
`float` nearest to that `double`. A literal that overflows is a compile error.
A nonzero literal that underflows to zero or a subnormal is also a compile
error.

A checked conversion of an integer constant that does not fit is a compile
error. A runtime value that does not fit fails the invocation. `wrap` and
`clamp` apply only between integer types.

A floating value converts to an integer with these rules:

- The conversion truncates toward zero.
- It clamps at the destination bounds.
- It maps NaN to zero.

### Integer arithmetic

In arithmetic and comparisons, mixed integer operands use the wider type. A
signed and unsigned pair uses `int` or `int64`. For example, `byte + int8` is
`int`, and `int + uint` is `int64`. `int64` with `uint64` needs an explicit
conversion.

Every integer type has every arithmetic and comparison operator. Unsigned
types compare as unsigned values. Integer arithmetic wraps on overflow:
`byte(3) - byte(200)` is 59.

### Explicit types

A constant can declare its type, as in `Mask: uint : 0xFF`. A conversion to a
value's own type, such as `entity(get_global_variable("Player"))`, only
supplies the type that a polymorphic call needs.

## Array storage

### Packed element types

The runtime automatically packs eligible array elements into contiguous
fixed-width storage. These element types are eligible:

- booleans
- signed and unsigned integers
- F32/F64 values
- enums
- handles
- plain fixed-layout records, including vectors

A record is eligible only when every field, including nested records, has a
validated fixed layout and needs no managed storage.

### Boxed elements

Boxed storage applies in these cases:

- Strings, names, managed record fields and nested collections use boxed
  elements.
- Arrays that contain null record elements also stay boxed.
- Mutable native builders may remain boxed until an owned snapshot is made.

### What packing changes

Packing changes the physical storage. It does not change element order,
indexing, value types, copy ownership or iteration semantics.

Inspection and persistence support both layouts.

Maps use separate associative storage. They are not packed arrays.

### Numeric-loop check

The focused numeric-loop check compares boxed and packed inputs for ten numeric
types. It uses 64 elements and 16 trials per representation. It runs through
the VM and the available compiled backend, native SLJIT or Wasm.

The check makes sure that the results are equal. It reports payload bytes and
elapsed time. This small workload does not establish a universal speedup.

## Maps and iteration

### Map types

Maps keep separate key and value types. An empty literal needs a concrete
contextual type. A complete type can be explicit or inferred from a nonempty
initializer:

```code
scores: Map<string, int> = {"Ada": 3, "Bo": 5}
inferred: Map = {"Ada": 3}
more := {"Ada": 3}
empty: Map<string, int> = {}
scores["Ada"] = 4
if map_has(scores, "Bo") { score := scores["Bo"] }
scores = map_remove(scores, "Bo")
for name,score in scores { record_score(name, score) }
```

### Map lookup and updates

`map_has` guards a lookup. A missing key fails the lookup. The lookup does not
synthesize a zero value.

Assignment inserts or replaces a value. Replacement keeps the position of the
key. Removal followed by reinsertion appends the key.

### Map order and equality

Map iteration takes a snapshot in insertion order. Map equality compares
associations independently of order. Dynamic duplicate keys keep the last value
at the first position.

### Map keys and values

Keys must be supported finite scalars, strings, names, enums or handles. Nested
maps and arrays keep their complete value types.

### Array and string iteration

Array iteration binds the item first and the optional zero-based index second.
String iteration yields one string per Unicode scalar, not one byte or one
grapheme cluster. Both take a value snapshot before they enter the loop:

```code
for item,index in items { total += item * index }
for character in "Aé🐈" { append_character(character) }
for item: ref in items { item += 1 }
```

### Reference iteration

Reference iteration writes through to a mutable owned array binding. Other
bindings copied from the array keep their snapshots.

Reference iteration has these rules:

- Inside the loop, code cannot access or replace the borrowed source directly.
  Use `item`.
- The loop body cannot suspend, start tasks, perform opaque host operations or
  call mutating procedures.
- `break` and `continue` preserve writes that the loop already performed.

This form rejects temporary arrays, reference parameters, maps, strings and
paired item/index bindings.

The implementation stores logical references. It resolves each write against
the source binding. Source code never receives a raw element pointer.

### Vector receivers

Vector receivers support `velocity.norm()`. It resolves statically to the
existing vec2 normalization operation. `vec2_norm(velocity)` names that
operation directly.

Neither form introduces object allocation or virtual dispatch. Normalization
keeps the zero-vector behavior of the native operation.

### Numeric context

A declared numeric result type or argument type supplies context for an
otherwise untyped arithmetic expression. Thus ordinary arithmetic does not
require casts.

Typed operands keep their precision. A double operand also gives double
precision to otherwise untyped literals, before any conversion to a float
destination. To request that precision explicitly, use `double(value)`. A
decimal point alone does not require double precision.

### Host event payloads

A handler names the payload fields that it reads, with their types:

```code
on Tick :: (dt: s) { ... }
on ContactBegin :: (other: entity) { ... }
on Begin :: () { ... }
```

- Each name selects one payload field. The match ignores case, `_`, spaces
  and `-`, so `other` reads the field `Other`.
- List any subset of the fields, in any order. List each field only once.
- Write the type of each field. A written unit must match the field's unit.
- A payload parameter takes no default, source or `$`.
- A handler that reads no field writes `()`.
- The handler cannot read the fields that it does not list, and it cannot read
  `event`.

Tick's payload is `dt`, the time in seconds since the last frame.

These older forms still work until a project-format migration rewrites them:

- `on Tick(dt) { ... }` binds the payload fields in order.
- `on Tick { ... }` binds each field by its own name.
- `event.dt` reads a field of the payload.
- `on Tick as tick { ... }` names the whole payload.

Event payload members are read-only unless their reflected signature supplies a
reference.

### Invocation owner

A behavior, library or system has an implicit invocation owner,
`self: entity`. Library calls use the owner of their caller.

### Component paths

Component paths read the owner or another entity handle and follow reflected
members:

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
strings, casts or a dynamic property interpreter.

### Component path rules

- Assignment and compound assignment target a writable reflected component
  field on the exact addressed entity.
- Reads can traverse nested reflected structs. Assignment to a nested struct
  subfield is currently rejected.
- Component and field names that cannot be resolved produce source diagnostics.
- Prototype membership improves completion ranking. It does not guarantee that
  a component is present on a live entity.

### Component presence conditions

A component path used as a condition tests presence instead. These forms ask
whether the entity has the component:

- `if self.Dash`
- `!self.Dash`
- `self.Dash == nil`
- `&&`/`||` operands

Thus `if self.Dash && self.Dash.remaining > 0` reads the field only when it
exists.

### Formatter path rewrites

`chibi_ctl --format` spells reflected accessor calls as paths. It also
respells supported state reads and operator functions, as the table shows:

| Source | Formatted |
| --- | --- |
| `get_dash().Value` | `self.Dash` |
| `has_dash()` in a condition | `self.Dash` |
| A statement `set_dash_remaining(v)` | `self.Dash.remaining = v` |
| Supported state reads | `state.x` |
| Operator functions such as `add(a, b)` and `less(a, b)` | Operators |

The formatter keeps each rewrite only when the program analyzes the same.

Awaiting a component accessor does not wait.

### Block statements

Ordinary blocks support these statements:

- assignments
- compound assignments
- `if`/`else`
- `switch`
- `while`
- `for i in first..last` with inclusive bounds
- `for i in first..<last`, which stops before `last`
- `for item in array`
- `for <`, which counts a range or an array down: `for < i in 0..<n` visits
  `n - 1` down to 0, and `for < item, i in items` visits the last item first,
  with its own index
- `break`
- `continue`
- calls

Ranges never count down by themselves: a range whose first bound is above its
last runs no iterations, and one whose constant bounds descend, such as
`10..0`, is a compile error. Neither form wraps at the `int` bounds. `for <`
does not reverse a `Map` or a string.

### Ordinary and awaited calls

An ordinary call invokes its native entry and then continues with the following
statement. This is also true for entries that emit no outcome.

`await` follows one native `Then` or `Completed` outcome instead. A suspended
capability resumes that continuation when its native wait completes.

Waiting suspends that behavior continuation. The engine thread continues to
process other work. Calls without one conventional completion produce a
diagnostic when they are awaited.

### Sequence branches

Native sequence branches use `sequence { branch { ... } branch { ... } }`. They
run in their native order.

### Labels and typed locals

Labels and `goto` express shared or irreducible control flow when structured
blocks cannot describe it.

The type of a typed local must begin on the same line as its declaration colon,
for example `item: Package = {value: 7};`. A complete `name: Type = value`
takes precedence over a label followed by an assignment. To distinguish the
two, put the plain assignment of a label on the following line:

```code
ordinary:
result = result + 1;
```

Same-line labels before other statement forms remain valid, for example
`finish: goto finish;` or `compound: result += 1;`. Inferred `:=` locals and
persistent system-root declarations keep their existing meaning.

### Switch statements

A `switch` evaluates its value once and executes the matching case. Cases never
fall through. Thus they do not need `break`. `case:` is the default.

The switch keeps its outer braces. Braces around each case body are optional:

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

### Parameter reads by name

Project version 28 reads parameters by name. A callable reads its parameter `X`
as `X`.

The migration from version 27 rewrites reads as follows:

- The version 27 spelling `entry.X` migrates to `X`.
- When `X` is not a parameter, `entry` meant the only parameter `p` of the
  callable. Thus a bare `entry` migrates to `p`, and `entry.x` migrates to
  `p.x`.

The migration refuses a read if a local, constant, loop, handler or pattern
binding of the same name would capture it. Rename that binding first.

### Handler payload names

A handler without a parameter list binds the names of its payload. If a payload
name is also a parameter of the function around the handler, a bare read of
that name is an error. The reason is that the payload would hide the function's
parameter.

To give the payload's parameters other names, list them, as in
`after spawned(made) { ... }`.

### Function bodies

Project version 29 runs the statements of a function when it is called. A
function cannot have an `after entry` handler. Macros keep their `after entry`
handlers.

The migration from version 28 moves the statements of each `after entry`
handler in a function. They go after the function's own statements and before
the handlers that follow them.

The handler read the arguments as the caller passed them. Thus the migration
refuses these cases:

- The handler reads a parameter that the function's statements assign.
- The handler reads a parameter that the function's statements pass to a
  function or macro with a `ref` parameter.
- The handler reads `event` or names its payload.
- The handler declares a name that the function also uses.
- The function is on one line and has other statements.

Rewrite these cases by hand before you migrate.

### Keyword migrations

Project version 24 migrates existing `function` declarations and `match`
statements to the current forms. Those old keywords are no longer runtime syntax.
Version 26 removes the named `fn`, `pure` and `foreach` spellings.

Current reserved words include `union`, `switch`, `is`, `and`, `or`, and
`where`. If a version 23 project uses one as an identifier, rename it and its
references before you migrate. The migration does not rename identifiers
automatically.

### Awaited results

An unawaited call continues before its native completes. Thus it is an error to
use its data result. An awaited call with one data result can declare a
block-scoped result:

```code
on Begin {
    emitter := await spawn_prototype(MovementEmitter);
        emitter.ParticleEmitter.Parent = self;
}
```

The name becomes available after completion and remains inside its lexical
block. Capturing a shared action instance is rejected, because later
invocations could replace its output. Scopes with explicit labels cannot
declare awaited results, because a jump could bypass their initialization.

### Block snapshot declarations

An ordinary block declaration such as `x := calculation;` evaluates once at
that execution point. It uses a native Snapshot node. Later reads use the
captured value.

## Unions and patterns

### Union declarations

Named tagged unions declare alternatives with optional owned payload fields.
Literal unions restrict a value to the listed strings. Type alternatives use
`|`.

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

### Type tests and narrowing

`is` and `is not` test types or union alternatives without converting values.
Matching branches narrow the original symbol. The opposite branch removes the
tested alternatives.

A terminating branch also permits the surviving path to keep its proof.
Assignment or calls that may change the value invalidate that proof. Pattern
bindings belong to the matching arm or positive condition.

### Union coverage and initialization

- Switches over finite unions must cover every alternative or include `case:`.
- Union declarations require a value.
- Union function results must be initialized on every return path.
- Guard an optional union before you switch over it.

### Optional extraction

`as T` safely extracts `T?`. A mismatch is `nil`. A match preserves zero,
false, and empty text as present values. `as T` does not perform numeric
conversion.

An optional condition tests presence. Inside that branch, the value has type
`T`.

### Union storage

Union values keep immutable snapshots of their payloads in execution-owned
storage. Trivial payload fields live inline in the tagged record. Creating that
record uses arena storage. `Cancelled` is a shared payload-free singleton.

### Where unions are allowed

Union values can be local values, parameters, results and behavior state. ECS
component fields currently reject unions, because ECS storage does not own
their managed payloads.

Recursive owned union payload types are rejected. For references to
independently owned work or entities, use task or entity identities.

## Tasks

### Starting a task

A function waits when its body contains a retained `await` or `yield`. If the
compiler proves that a component await is synchronous, that await does not make
the function wait.

A call to such a function without `await` starts a task. The task runs inside
the call until its first suspension. Then it runs beside the caller, and the
call returns a task handle.

An `await` on a call gives the result of the call directly.

### Cancelled results

An `await` on a `task<T>` handle gives `T | Cancelled`. The successful
alternative contains the original result. This includes zero, false and empty
strings.

`Cancelled` is a distinct singleton. It is never a default result. It is
reserved for cancellation, and it cannot appear in the successful result type
of a task.

`Cancelled` is also a semantic builtin, so a bare declaration cannot reuse its
name. Qualified names such as `Outcome.Cancelled` remain legal. Component fields
named `Cancelled` also remain legal. This restriction does not make `Cancelled`
a grammar keyword.

This example starts a task, schedules its cancellation and awaits its result:

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

### Yield and cancel

- `yield` resumes on the next update.
- `cancel t` stops a running task and the tasks that it started. Awaiters
  receive `Cancelled` and can continue.
- Cancelling the awaited target does not by itself cancel the awaiting caller.

### Cancellation deadlines

`cancel t after n` schedules cancellation without waiting. These rules apply:

- The deadline is the original start time of the task plus `n`. `n` uses the
  same game-clock seconds as `delay`.
- The start time is recorded before the initial synchronous work of the task.
- Negative or NaN durations count as zero.
- Positive infinity sets no finite deadline.
- Repeated deadlines keep the earliest.
- An overdue pending task stops at a safe point. Effects that it already
  performed remain.
- Completion removes the deadline.
- Cancelling a completed task leaves its successful result intact.

### Task result lifetime

A completed result remains available while a live handle or waiter can observe
it within its owner. Copied handles name the same task.

Awaited values have their own snapshots. They remain valid after the task
handle is dropped.

Locals stop retaining results when their scope exits. This includes `break`,
`continue` and jumps to an enclosing scope. References that other tasks hold
keep their values.

### Unobserved results and collection

Dropping the last handle does not cancel a running task.

Unobserved completed results are reclaimed at owner safe points. Thus repeated
fire-and-forget work does not accumulate results each frame.

Collection visits these values:

- live frames
- typed value banks
- state
- captured locals
- nested result values

Task-slot storage is reused up to the owner's peak number of simultaneously
retained tasks.

Collection indexes live tasks once. Then it resolves each handle in logarithmic
time. It visits chains of task results iteratively. It skips primitive
collections, because their elements cannot contain task handles.

### Testing a running task

A task is true while it is running, so `if t` and `if !t` test it.

An unassigned `task` state names no task. An `await` on it gives `Cancelled`.

### Task ownership

Handles remain bound to the entity or system registration that started them.
If you copy a handle to another owner, that owner does not get access to the
task.

Tasks stop when their owner is removed. A running task never blocks the events
and ticks of its owner.

Calls into a library start tasks in the same way, for example `Store.Work()`.

### Task result types

An explicit `task<T>` annotation declares the successful result type. A `task`
state infers its successful result from the functions assigned to it.

A state that holds different function signatures can be awaited only for
completion. A plain `task` parameter can also be awaited only for completion.

Multiple function results form the successful tuple alternative. Use narrowing
to remove cancellation before you access fields:
`r := await t; if r is not Cancelled { use(r.b) }`.

### Captured locals

Tasks retain references to state, components and locals. A captured local is
shared with its caller while the caller runs. It remains alive after the caller
returns.

Each declaration creates a new local. This includes declarations inside loops.

The runtime retains call frames and captured locals across supported native
waits. It cancels owned invocations when their entity or system registration is
removed.

### Native call arguments

Native calls use their registered source names. Named arguments follow
`name: expression`. Ordinary statement calls may use positional arguments.

Polymorphic values that reflection cannot infer need explicit types.

Multiple results use named projections, for example `nearest.Entity`. No result
is selected by guessing the first output.

### Block-local values

A local declared inside an executable block evaluates its initializer at that
statement. It retains the value for that activation:

```code
nearest := find_nearest_entity(Owner: self, Radius: 500)
```

If a computation should be recomputed, write it where it is used or as a
function. Assignments belong in executable blocks.

### Shared action instances

Stateful capabilities that several paths share have one named action instance.
Methods invoke its named entries. Handlers consume its named outcomes.

The instance keeps its state across method calls and events. An invocation runs
any emitted outcome handler before its next statement.

Action completion handlers use `after action.Outcome`. For a single
conventional completion, they use `after action`. `on` handles events.

For example, the same handler can open a gate and enter it:

```code
SharedGate :: behavior {
    visits: retain int = 0;
    action latch = gate();
    on Tick() { latch.open(); latch.enter(); }
    after latch.Exit { visits += 1; }
}
```

### Named functions

A named function uses `Name :: (parameters) -> result_type { body }`. An
optional `=>` can come before its brace body:

```code
DamageFor :: (level: int) -> number { return level * 2 }
OtherDamage :: (level: int) -> number => { return level * 2 }
```

A single result may be named with `-> (Damage: number)`. Multiple results keep
that parenthesized form.

The current callable model does not support anonymous function values or
closures.

### Units in function signatures

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

Known standalone unit names and compound units use `number` storage. An
explicit numeric storage type can come before the unit, as in `float u/s` or
`number charge`.

Unknown bare type names remain errors. Use the explicit form for a custom
single dimension.

Units are checked at calls, defaults, returns and reference writes. This
includes named multiple results and imported signatures. Native callable
metadata can supply the same contract.

Incompatible known dimensions are compile errors. Signature edits that change
only units invalidate the affected compiled callers.

Unannotated parameters and results remain dimensionally unknown. This does not
infer unit-polymorphic signatures. It also does not permit cyclic library
publication. Numeric inference and runtime representation stay unchanged.

### Effects and purity

Functions declare data parameters and results. Ordinary functions infer their
execution continuation and effects.

Purity is inferred through local and imported calls. This includes recursion,
writes and suspension. Starting a task is an effect.

Query filters also reject reads of retained or per-invocation values. This
applies even through an imported helper.

### Calls and qualification

Calls use the function name directly. Qualification selects an external
library, for example `MovementActions.ApplyForce(force)`.

Qualification has no import aliases or visibility syntax of its own.

### Function reference parameters

Reference parameters use `ref`. A call passes a writable value directly, for
example `Nudge(self.Velocity)`. The function writes through it. There is no
address-of operator.

Other parameters are copies. They can change without changing the caller.

### Call-depth budget

All authored functions use the shared call-depth budget of 64. Source metadata
cannot override that limit. It also cannot declare a custom owner type.

Descriptions belong in comments, not in executable metadata declarations.

### Generic type parameters

Argument-bound type parameters preserve generic relationships, for example
`Identity :: (item: $T) -> T { return item }`. Later uses of `T` must match the
bound type.

`$N: int` binds a compile-time value argument.

### Return statements

A function gives its results with `return x`, or with `return(a, b)` in
signature order. A bare `return` gives the current value of each result.

A function with results must end in a terminating statement, as in Go and
Odin. Otherwise the compiler reports "Missing return" at the closing brace.
These statements terminate:

- a `return` or a `goto`;
- an `if` with an `else`, when both branches terminate;
- a `switch` with a default arm, or over every alternative of a union, when
  every arm terminates;
- a `while (true)` that no `break` leaves.

A function with an `after` handler that returns is exempt: it may return the
results after the body ends.

### Inferred results

A function without `->` infers its results from its first `return`:

- `return x` gives one result named `Result`, with the type of `x`.
- `return(Hit: h, Distance: d)` gives one result per label.

Later returns must match. A function that never returns a value has no
results.

Recursive functions declare their results. Functions that call each other in a
cycle also declare their results.

## Compile-time values and constructed types

### Compile-time parameters

`$T` binds a type from a runtime argument. `$N: int` binds a value that must be
known at compilation.

Specialization keeps concrete storage types. It reuses the same instantiation
for identical bindings:

```code
Identity :: (value: $T) -> T { return value }
Scale :: (value: $T, $N: int) -> T { return value * N }
Factorial :: (n: int) -> int {
    if n <= 1 { return 1 }
    return n * Factorial(n - 1)
}
Size :: Factorial(5)
```

### Compile-time execution

An ordinary procedure runs in a compile-time context when its inputs and body
are available there. It needs no `pure` or `comptime` marker.

Runtime storage, host effects and unsupported operations cannot enter that
evaluation.

The evaluator supports these items:

- scalar and type values
- authored calls
- local variables
- assignment
- `if` and `while`

Compile-time execution does not support arrays, `for` loops, closures or native
calls.

### Compile-time limits

- Each evaluation has a 65,536-step budget.
- Each evaluation has a 128-call depth limit. This limit is separate from the
  runtime call-depth limit of 64.
- Expression nesting is limited to 256 levels.
- Statement nesting is limited to 256 levels.

### Library specializations

Qualified library calls also specialize generic functions and construct types,
for example `Units.Quantity(double, 1, -1)`.

Specializations are materialized in the defining library. They use the source
ranges and lexical bindings of that library. Their bodies are not copied into
the analysis of the caller.

Nested imported requests resolve before executable generations are published.
A failed specialization does not publish a callable generation.

Body edits rebuild dependent runtime callers. Changed compile-time results or
constructed layouts invalidate cached analysis as semantic catalog changes.

### Constructed record types

Libraries can construct record types from compile-time type and value
parameters. For example, dimensions can live in type arguments, while the
runtime value stores only the numeric payload:

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
`Quantity(double, 1, 0)`. The resulting record stores one double. Dimension
exponents are compile-time metadata.

The procedure constructs the return type with ordinary type operations. This is
separate from the existing erased unit annotations on native and component
fields, described above.

### Functions from generated expressions

When several graph calls share a pure generated expression body, formatting
emits one ordinary pure function and calls it from each original site.

Typed functions also preserve expressions whose internal precision or capture
types would be lost in a shorter formula.

An action with a consumed data result can use an ordinary function to retain
its result type. The operations, parameters, results and input references
remain executable code.

Edits to that function now own its body. The old generated formula no longer
regenerates it automatically.

A generated action may mark its caller capture as writable. This can occur even
though its function entry takes a value snapshot and its body writes a named
state directly. The ordinary function preserves that value parameter and
concrete state write. The generated caller-only reference flag is no longer
retained.

### Macros

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

### Event scopes and disabled actions

Additional event scopes use `events ScopeName { ... }`.

Disabled actions are explicit source declarations, for example
`disabled action gate = multi_gate();`. Disabling behavior is executable source
semantics.

### System headers

Systems declare their required and optional components in the header. Their
ordinary body runs once per matched entity per tick. A system names the step
it reads after its query clauses, as a handler names its payload fields:

```code
EmitterControl :: system(emitter: ParticleEmitter, trigger: Aim?) without Collider
    on Tick :: (dt: s) {
    if (trigger) {
        emitter.Active = trigger.Firing;
    }
    emitter.Rate += dt;
}
```

- A system runs on Tick, so the clause names Tick and lists the fields that it
  reads. Leave the clause out to read none.
- Only the system's body reads the step. Pass it to the functions that the
  body calls.
- A system cannot read `event`.

### Required, optional and excluded components

- Required parameters determine query membership.
- `?` makes a component optional.
- `without` excludes the listed components.

A component cannot appear in both the parameter list and `without`. This
includes an optional parameter.

### Optional component guards

Optional members require a presence guard. `if (trigger)`, `if (!trigger)`,
`trigger != nil` and `trigger == nil` select the branch in which the component
is present.

Reading an optional member outside that branch is a compile error.

An `await` invalidates the presence proof. Guard the component again before you
use it after suspension.

Presence checks lower to native Has Component operations. They do not test the
value of the component.

### Explicit system queries

An explicit query declaration also supports `all`, `any`, `not` and `has`, for
example `query all(has(Transform), not(has(Collider)));`.

When both a header and an explicit query are present, the query intersects the
required and excluded components of the header.

A parameterless system must write `()`. Its body still has an implicit Tick.
This includes an empty body.

Systems without declared membership use the inferred component query of the
runtime.

### Relationship bindings and header filters

The header form `Move :: system(position: Transform, velocity: Velocity)
where not Frozen { ... }` supports Boolean membership filters and
relationship-sourced bindings.

`join aim: Targets.Transform?` groups a binding that follows an exclusive
relationship. The existing comma-separated binding has the same meaning.

`team: Team from ancestor(ChildOf)` selects the nearest matching ancestor.
Sources also accept `from self` and `from Targets`.

Header filters can test `self.Frozen`, `Targets.Frozen` or
`ancestor(ChildOf).Frozen`. `has(Targets)` tests relationship existence without
repeating the body of the root.

Access is inferred from the body.

## Shared-target reductions

### Reduce statements

A synchronous system can contribute to a numeric component field. It does not
read and write the destination immediately:

```chibi
Damage :: system(projectile: Projectile) {
    reduce projectile.Target.Health.Damage += projectile.Damage
}
```

```chibi
Closest :: system(candidate: Candidate) {
    reduce min(candidate.Target.Threat.Distance, candidate.Distance)
}
```

The destination must be a writable numeric ECS component field. Contributions
are evaluated when the statement runs. They are published after the system
batch finishes and before the next dependent system prepares its query.

### Producer restrictions

A producer cannot ordinarily read or write a component that it reduces. This
rule also applies to:

- reachable helpers
- bindings
- value/change filters

Component-presence tests are allowed. The restriction lets separate systems
calculate contributions to shared targets concurrently.

### Parallel reduction rows

Eligible reduction rows can also execute on workers. Their private
contributions are merged in row order.

For this, the runtime requires:

- sufficient rows/workers
- streaming execution
- verified reduction-only effects

Ordinary writes, observation, inspection or profiling retain the serial
fallback. See `execution_program_run_system_parallel` in
`src/execution/program.c` and its caller in `src/runtime/behaviors/source.c`.

### Ordered replay

Ordinary `reduce` replays contributions in stable system/query order. The
replay includes:

- integer wrapping at the destination width
- clamping of each bounded-field update
- normal floating-point rounding

Min/max use the existing comparison-and-select rules. The incoming value wins
ties and unordered NaN comparisons. Units are checked as for assignment.

### Failed systems

A failed system discards its contributions. Earlier successful systems stay
published. The contributions of later peers are discarded. This is not a
rollback of ordinary writes.

### Paused and canceled dispatches

Debug pauses retain the unfinished reduction dispatch and resume it before
dependent work proceeds. Reloading a paused system cancels its unfinished log.

A scheduling change while paused restarts the current system phase with fresh
dispatches. This includes systems already run in that phase. Completed earlier
engine phases stay complete.

Scene reset and engine teardown also cancel held dispatches.

### Unordered sums

Regrouped sums are opt-in. Select them when the changed numeric behavior is
acceptable:

```chibi
Damage :: system(projectile: Projectile) {
    reduce unordered projectile.Target.Health.Damage += projectile.Damage
}
```

`unordered` combines contributions per target within each system. Then it adds
the subtotal to the field. This reduces log entries and publication work when
many rows share targets.

### Unordered sum limits

`unordered` can change floating-point rounding, NaNs and signed zero. For this
reason, use ordinary reductions for calculations that need the default
reproducible order. Integer addition still wraps at its declared width.

These rules also apply:

- Unordered sums require fields without a stored range contract. Clamping once
  would differ from clamping each update.
- A producer cannot mix unordered sums with ordered reductions on the same
  component.
- Min/max remain ordered.
- Publication occurs once per aggregated target field. Thus its write-revision
  count differs from the ordered form.

### Reduction memory and cost

Reduction logs and prepared query rows use temporary memory proportional to the
work. Idle buffers have bounded retention. The retention limit does not limit
valid query size.

Reductions avoid conflicting direct writes, but they do not guarantee a
speedup. Small workloads can cost more to collect and publish than a serial
ordinary update.

## State lifetime and live reload

### Behavior instance state

State belongs to a behavior instance. It persists across event executions and
suspension. Task, entity and array state needs no default. It starts empty.

### Matching state on publication

A nonrestart publication preserves matching state. It matches state by:

- stable variable identity
- sharedness
- scalar storage
- container
- nominal type name

New or nonmatching state starts from its new default. Removed state disappears.
A changed default does not replace a matched value unless explicitly restarted.

This state matcher does not compare reflected layout hashes. Installed-schema
preflight is a separate check. It is not a promise of arbitrary
reflected-layout migration.

### Publication timing

Publication occurs outside active dispatch. It cancels the old continuations
and tasks, and replaces the program. It does not wait for suspended work to
finish. Invalid drafts retain the applied source/runtime generation.

### Retained system state

Explicit system `retain` belongs to the registration. It persists across
completed invocations. It also survives a period with no query matches.

Removing or re-adding a match does not reset it. Imported library retention
follows the same owner.

### Invocation-local declarations

Ordinary system-body declarations are invocation-local. Each complete query
tuple has independent local variables and suspended work. It has no private
retained bindings.

Removing a structural tuple cancels that work and discards its locals. A new
match starts a fresh invocation. Entity generations participate in tuple
identity.

### Suspended system executions

Suspended system executions resume their continuation. They do not start a
second invocation for that tuple. A completed invocation may start again on a
later eligible update.

A value predicate that becomes false does not itself cancel already suspended
work. Check changing gameplay conditions inside the coroutine.

### System publication and restart

System publication uses the same matching-state replacement path. Query-managed
rows are retained only when the query generator shape matches. Later membership
refresh still releases rows that no longer match.

In the preview caller, `preview_sources_stage` sets restart only for the
explicitly requested `restart_path`. In the preview caller, dependent
replacements preserve matching state. Explicit restart resets retained state
for the owner.

Components keep their independent ECS lifetime. They are not reset merely
because a system query stops matching.

These rules come from `src/authoring/preview_source_publish.c` and
`src/execution/program.c`. They do not come from the retired native
application-DLL reload mechanism.

## Named scene entities

### The globals namespace

The built-in `globals` namespace exposes entities named by authored
`GlobalBinding` components. It is available in behaviors, libraries and
systems:

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

### Global name discovery

Names are discovered from the project's prototypes and scenes before Play.
`globals.Player` has type `entity`. It needs no string lookup or cast.

Completion after `globals.` lists the declared names. An unknown name is a
source error. Definition, references and rename connect a member to its
authored bindings.

A name may be reused in different scenes. Two entities cannot publish the same
name in one running scene.

### Global name resolution

Each read resolves the binding in the current scene. A declared name absent
from that scene is a runtime error.

Assigning the result to a behavior variable, as above, captures that handle.
It does not maintain a live binding. It does not retarget the variable when
the scene changes.

### Read-only global members

Namespace members are read-only references. Assign to an entity's writable
component, such as `globals.Player.Transform`. Do not assign to
`globals.Player`.

The namespace itself is not a value. These bindings do not declare shared
scalar variables or persistent save data.

## Input actions

### The input map

A project's input map is `content/input.kdl`. It names:

- each action
- the Control labels that hold the action
- whether the action also holds the Aim of input owners

```kdl
Boost {
    (array)sources "Shift" "Pad Left Shoulder"
}
Fire {
    (array)sources "Mouse Left" "Pad Right Trigger"
    firing #true
}
```

### Action names and events

A map holds at most 256 actions. Each action is an event. Entities with Input
receive it when one of its sources goes down.

For this reason, an action cannot take the name of a host event such as
`Tick`. Also, no other global may be named `input`.

A project event may share an action's name. Pressing the action raises that
event.

### Reading actions in scripts

Scripts read an action through the `input` global:

- `input.Boost.held` is true while any of its sources is down.
- `input.Boost.pressed` is true for the one fixed step after one of its
  sources went down. This is also true for a tap released before that step.

Action names are typed members. Thus `input.Bost.held` is a compile error.
Completion after `input.` lists the map's actions.

A pause or focus change keeps held actions. Thus resuming with the same
controls held presses nothing again.

### Migration from version 15

Migrating a version 15 project moves its ActionBinding components into the
map. The migration omits disabled and retained ones.

The action that a script's `ActionBinding` or `PressedChanged` meant depended
on its entity. Thus a script that still uses one stops the migration. The
migration stays stopped until the script is rewritten with `input.<Action>`.

An existing `content/input.kdl` that is not an input map also stops the
migration. The migration stays stopped until the file is fixed or moved.

## Maintained ECS relationships

### Root-level bindings

A root-level `bind` maintains a writable component field from a deterministic
value expression:

```code
LinkedEmitter :: behavior {
    bind self.ParticleEmitter.Active = input.Boost.held;
}
```

### Maintenance safe points

Bindings are retained relationships, not declaration-time snapshots. The
runtime maintains them at deterministic safe points around:

- input
- behavior execution
- structural command commits
- system phases

At each safe point, the runtime resolves current entities and components
again. This includes when a parent handle changes.

### Unavailable sources and destinations

An unavailable source makes an available destination zero or null. An
unavailable destination remains dormant until it exists again.

### Event baselines and callbacks

Initial subscription and retargeting establish the current event baseline.
They do not invent a transition. Component writes are observed at maintenance
points. Behavior callbacks may suspend with normal behavior continuations.

### Relationships in systems

A system maintains relationships for its matched entities.

While an entity's system invocation is active or suspended, relationship values
and subscription baselines still update. Change callbacks are dropped during
that time, without accumulating a backlog.

A suspended change callback becomes that entity's single system invocation.
Tick does not run concurrently with it. These callbacks use the existing
coroutine runtime. They do not introduce a separate scheduler.

Losing query membership removes that entity's registrations along with its
retained state.

### Binding expression rules

Reactive declarations belong to behavior or system roots. They do not belong
to callable libraries or nested function bodies.

A binding expression cannot:

- assign
- suspend
- use randomness
- invoke a capability with side effects

These checks include named computations and callable bodies.

### Binding conflicts and order

Within an asset, duplicate binding destinations and dependency cycles are
rejected. Within an asset, bindings run in dependency order.

At each world maintenance epoch, ownership checks also reject identical or
overlapping destination fields across behavior and system managers for the
same entity generation.

Cross-asset dependency cycles are not analyzed globally. Unchanged destination
values are not written.

### Relationship implementation

The compiler and execution runtime implement the relationship.

The authoring graph projects the resolved binding and its editable value and
target. Compiler-generated maintenance work is not an independent editable
graph operation.

## Authoring state and reconciliation

### Source is the whole asset

Project manifests use version 29. Executable files contain no generated
identity annotations or embedded authoring footer, and no file beside a
`.chibi` source describes it. Descriptions are comments in the source. Graphs
lay out automatically when they open.

### Editor state

Editors keep in-session layout, comments, watches and breakpoints as editor
state. A snapshot of it binds to the exact source text and semantic origin
identities. Projects never store it. A stale snapshot is ignored. It cannot
supply executable values or connections.

Matching ambiguity during an edit does not invalidate executable code.
Uncertain identities are replaced. Runtime state compatibility decides whether
a restart is required.

### Source generations and graph views

For each generation, source collections retain:

- syntax
- resolved facts
- diagnostics
- executable modules

Graph views retain a presentation of those facts. Graph edits produce source
edits, followed by analysis and preparation. Source edits refresh the
projection. Invalid drafts preserve the applied generation.

### Shared semantic APIs

Highlighting, completion, diagnostics, hover, definition, references, rename and
call hierarchy consume shared semantic APIs outside Studio.
`docs/authoring-navigation.md` records their UI availability and verification.

### Completion and indentation

Completion shows source-style signatures. It prioritizes:

- names in scope
- receiver members
- values compatible with the current argument

Regular and Vim editing preserve indentation on new lines and indent inside
brace blocks.

### Fallback inputs

A connected input can retain a binding or literal for later disconnection:

```text
reroute_data(In: fallback(driver, binding: component("Movement"), default: {speed: 0}));
```

`fallback` preserves the input's authored precedence. The connected driver
wins, then the binding, then the literal default.

Removing the wire restores the binding. Removing the binding restores the
default. `fallback` does not test truthiness or catch runtime errors.

These rules apply:

- The `binding` and `default` clauses are optional, but at least one must be
  present.
- A binding with a retained default can use
  `fallback(binding: speed, default: 0)`.
- A binding must be writable.
- Defaults must be compatible literals.

## Automatic performance diagnostics

### Performance diagnostics

Ordinary code needs no `hot` or `noalloc` annotation. Successful source
preparation exposes nonfatal `authoring_source.performance` diagnostics for:

- allocation
- copying
- values retained beyond their producing expression or frame

### Cost analysis APIs

The shared `behavior_function_costs` API analyzes finalized normal
instructions. It follows linked callees, including library bodies.
`execution_module_costs` also includes entry points, binding bodies, and
imports.

Evidence retains the originating function, instruction, capability, and source
location.

### Proven and possible costs

“Proven” means the generated body contains the identified operation. A
conditional branch may never execute it.

An allocation is a dynamic storage request. An arena or pool may satisfy it
without a fresh heap allocation. Scalar moves do not count as managed copies.

“Possible” covers:

- nullable payloads
- cache misses
- runtime callbacks
- native code without a verified cost contract

Pure native functions are not assumed allocation-free. Folded constants and
scalar arithmetic carry no such body costs.

### Summary scope and budgets

These summaries do not estimate elapsed time, operation counts, or algorithmic
complexity. They exclude backend compilation and initial activation setup.

Analysis is bounded to 1,024 definitions/modules and 200,000 instructions.
Exceeding a budget returns conservative possible costs and an incomplete
summary.

### Inspection code

Normal native, Wasm and bytecode bodies omit source watch and pause operations.
Native and Wasm inspection code is compiled on first use. It is cached for the
retained definition.

Concurrent native first-use inspection can execute bytecode until compilation
publishes the complete code and resume map. Pauses and watches preserve the
canonical instruction locations.

Detaching inspection after a debug pause keeps that saved continuation
canonical until its next real wait or completion. This preserves values, but
that continuation is temporarily not optimized.

### Scalar union elimination

Normal VM and compiled bodies also remove these operations for nonescaping
unions of plain scalars:

- construction
- snapshots
- tag/payload operations

This includes nested unions and aliases whose incoming values agree at forward
control-flow joins.

Isolated native calls with only plain scalar inputs and outputs preserve
unrelated virtual values. The calls, side effects and errors still execute.

These cases retain their owning representation:

- different values merged at a join
- backedges
- managed payloads
- escaped values
- authored calls
- context callbacks
- suspension

Analysis has a bounded scratch budget. It conservatively retains allocations
when it cannot prove safety. Inspection retains the canonical operations.

### Numeric interval analysis

Numeric interval analysis removes proven redundant clamps and tests. It can
compare original integers when widening them to `number` was exact.

It preserves floating-point rounding, NaN and signed-zero behavior. `n := 0`
still infers `number`. Generic component reads do not acquire range proofs from
metadata alone.

### Borrowed array traversals

Read-only plain-array traversals can borrow their existing storage during a
synchronous invocation. This includes loops that update unrelated scalar state.

The compiler rejects borrowing when:

- addresses escape
- collection storage can change
- raw header or pointer identity is observed
- an opaque helper runs
- execution can suspend

Runtime guards check the actual array and element headers. They exclude
overlap with writable context, state, result, host-value, saved activation and
VM scratch banks.

### Borrowed and copied arrays

Matching arrays avoid snapshot allocations and element copies. Checking their
headers still takes linear time.

Other inputs retain the canonical deep copy and its errors. This includes mixed
element headers supplied by native callers.

Debug inspection retains owned snapshots. Prepared modules recompute the proof
instead of trusting saved optimization flags. Purity alone does not establish
that a helper cannot mutate an aliased collection.

### Read-only helper views

The same proof follows synchronous authored helper chains. Eligible array
parameters become internal call-scoped read-only views automatically. No new
parameter syntax or author annotation is required.

The callee cannot do these things with a view:

- mutate
- return
- retain
- expose collection identity
- start tasks
- suspend

Unsupported or recursive call chains keep owning copies.

### Helper view runtime checks

Runtime checks cover actual argument headers, ancestor scratch and bound
library state. They recognize only the engine's exact safe callbacks. Custom
callbacks and debugging keep snapshots.

Generated C conservatively retains caller snapshots around helper calls. Its
shared call driver can still borrow verified helper arguments.

These restrictions govern when the optimization applies. When it cannot apply,
ordinary array helpers retain their existing value semantics.

### Arguments without copies

A call passes all of its arguments without copying them, records, strings and
unions included. This occurs when the callee and every function it can reach
leave storage they did not allocate untouched.

Each loaded definition is checked once. In it, nothing may:

- write through a value derived from an argument, state or context
- update such a value in place
- write through a reference to such a value

The definition may not store state, send commands, start tasks or suspend.
Natives must declare pure or world-read access.

### Allowed helper writes

Copying a parameter into a local and changing the local is allowed. So is
assigning fields of the helper's own records, text from its arguments included.

Strings are immutable. Thus storing the caller's text leaves the storage the
helper's own. Only reading such bytes back as an address makes the helper copy.
No lowering emits that read.

Therefore a helper that reads a table costs the same however large the table
is. Debugging keeps the copies.

### Incremental source preparation

Preview source preparation reuses unchanged analysis and executable
generations through `authoring_sources_prepare_incremental`. Exact source
identity/text, semantic catalog metadata/defaults, backend and native binding
generation must match.

Editing a library body rebuilds its transitive callers. Generations own their
data independently of older collections. Declaration discovery and parsing
still run.

Default comparison evidence is capped at 1 MiB and depth 64. Unsupported
defaults, managed struct pointers or padding can conservatively prevent reuse.

Custom binding providers must supply a nonzero immutable generation to opt
into reuse. This is an in-process cache, with no persistent artifacts.

### World string interning

World-owned strings use a hash table with exact text comparison. Allocations
stay stable until world teardown. A null input interns as an empty string.

Lookup has an expected constant probe count at a bounded load factor.
Adversarial collisions can still require a linear scan.

This structural improvement and the removed instrumentation work are not a
measured speedup claim.
