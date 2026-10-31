# Authored package compiler

`tools/compiler/package_compiler` is the stage-one compiler as it grows. It
compiles packages' source files to a prepared closure, entirely in Chibi
after the raw Tree-sitter parser: the source snapshot and package graph, the
declarations, typing, lowering and the prepared writer. Stage zero still
builds the compiler itself; stage one is this compiler once it compiles the
compiler sources.

## Entry

`Compile(project: string, collection_names: string[], collection_paths:
string[], paths: string[], sources: string[], debug: bool, target: Target,
abi: u64, names: string[], natives: Native[], ports: Port[], identities:
u64[]) -> (module: u64, bytes: u8[], error: syntax.Diagnostic?)` returns
ordinary named outputs. Success returns a root module ID, a CHIBICLO closure
with a module for every package, and nil error. Failure returns module ID zero,
empty bytes and a source diagnostic. The optional error requires acknowledgment
under the [recoverable-error convention](recoverable-errors.md):
`syntax.Diagnostic` is an `@error` struct, and checking is inferred from the
result type. Functions do not carry a separate marker. `paths` and
`sources` hold the files of every package, those of imported collections such
as `core` under their collection's path; the package of `paths[0]` is the
closure's root. With `debug`, each function has stage zero's debugger points
and watches (see [Output](#output)); without it, none, as the bootstrap
compiles the compiler. The target supplies what Chibi cannot see:

- `Target`: pointer width, the offsets of a value header's and an array's
  members, and the failure kinds a host call and a failed assertion report;
- the prepared header ABI (`execution_prepared_abi`);
- the instruction names in ordinal order
  (`execution_prepared_instruction_name`), which the compiler resolves by name;
- the native functions it links, as its catalog describes them: each name,
  its ports' names, storage and container, and its capability ID and ABI. A
  port of storage 0 takes the type the call infers, one for all such ports of
  a call, as a generic port does; -2 takes each argument's own type, as a
  wildcard port such as `format_text`'s values does; -1 marks one the
  compiler cannot pass. A variadic native's last parameter takes any number
  of arguments. The test builds these tables from the compiler host's packs.

`CompileClosure` takes the same arguments and returns only the closure's
bytes, or none when `Compile` rejects the input, for a host that takes one
`u8[]` result (`compiler_host_invoke_bytes`).

The twelve-argument package entries require a verified pure native catalog.
`CompileWithEffects` and `CompileClosureWithEffects` add a thirteenth argument,
`native_effects: int[]`, with one validated mask per native. `CompileWithEffects`
returns the same three named outputs as `Compile`; its closure wrapper still
returns only bytes. Hosts that expose process, file or state operations must
supply their actual masks.

## Supported language

Package `Compile` specializes exported generic functions for inferred concrete
types and builtin numeric compile-time values. Boolean compile-time arguments
currently accept literals and bound constants. Named functions and capture-free inline
functions can bind synchronous callable parameters with zero or one result;
those parameters disappear from the runtime signature. Definitions retain their
own private-name scope, while callbacks and foreign nominal types can place a
specialization in the consuming module. The numeric scalar-facts route uses the
same specializer. See [passing functions](code-language.md#passing-functions) for
the initial admission rules.

These additions do not extend general compile-time string/type-function
execution or the separate legacy `CompileBehaviors` source route. That route
still needs mixed package/source collection and import integration before it can
consume package algorithms. Shared scalar-facts helper checks do not establish
material or GPU compilation.

Packages of functions, records, unions and constants, and the packages they
import:

- `int`, `bool`, `string`, `i8`, `u8`, `i16`, `u16`, `u32`, `i64`, `u64`,
  `number` and `f64` values, records of those, unions whose variants carry
  any of them, and arrays of the non-floating ones, as parameters, results
  and locals; a `number` beside an `f64` becomes one, and
  a number stored in a floating field converts as stage zero converts it;
  string literals decode as JSON strings, as stage zero reads them;
- parameter defaults that are integer, bool or string literals, `(name:
  string = "you", count: int = 1)`: a call that omits trailing arguments
  passes their defaults, as stage zero does. A floating default is rejected;
- Unqualified `assert(condition, "message")` is a checked language intrinsic,
  even when the target has no native `assert` capability. An authored callable
  of that name takes precedence; qualified calls resolve normally. The intrinsic
  fails when the condition is false, with
  "Assertion failed: " and the message, or the condition's source when there
  is none or it is empty, as stage zero's `ASSERT` does. Its arguments may be
  named `condition` and `message`; the message must be a string literal.
  `Target`'s `assert_failure` is that failure's kind. Stage zero compiles an
  assert without a condition to one that always fails; stage one refuses it;
- named results, `Split :: (value: number) -> (whole: int, rest: number)`: each
  is a writable binding initialized as its type requires. `return (a, b)`
  assigns each value in order, then returns them; a bare `return` returns
  their current values. A single named result also accepts `return value`
  and its call yields that value directly. A call's `.name` reads a named
  result, directly or through a local. The projection has the declared result
  type, with checked numeric conversion when a local widened the value.
  A named result may be a number, bool, struct, string, array or optional
  such as `syntax.Diagnostic?`. Structs use owned, typed storage and retain
  their field bytes when returned to the caller. Strings, arrays and optional
  payloads retain their ordinary value lifetimes. A local function-result bank
  owns independent output cells and permits direct writes such as `r.value = 5`.
  Whole-bank assignment, native result-bank writes and nested result paths keep
  their existing stage-one restrictions;
- integer literals, decimal or hexadecimal, typed as stage zero types them: by
  the declared local, assignment target, result, parameter, field, payload,
  element or index, or by the other operand. A literal whose type nothing
  gives, as in `x := 5`, is an `int` in stage zero, and stage one rejects it.
  A literal outside its type, such as `u8` 256, is rejected. A decimal at an
  integer type is read exactly, digit by digit, in both stages: `2.0` and
  `1e3` are the integers they spell, `9007199254740993.0` at `i64` keeps its
  last unit, and `2.0000000000000000001` is rejected as a fraction. Stage zero
  used to read it as a long double first, so whether a long fraction or a
  unit past 2^53 survived depended on the platform;
- `T(value)`, `T(wrap: value)` and `T(clamp: value)` for the integer types,
  of an integer or a floating value: the checked form fails the call when the
  value does not fit, `wrap` keeps its low bits and `clamp` saturates. Mixed
  integer operands widen to the type that holds both, and local
  declarations, assignments to locals and returns widen implicitly; `int` or
  `i64` with `u64` needs a conversion. An integer operand of a floating
  one, a named result or a component field becomes floating as stage zero
  converts it: exactly to an `f64`, then to a `number`. A literal where a
  floating value is expected is one;
- local declarations, assignment and compound assignment; `if`/`else`,
  `while`, `break`, `continue` and `return`; `for` over an `int` range,
  `first..last` or `first..<last`, and over an array, `for x in xs` or
  `for x, i in xs` with its index, each counting down with `for <`, as stage
  zero counts them;
- every integer arithmetic, bitwise and comparison operator on every integer
  type, unsigned ordering and division included, as stage zero computes
  them; negation only on the signed types. String equality, and `!`, `&&` and `||` with short-circuit evaluation;
- struct and array literals, where the type is known from the declared local,
  the result or the parameter. With no type expected, as for `xs := [a, b]` or
  an argument of a native such as `resize([int(0)], n)`, an array literal's
  first item gives its element type, as in stage zero; field reads;
  bounds-checked element reads;
- element assignment, compound included, and `xs = append(xs, x)` and
  `xs = extend(xs, ys)` on a local array. The native is recognized by its
  capability identity. In-place lowering requires a resolved proof that the
  item has no WRITE effect, including calls it makes; READ and other non-WRITE
  effects do not prevent it. Unknown native metadata cannot establish that
  proof. The ordinary call preserves source argument order when the item may
  write. Proven updates read/watch the receiver before the item and reacquire
  its writable header afterward;
- field assignment, compound included, on a local record: `x.f = v` writes as
  stage zero writes it, through the reference make, field and write
  intrinsics, which copy the record with the new field, so no other value
  shares the old storage, and copy a string into it. A narrower integer
  value converts to the field's type. A record or array parameter whose
  value, fields or elements the function writes is copied into a cell of its
  own on entry, so the caller's value never changes; the others are read in
  place. For-loop elements, like pattern bindings, cannot be assigned;
- union values `U.V` and `U.V(a, b)`, with one positional value per payload
  field, and `switch` over a union with `case .V:`, `case .V(a, b):` binding
  every payload field, and a default `case:`. Bindings are immutable. As in
  stage zero, a switch without a default arm must cover every variant, and a
  repeated variant or default arm is rejected. A `switch` over an integer
  compares it with one value per arm, `case 61:`, in order, and runs the
  default arm after them all, wherever it is written;
- `value is .V` and `value is not .V`, and `value is .V(a, b)` binding the
  payload in an if or while condition, alone or joined by `&&`, as stage zero
  allows bindings. They last through the consequence or loop body, not the
  else branch;
- calls between the package's functions, including recursion, and calls of
  natives, such as `length`, `slice` and `format_text`. A native's generic
  ports take the one type its arguments give them, its wildcard ports each
  argument's own type, and a variadic native's last parameter any number of
  arguments; natives are matched by name as the catalog matches names. A
  native with several results gives a value whose `.name` reads one result,
  directly or through a local: `decoded := native(text); decoded.valid`;
- integer constants, typed (`Mask: i64 : 1 << 40;`) or untyped, which
  are `int` (`Width :: Unit * 4 + 1;`), whose value folds from literals, other
  constants of the package in any order or of an import, parentheses and the
  integer operators. Every intermediate value must fit the constant's type,
  so stage zero's wrapping never shows, and a u64 constant is a literal. A
  use is the value itself; a local hides a constant of the same name;
- imports of `project:` and collection packages such as `core:strings`, and
  relative imports such as `"../source_tokenizer"`, which name a directory
  below the importing package's and may not leave its root: a package that
  imports its siblings relatively compiles under any root, as the stage-one
  parser does in `chibi fmt` from a `compiler` collection. `alias.F(...)` calls
  an imported function, `alias.C` reads an imported
  constant, and `alias.T` names an imported record or union in types,
  literals and `alias.U.V` variants. Imported declarations must be `pub`.
  Private helpers remain usable throughout their own package, including across
  files, and a public function may return a value of a private type without
  exposing that type's name. A local hides an alias of the same name.

Privacy applies to source imports. Prepared modules retain private callable
entries just as stage zero does; package privacy is not a host-invocation
security boundary. Private union constructor accesses receive a specific
private-name diagnostic here, where stage zero currently reports an unknown
callable or value. Both reject the source.

Anything else is rejected with its source position. Implicit widening of call arguments,
element stores, literal
members, payloads and in-place natives, constants of other types, constant
arrays and constants computed by calls, type patterns, record field defaults, fields of array elements, named
payload arguments and literal variants come next. A literal passed to a
wildcard port, as in `format_text("{}", 1)`, is an `int` in stage zero, and
stage one rejects it. One check stage zero makes at compile time happens at
run time here: a checked conversion of a named constant that does not fit
fails the call.
Locals follow stage zero's scopes: blocks, switch arms, whose statements form
a block, and if and while statements, which hold their condition's bindings.
A name declared twice in one scope, such as `x := 1; x := 2;` or `.Pair(a, a)`,
is a duplicate declaration, and an inner scope may hide an outer name. One
difference remains, and only for programs stage zero rejects: stage zero
declares a local for its whole scope, so `n := n + 1` with a parameter `n` is
a cyclic dependency there, while stage one reads the parameter.

## Behaviors and executable libraries

`CompileBehavior(identity: string, source: string, target: Target, abi: u64,
names: string[], natives: Native[], ports: Port[], native_ids: u64[],
game_events: GameEvent[], event_fields: GameEventField[], types: GameType[],
type_fields: GameField[], entity: u64, operators: NumericOperator[],
globals: GameGlobal[], makes: NativeMake[], make_fields: int[])
-> (module: u64, bytes: u8[], error: syntax.Diagnostic?)` compiles one behavior
asset (`Behavior.chibi`). Success returns its module ID, the bytes of one
prepared module of the game the arguments describe, and nil error. Failure
returns module ID zero, empty bytes and a source diagnostic. `identity` names
the source as the project does, such as `behaviors/Counter.chibi`. The module,
its states and its handlers take the IDs stage zero gives them from it:
- the module is `module:<identity>`;
- a state is `symbol:<identity>:1:<name>`;
- a handler is `function:<identity>:<offset>:1`, where `<offset>` is the
  handler's byte offset.

The game arguments are flat tables, since records hold no arrays:
- each event, with its ID, its ABI and a range of payload fields;
- each type the game links, with whether modules require it (a namespace
  such as `SceneGlobals` is listed but not required), the type it stands for
  (as `Transform` stands for `vec2`), its inferred error category, its ID,
  layout, storage and size, its
  component ID when it is a component, and a range of fields with their
  storage, offsets, whether they are writable, their type as an index into
  these types, whether one is a constant whose value is its name, such as an
  input action, the native that reads one, such as an action's `pressed`,
  and the key it reads with, such as a scene binding's name;
- the entity type that owns behaviors;
- each native that combines numbers, and records of them field by field, for
  a binary operator, as an index into the natives;
- each global the game binds, with its storage and type;
- each native that makes a record of its arguments, such as `vec2`, with the
  record's type and, for each argument, the record field it gives.

As in stage zero, the module lists every type as a requirement, and lists the
events its handlers take. `CompileBehaviorsWithCatalog` takes the identities
and sources of a project's behavior assets, then the same arguments, and
returns a CHIBICLO closure of their modules, or no bytes when it rejects one;
then `BehaviorRejectionWithCatalog` returns the first rejection's identity,
line, column and message. Their eighteen arguments use `CatalogNative` and
`CatalogPort` rows and append the native effect masks in native order.
Native rows retain capability kind, canonical and source names, and whether the
binding supports a direct data call. Ports carry nominal type IDs and reflected
defaults, including whether a default can be represented. `StageOne.chibi`
invokes these entries on the input that
`chibi-compile --stage-one-input` exported from the project's catalog; both are
archived with game projects. The pure package/bootstrap inputs keep
their existing layouts. Fixed native structs and entity handles preserve their
types through arguments, results, aliases and result-name projections. Nominal
collections, unknown layouts and unsupported flags remain explicit failures
when selected; unrelated unsupported catalog rows do not reject a project.
Events, components and the fields of game types match their names as the catalog
matches them: exactly, else loosely, ignoring case, spaces, underscores and hyphens.

Synchronous native actions use their actual linked binding, parameter defaults
and effects. Supplied arguments evaluate once in source order; positional or
named arguments then bind to the selected parameters, and omitted parameters
use supported defaults. Only a compatible constant empty owner argument is
replaced by the current invocation owner. A dynamic empty handle remains empty.
C-string boundaries reject embedded NUL; owned string results preserve their
bytes. Suspending actions and other bindings that lack a supported direct call
remain explicit rejections.

The plural entries also compile executable `library` roots. All source roots
and signatures are declared before bodies, so libraries can call each other
regardless of file order. Each body is compiled once; actual call edges order
the closure's modules and propagate effects. Recursion inside a library is
allowed; a dependency cycle across libraries is rejected. Library declarations
export their functions without `pub`. Lexical functions and strict package
imports keep their existing lookup rules; library exports share catalog name
resolution with native functions, including canonical-name precedence. Rows
whose exact callable IDs belong to parsed sources retain their catalog indices
without competing as native implementations.

Executable libraries support flat records of builtin numeric, bool and string
fields without field defaults, numeric/bool and struct named results, and
positional calls with trailing numeric/bool/string literal defaults. String
fields have owned storage. Their native graph projection uses the same source
catalog and owns its names and defaults;
it performs no executable lowering. Authored named arguments, nested source
records, source constants and unions, systems and overload selection involving
authored libraries remain unsupported by this game-source path.
Bare `char`, `number`, `float` and `double` conversion calls are also unsupported;
qualified library members with those names remain ordinary source calls.

A behavior may hold integer, floating, boolean, string and entity state,
member procedures, and `on` handlers of the game's events with scalar
payloads. State types may be inferred from boolean, string and numeric literals.
An entity state may omit its default and starts empty; the other supported
scalar states require a default. Entity values retain their nominal type through
`self`, component fields and keyed scene globals; a different handle type cannot
be assigned or compared as an entity. A typed `null` default gives the type's zero
value. Strings preserve all bytes, including embedded NUL, and assignments keep
an instance-owned copy after the handler returns. A
member procedure is a function the behavior's handlers call; stage zero
names it `symbol:<identity>:1:<Name>:callable`. A handler takes the
event's payload, one parameter per field in payload order. As in stage zero,
`on Move :: (y: number)` names fields in any order, matched as the catalog
matches names; each field it leaves out gets an unnamed parameter. A name the
payload lacks, or a field listed twice, is refused with stage zero's
diagnostic. Written types are not checked yet. A handler body
is a function body, as above. In it, states are names that read and store as
`VARIABLE` and `STORE`, `self` is the owner, and `self.C.f = v` writes the
writable field `f` of the owner's component `C`. As in stage zero, the write
makes the component and field references, then evaluates `v`, then writes
through them. An integer converts to a `number` or `f64` field exactly to an
`f64` first; an integer literal is a constant of the field's type.

`self.C` reads the owner's component through a reference into a record of its
own, so `self.C.f` reads a field. When an operand of a binary operator is a
record, the operator's native from `operators` combines it field by field,
with a record whose fields have the same names or with a number, as stage
zero resolves operators. The result has the type of the first record operand;
an untyped literal operand is an `f64`. `self.C op= v` reads the component,
combines it with `v` and writes the result back through the same reference;
`self.C.f op= v` reads the field through its reference the same way. A game
record's record field, such as `self.Input.Move`, is the address inside it.
`number` and `f64` values add, subtract, multiply, divide, negate and
compare, and an untyped literal takes the other operand's floating type. A
global's constant field, such as `input.Jump`, is its name, and a field a
native reads, such as `input.Jump.pressed`, is a call of that native on the
value; a field a native reads by key, such as the scene binding
`globals.Leader`, is a call of that native on the key. `e.C` reads the
component of the entity `e` as `self.C` reads the owner's. `self.C = v`
writes a record whose type stands for the same one as the component's, as
stage zero assigns `vec2` to `Transform`. Among natives sharing a name, such
as the `number` and `f64` `cos`, each argument's own type picks the one
whose parameters take them. A native that makes a record, such as
`vec2(x, y)`, is a struct literal of the fields its arguments give. `after`
handlers and reading a global itself are not supported yet.

A handler's function lists the components it reads or writes, and one write
for each component it writes through a reference, where stage zero lists two,
and has the behavior's states as its variables. Its effects are stage zero's:
reading a state reads the invocation's retained values; storing one also
reads them and writes; reading a component reads, and writing one reads and
writes. Its debugger points are those every function has (see Output).

## Output

`Body.chibi` emits each function in one pass. A work stack visits statements
and expressions in evaluation order, and each expression pushes its typed
value. `EmitBody` keeps the tables and emits control flow; every other node is
a step in `Steps.chibi` that returns only the instructions and rows it adds,
numbered from the tables' sizes. A single emitter function exceeded the
verifier's storage budget for one definition, instructions times slots.

Scalar and string parameters move to locals and live in the locals bank.
Records and arrays follow stage zero's value model:

- A record or array value is a pointer to its storage.
- A struct or array literal calls the runtime's construct intrinsic.
- Every store into a local copies the value with the snapshot intrinsic into
  the data cell that is the local. Record and array parameters are read in
  place.
- An element read checks `0 <= index < count` and then reads the array's
  representation, as stage zero does: a nonzero stride means the elements are
  inline in `data`, where a record element is its own address; otherwise
  `items` holds value headers. The target supplies the `data` and `stride`
  offsets. A record element's boxed pointer is loaded only past a branch,
  through an address local. The runtime never packs strings, unions or records
  with a string field, so a read of those indexes `items` and loads the
  header's payload in three instructions, where the choice takes 15 or 16.
- In-place updates use the array push, extend and store intrinsics on the
  local's header address.
- A `for` loop walks its own copy of the array, so the body may change the
  original; its continues jump to the increment.
- A record or array result publishes its whole value header.
- A union value is a scalar in the locals bank, built by the union-make
  intrinsic from its variant's tag and payload. A switch saves its subject in
  a local; each arm tests it with union-test and reads its bindings with
  union-get, and a record or array binding is read in place. Union IDs and
  variant tags follow stage zero's scheme.
- Pointer slots are listed as roots, so a suspended caller keeps its values.

Calls pass arguments and results through data cells; authored calls suspend
the caller once each, and native calls are host calls with the catalog's
capability ID and ABI. Every statement starts with a budget check, so every
loop crosses one. The definitions are canonical and unoptimized: the runtime
verifies and finalizes them on load, and a rejected definition's error now
names the verifier's own reason. Function and record IDs, record type names
and layout hashes follow stage zero's scheme, so a host invokes the same
entries and the runtime accepts the same record requirements.

Each package is its own module, assembled dependencies first:

- A call into another package goes through an import row, added on the first
  such call, as stage zero adds them. The row names the callee's module ID and
  the fingerprint of its prepared bytes, which the runtime checks on load. The
  reference names the callee's entry ID.
- Record requirements cover every record of the package and of each package
  it reaches, under the declaring package's IDs and type names. Stage zero
  lists only its own records and the types its direct imports export; the
  runtime accepts the extra rows. Unions have no requirement rows, and a
  variant's tag is the same in every module.
- The closure has one row per package, named by its identity.

Runtime sources record each instruction's origin. Each function has stage
zero's points:
- a function point at its entry, meaning the event a handler handles, or the
  function, and a completion point at each `return` and at its end;
- a statement point before each statement, and one before each value a
  `return` gives;
- an expression point before each expression it evaluates, outermost first.
  Parentheses only group, `-1` is one literal, and an array of literals is one
  constant, so their parts have none;
- a symbol point where it reads a name, after the name's expression point,
  and where it stores one: an assignment, a declaration with one value, a
  `for` element and each value a `return` gives its result.

An assignment's target has the points of its value's reads: a component or
field target has an expression point for each member it reaches through and a
symbol point for the name it starts from, and a compound assignment reads its
target first. Stage zero writes a parameter through its reference, so
assigning one has the target's points and no store. A local array that `xs =
append(xs, x)` updates in place is stored, then read again. A `switch` arm
has an expression point for its pattern, and each binding of an arm or of `is`
a symbol point. A function with a record or array result stores that result's
symbol at its entry.

A point's occurrence is `script_occurrence` of its record's source range, so
it is the one Studio's breakpoints find; a completion point's record is the
function, and a symbol point's is the name's declaration. A symbol the source
declares nowhere, such as `self` or an unnamed result, spans its package's
first document, since stage zero's unset range names document 0 and clamps to
its length; another package's constant is declared
where this package first uses it. A point's position is where it is placed.
Each point is a `SOURCE_POINT` instruction, which suspends, so each
invocation's and point's target is its order among them, as the runtime
numbers suspensions; an imported module's fingerprint covers those bytes.

Each function also has stage zero's watches, which Studio shows a paused
function's values with. A watch is a `SOURCE_WATCH` of the slot that holds a
value, with its expression as origin and its type's header as initial value:
- an output watch on each value an expression produces, `self` included, and on
  each number literal once its use gives it a type, except an array of
  literals. An integer constant expression, a grouped and signed literal such
  as `-(2)` or `-(2.0)` included, folds to one constant at its outermost
  expression, which has the one point and watch; a grouped floating literal
  keeps a point and watch at each sign;
- a call's inputs, one per argument with the argument's own type, defaults
  included (a default also has an output watch); a struct literal's, numbered
  by each field's place in the record; and a call with several results has
  an output watch for each, numbered;
- a compound assignment watches the value it stores at its target, and one on
  a name or an element also the value it reads there;
- a `switch` arm watches the subject it reads again and its pattern's test;
- `xs = append(xs, x)` watches the array as its argument and as the call's
  first input before evaluating the item, then the item as its second input,
  and the array it leaves as the call's output. Extend and method statements
  preserve the same receiver-before-item order, including an authored item call.

A behavior's handles are its entities, so their watches name the entity type.

Without `debug`, a body places none of these. Steps still leave some
placeholders, which the body drops at its end, sending a jump or branch to one
to the instruction after it. The `-debug-free` selector that checked this
was deleted with stage zero on 2026-10-30.

`Number.chibi` reads number literals of floating types as stage zero does:
the binary64 nearest the literal, and the binary32 nearest that binary64. A
literal that overflows, or a nonzero one whose binary64 is zero or subnormal,
is out of range ("Numeric literal must be finite"), and the reading reports
whether the value fits a binary32. This reading is the same on every host and
is the intended one. Stage zero calls `strtold`, narrows, and leaves subnormal
results to the C library's `errno`; where `long double` is `double`, as on
macOS arm64, that is the same reading. Where `long double` is wider, as on
x86-64 Linux, stage zero rounds a few literals twice and judges values near
the least normal by the C library. Stage zero is left as it is: it retires
with the bootstrap.

The reading ports the slow path of Odin's `core:strconv`
(`decimal_to_float_bits` over `core:strconv/decimal`, from Go's `strconv`).
The literal becomes a decimal of up to 800 stored digits that shifts by powers
of two until its integer part is the mantissa rounded half to even, using only
integer arithmetic. Two departures: computing the carry's digits replaces
Odin's table of powers of five for left shifts, and the decimal point counts
every significant digit read, not only the stored ones, which Odin's and Go's
fast paths otherwise cover.

## Hosts

Stage one runs natively on macOS arm64 and on the 32-bit Wasm host. Linux
x86_64 and Windows x64 host execution remains unverified here.

- **32-bit Wasm:** the compiler prepared for Wasm compiles every package and
  behavior sample, and their closures run there. Stage one writes the
  target's pointer width into each module's header.
- **Compiler host and bootstrap:** they load closures on the host's compiled
  backend: SLJIT where the build has it, bytecode where it doesn't, and the
  Wasm JIT in a browser.
- **Large definitions on Wasm:** the Wasm JIT runs a definition on bytecode
  when its generated module would exceed 128 KiB. V8's optimizing tier spends
  about 4 MB per KiB of module, and aborted on stage one's 482 KiB and 766 KiB
  modules. The same budget sends an inspected run to bytecode when its
  inspection module would exceed it.
- **Bootstrap:** it runs natively only. Stage one compiling itself needs more
  than Wasm's 2 GiB of linear memory.

## Test

The compiler's own `@test` functions and the sample suites run with
`chibi test`. Each is a row of `tools/tests.tsv`:

- `chibi-test-compiler` (`chibi test tools/compiler`): the tests beside each
  compiler package, including the emitter's;
- `chibi-test-samples-base`, `-collections`, `-comptime`, `-fallible`,
  `-imports`, `-math`, `-native`, `-native-arguments` and `-types`
  (`chibi test test/compiler/samples/NAME`): the samples in
  `test/compiler/samples`, with their `// error:` and `// fails:` expectations;
- `language-package-compiler-chibi-scalar` and `language-scalar-native`: the C
  scalar and native lanes of `test/language/package_compiler_chibi.c`;
- `compiler-bootstrap`, `bootstrap-recovery` and `bootstrap-load`: recovery of
  the compiler from the checked-in artifact.

```sh
chibi build test chibi-test-compiler chibi-test-samples-base
```

Until 2026-10-30, `package_compiler_chibi.c` also ran the samples through stage
zero and compared the two compilers: the plain, `-compiled`, `-types`,
`-collections`, `-imports`, `-behaviors`, `-sharing`, `-actions`,
`-libraries`, `-literals` and `-debug-free` selectors. They were deleted with
stage zero; `archive/README.md` lists what they covered that nothing re-covers,
such as authored-compiler output executing on both backends against the same
sample tables.

Compiling is slow. In the Debug ASan/UBSan build of 2026-10-20 (macOS arm64),
stage one compiled the base selector's 16 accepted samples in 2.5 s on SLJIT
and 5.7 s on bytecode. The arrays sample,
about 50 lines, takes 0.8 s and 1.8 s; the imports sample, which compiles all
of `core:strings`, takes 5.7 s and 13.7 s, and stage zero's build of the
compiler takes about 11 s of every selector. Two runtime changes cut the base
selector from 34 s to 28 s:

- A running call stack keeps up to 8 MiB of retired frames, evicting the least
  recently retired, and settles back to the 64 KiB idle budget. Before, the
  frames of the compiler's step functions, 0.2 to 0.7 MB each, were set up
  afresh on every call.
- Copying a record descriptor into another arena copies its bytes instead of
  re-deriving and re-hashing its layout.

The rest is the interpreter running the compiler's code and copying its values:
calls that return arrays, records copied into locals, and `for` loops over
arrays of records, which copy each record. `docs/authored-prepared-writer.md`
measures what each costs. On 2026-10-19, encoding a row's fields in one call in
the prepared writer, and reading arrays that are never packed without the
stride choice, cut the CPU time of the base, types and imports selectors from
31, 26 and 41 s to 27, 24 and 36 s, and of the writer's own selector from 36 s
to 21 s.

`chibi build bootstrap-artifact` recovers stage two from the seed and the sources
on disk (the stage-zero lane that prepared stage one was retired on 2026-10-30; the
sequence below is what it checked). Stage one compiles the compiler's complete source set, without debugger
points and watches, to a 9.6 MB closure: stage two. The compiler host loads
stage two and runs its `CompileClosure` on the same sources and arguments, as
JSON, and the bytes must equal stage one's. Stage two then compiles every
sample with them: its `CompileClosure` must give stage one's closure, and a
rejection its `Compile` JSON; the existing selectors check stage one's against
stage zero and run its closures on bytecode and SLJIT.

With watches, stage two was a 20.6 MB closure, past the host's 16 MiB byte
result, its million prepared values and its layout work; raising every bound
took about 10 GB to load it and still failed. On 2026-10-20, in about 41 s of
wall time at a load average of 12, stage one compiled the compiler in about
20 s of CPU time and stage two in about 13 s. Before then, every constant,
default and first use counted whitespace with `ByteAt` from its document's
start, and each `ByteAt` measures the text. A profile of stage one now puts
over half of its time in `pin_record_valid` and the layout hash it recomputes
on every record value it copies.

On 2026-10-19, before points and watches, the run took about 23 s, with a peak
memory footprint of about 3.5 GB (macOS arm64, 2026-10-19): stage one compiles
the compiler in 11.7 s of CPU time and stage two in 7.6 s, against 13.3 s,
8.9 s and 4.2 GB before the changes above, and the closure shrank from 6.64 MB.
An earlier measurement put most of stage one's footprint in
the `prepared_writer.Write` frame writing the package compiler's module,
against 92 MB for stage two's build of the same function on the same module.
Each copy of the closure is about 256 MB as a `u8[]` of 40-byte values.
Under Debug and the sanitizers one
compile takes about two minutes, about 40% of it validating record
descriptors on every snapshot, so this check runs only on the Release build.

Stage two needs the compiler host's larger closure bounds. On 2026-10-19 the
package compiler's module needed about 113 M units of record-layout work and decoded to
about 80 MB with its verification scratch, past the 64 MiB defaults of every
prepared read, and its 6.4 MB closure, as a `u8[]` of 40-byte values, was
past the host's former 1,000,000-value byte result.
