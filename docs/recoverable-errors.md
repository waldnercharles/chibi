# Recoverable errors

The owner selected ordinary Odin/Go-style results on 2026-10-27. A fallible
operation returns its values followed by a typed optional error. On 2026-10-28,
the owner selected `@error` on types and inference from result types, replacing
the draft's per-function `@fallible` marker. A nil error
means success. Errors are ordinary values that callers can inspect, report,
store or return. There are no exceptions or new propagation operators.

The compiler's existing named-result syntax is the source form. These example
package signatures describe the selected convention:

```chibi
@error
pub DecodeError :: struct {
    offset: int;
    message: string;
}

pub Decode :: (input: u8[]) -> (data: u8[], error: DecodeError?)

@error
pub ClockError :: union {
    PermissionDenied | Platform(code: int)
}

pub Now :: () -> (instant: Instant, error: ClockError?)
```

Read named outputs through a local. Tuple destructuring is not part of this
change:

```chibi
decoded := codec.Decode(input)
if decoded.error {
    Report(decoded.error)
    return
}
Use(decoded.data)
```

Packages own their error types. A codec can describe an error kind and byte
offset; a clock can describe permission denial or platform failure. A different
variable name or import alias does not change the contract. Do not identify an
error by a name ending in `Error`, an `err` local, or a copied nominal type ID.

## Partial results

An error does not automatically invalidate the other outputs. The API defines
their meaning on every outcome:

- A complete decoder publishes no partial decoded output on failure. Its error
  identifies the first rejected input location.
- A streaming read can return valid bytes and an error together. Process those
  bytes before handling its stopping condition. End of input is normal status,
  not malformed input.
- A batch importer can return successful items and individual failures in an
  always-valid report. A failure to start the batch is a separate operation-level
  error. Successful items do not disappear because another item failed.
- A complete asset compile returns its prepared bytes and entry ID on success.
  On failure it returns empty bytes, entry ID zero and a source diagnostic.

The compiler does not require proving success before reading another output.
Do not add a blanket `if error == nil` requirement to progress, counts or batch
reports. Typed error returns also do not promise rollback of earlier effects.
Using another output while the error is present reads the value the function
returned. There is no hidden poisoned-value state or automatic trap. The API
must document whether that value is partial progress, an empty default or
another meaningful result.

## Acknowledgment

Silently dropped errors must be compile errors. Reading only the other outputs
does not acknowledge the error. Binding an outcome to an unused local or copying
it through unused aliases does not acknowledge it either. An overwrite must not
erase an unacknowledged outcome.

Testing, reporting or returning the error acknowledges it. Explicitly passing or
storing it transfers responsibility. This does not prove a caller's recovery is
useful, or that a stored error will eventually be handled. It avoids a separate
linear ownership system for ordinary data.

`@error` designates a package-owned struct or union as an error type. A callable
whose final result can directly contain that type requires acknowledgment of
that result. No function marker is needed. Earlier result positions cannot be
error-bearing: the final result is the operation's error channel. A single
required error result also requires acknowledgment; optionality adds a nil
success case, rather than enabling checking.

Aliases retain the property. Optional types and transparent unions retain it
when any direct alternative is error-bearing. A wrapper record, array, map or
pointer is ordinary data, even when it contains errors. This lets a batch report
hold many errors without turning the report itself into an operation-level
error. Mark the wrapper type explicitly when that is the intended contract.

The type property is exported and compared during source-catalog cache
validation. An import alias or a different output name does not change it.
Ordinary optional absence-returning APIs remain unaffected. Existing `@test`
function attributes remain in use; the same attribute grammar now applies to
struct and union declarations. The old function marker is rejected.

This keeps each package's typed errors without introducing a universal built-in
error interface or erasing their details. A shared error abstraction would need
a separate concrete consumer and a type-composition contract.

Pointer tracking is conservative after branches select different local targets.
If an error alias is later overwritten, checking an uncertain pointer may no
longer acknowledge the original error. Check a surviving direct alias or bind
the pointer to it again. The checker does not maintain sets of possible pointees.

For native asynchronous calls, awaiting, cancelling, passing, storing or returning
the task acknowledges the task handle. Copying it into unused locals does not.
After a successful await, the returned error has its own acknowledgment
obligation. Cancellation acknowledges the decision to stop waiting; it does not
promise that the caller observed a failure from work that already completed.

## Union composition

For a marked `TextureError`, this signature retains the checking requirement:

```chibi
pub LoadTexture :: (path: string)
    -> (texture: Texture, error: TextureError? | int)
```

The final result contains either an optional `TextureError` or an `int`.
Adding another alternative does not remove the obligation to acknowledge a
possible error. The integer remains an ordinary alternative: its value is not
automatically interpreted as a success or failure code.

Chibi currently preserves the boxed optional inside this union. The outer value
is not itself optional, so comparing it directly with nil is a compile error.
Inspect the optional alternative, then inspect its presence:

```chibi
if loaded.error is TextureError? failure {
    if failure {
        Report(failure)
    }
}
```

Use `(TextureError | int)?` when nil should be the outer success case. Then
`if loaded.error` or `loaded.error != nil` tests presence, and a subsequent type
test distinguishes the error from the integer. The two spellings do not
currently normalize to one type. This revision preserves that optional contract;
optional normalization remains a separate design choice.

| Result type | Requires acknowledgment |
| --- | --- |
| `TextureError` | Yes |
| `TextureError?` | Yes |
| `TextureError? \| int` | Yes |
| `(TextureError \| int)?` | Yes |
| `int?` | No |
| `TextureError[]` | No; errors are report data |
| Unmarked report struct containing errors | No |

When the integer represents a platform failure, prefer a marked union such as
`TextureError :: union { Missing(path: string) | Platform(code: int) }`. That
keeps the integer's meaning explicit. A tagged union does not acquire the
property merely because one of its payload fields contains an error: mark the
operation's error union itself.

Native transparent-union support implements this composition rule. The authored
compiler currently rejects transparent unions and general type aliases rather
than compiling them without checking. Extending its type representation and
lowering is a separate change.

Native union flattening also needs to retain the category of a marked
string-literal union. The draft preserves it on each use without changing
runtime union tags or marking an equivalent ordinary literal union. Containers
and pointers around a composed literal error union whose nominal provenance
was flattened receive an explicit unsupported diagnostic: current shared
element descriptors cannot retain that distinction safely. The same limit
applies to a task with one such flattened success type. Named multi-result
async outputs retain their field metadata and remain supported. Optional union
alternatives whose named payload survives flattening remain supported too.
Arrays and pointers of named error types remain supported. A separate semantic
descriptor representation would be needed to remove this narrower limit.

Native aliases produced by existing type-returning calls retain the category;
plain `Alias :: SomeType` declarations do not gain new type-alias semantics in
this revision. The existing `task(T)` type reader accepts union composition and
parentheses in its type argument without changing compile-time value operators.

## Implementing an operation

An error-returning function constructs ordinary data on its failing branch.
This complete example reads one unsigned little-endian integer and reports the
requested offset without throwing:

```chibi
@error
pub ReadError :: struct {
    offset: int;
    message: string;
}

pub ReadU32 :: (bytes: u8[], offset: int) -> (value: u32, error: ReadError?) {
    if (offset < 0 || offset > length(bytes)) {
        return (0, {offset: offset, message: "Offset outside input"});
    }
    if (length(bytes) - offset < 4) {
        return (0, {offset: offset, message: "Four bytes required"});
    }
    value = u32(bytes[offset]) | (u32(bytes[offset + 1]) << 8)
          | (u32(bytes[offset + 2]) << 16) | (u32(bytes[offset + 3]) << 24);
    return (value, nil);
}
```

The returned value is zero on failure because this API explicitly returns zero.
There is no poisoned value or implicit trap. A texture loader follows the same
pattern: call the file reader, inspect its error, return a texture error carrying
the path and cause, then decode/upload and either return that stage's error or
the texture with nil. Errors retain their package-specific data when wrapped.

## Return inference and optional syntax

Inferring error checking from resolved result types is independent of inferring
those types from the body. The native compiler already accepts an omitted
result annotation, inferring results from the first return. Multiple inferred
results need labels, such as `return(value: value, error: error)`. Later returns
must convert to those types. A typed optional local can supply the intended
`ReadError?` type, but a first return of bare nil cannot identify its payload
type. Recursive inference cycles need an explicit result annotation.

The authored compiler still requires explicit function result types; its
expression-inference machinery does not infer callable signatures. This change
does not extend that machinery. A future inference change could compare all
return paths, infer `T?` from `T` and nil, and require consistent result labels.
Empty literals and recursive cycles would still sometimes need a type hint.
Exported signatures could expose the resolved types without requiring authors
to write every result annotation; explicit annotations remain useful API checks.

`T?` describes a value that can be nil, including locals, collection elements
and results. TypeScript's `name?: T` describes an omittable parameter or
property; an omitted parameter has value `undefined`. These are related but
different contracts. Chibi currently separates them:

```chibi
Inspect :: (error: ReadError?)       // Argument required; nil is accepted.
Inspect :: (error: ReadError? = nil) // Argument may be omitted.
```

Keep `T?` as a type constructor. A future `name?: T` could be shorthand for the
second declaration, but replacing type optionality would leave anonymous result
types, locals and nested collection types without a consistent spelling. No
parameter or optional-type syntax migration is included here.
See [TypeScript's optional parameter contract](https://www.typescriptlang.org/docs/handbook/2/functions.html#optional-parameters).

Before changing the spelling, decide whether optional values should normalize
as `T | nil` inside unions. Moving `?` to a declaration name would not remove
the current boxed-optional distinction. Normalization and parameter shorthand
remain proposals, rather than changes in this draft.

## Source context and cleanup

Keep error kinds and structured details usable without parsing message text.
Source errors retain document identity, zero-based byte start/end offsets and
one-based line/column positions. Format human-readable context at the reporting
boundary. Returning an error from a nested call must preserve that context.

An operation releases temporary buffers and closes resources it acquired before
returning. A returned resource belongs to the successful output's normal
lifetime contract. No new manual allocation or destruction duties are added to
ordinary values. OS handles still require their service's explicit close or
scoped operation contract.

## Activation failure

Recoverable errors are normal return values. They do not enter runtime
activation-failure handling. Failed assertions, invalid runtime state and
unchecked reads/writes through invalidated component references retain their
existing activation failure behavior. A checked component access operation is a
separate runtime capability; selecting this convention does not implement one.

The integer contract is unchanged: runtime arithmetic wraps, division and
remainder by zero have defined results, narrowing conversions clamp, and
development diagnostics preserve those results.

## Implementation and evidence

This source forward port now targets `dafe746411182c3320281c0b1fe4e75e7b9b6456`,
which includes merged collections/value ownership PR #694, the protocol-13
build-host selection fix, the scalar batch transport update and canonical
collection fixture repairs from PR #800. The accepted #797 native runtime
changes the C arena layout; native validation must rebuild matching objects
and headers. Earlier checkpoints retain their original proof identities.
The port preserves the accepted ownership machinery, optional type
construction and cost-explanation facade. No held character, math, generic or portable-library implementation is
included.

The preceding `@fallible` draft checkpoint, commit
`8dd33d141b59496806f347bf2faf76695bafd933`, passed focused diagnostic Release
validation on this base: 102 semantic cases /
2,495 assertions, 15 query-value cases / 1,458 assertions, 17 import/cache cases /
1,544 assertions and 64 formatter cases / 5,432 assertions. The fallible group
passed 3 cases / 29,817 assertions across 73 samples in 44.248 seconds, using
both compiler backends and executing produced closures on bytecode and SLJIT.
Fixed value, rejection and debugger-metadata oracles stayed intact. Whole-tree
clang-format 22 checking also passed. The diagnostic harness excludes asset-host
integration; these checks do not establish normal sanitizer/asset-build success.
Those receipts remain historical evidence for that checkpoint.

The revised `@error` candidate passed 107 semantic cases / 2,655 assertions,
25 syntax cases / 304 assertions, 88 query/program cases / 13,186 assertions
(including generics), 17 import/cache cases / 1,568 assertions and 64 formatter
cases / 5,434 assertions. The authored group passed 3 cases / 32,742 assertions
across 84 samples in 47.483 seconds. Both compiler backends and produced
bytecode/SLJIT closures preserved fixed value, rejection and debugger oracles,
including the reader example. Whole-tree clang-format 22 checking passed in
25.284 seconds. The exact diagnostic driver, source patch and full inventory are
recorded under `.chibi/errors/diagnosis/type-error-candidate-08` and
`type-error-authored-04`; no production source changed after those checks.

The revision also fixes an existing authored unsupported-type crash:
`ReadType` now rejects a type wrapper without a named primary before traversing
its members. A transparent union signature receives a source diagnostic rather
than an array-index activation failure. Metadata constructors explicitly
initialize the new category; no record-default behavior was added.

The recorded `frozen-7c6ca-01` source checkpoint passed native semantic checks
with 94 cases / 2,008 assertions and the fallible source/native differential
with 3 cases / 24,437 assertions across 53 samples, including source metadata
and values. Both groups also passed ASan/UBSan. Compiler self-build passed
1 case / 39,506 assertions, including identical stage-two bytes and target
plus post-bootstrap samples. Focused public API, query, asset packaging,
named-result, argument sharing, debug-info omission/cleanup, tools and
whole-tree formatting checks also passed. These results belong to the exact
recorded source checkpoint; results from the earlier frozen candidate do not
validate this base.

The normal scratch artifact writer and recovery also passed against that
checkpoint in 110.30 and 144.02 seconds, reproducing the same compiler bytes.
The accepted native/runtime and fixture updates were then integrated at recorded
boundaries. A fresh normal `make test-build` (since retired) with default sanitizers and asset
compilation failed after 890.00 seconds on base `dafe746411182c3320281c0b1fe4e75e7b9b6456`
and source patch `d4a2700dbf774bf3ba78d0c53d29d5059e7577de0535b1b2d5a70a04ec91641a`.
Bootstrap reuse succeeded, then the 41-source facade compile reached its
existing 600,000 ms timeout. Native host configuration and compilation passed;
the facade output and normal test drivers were not produced. No downstream
checks ran. This failed snapshot predates the subsequent checker regression
repairs and does not validate them.

Normal asset-enabled build success, matching-artifact integer integration,
eligible library/shared-helper reconciliation, required engine aggregates,
checked bootstrap, public SDK publication and final merge remain pending. The
scratch artifact is unpublished. Publication, phase/ABI changes and installed
defaults remain with their existing owners.

Public package compiler `Compile`, `CompileWithEffects` and `CompileBehavior`
return a module ID, bytes and optional `syntax.Diagnostic`. The asset facade
returns its entry ID, prepared bytes and optional diagnostic. This removes the
public `Rejected | Compiled` result wrapper and `Completed(ok, ...)` envelope.
Internal compiler assembly states remain private. Closure byte entry points and
the 29-byte asset transport stay unchanged. The host-clock and portable-library
work consume the same source convention after compiler parity has been verified.

Native and stage-one execution, negative compile diagnostics, source-only
recovery, existing asset transport assertions and backend coverage are separate
claims. Record exact revisions, results and unexecuted coverage before calling
this implementation verified or ready to land. Keep bootstrap publication and
the installed compiler default with their existing owners.

The authored checker currently supports synchronous source calls. Error-bearing
async calls and calls inside generated query evaluators receive an explicit
unsupported diagnostic. Native-call result checking remains a native frontend
claim; the authored checker does not yet consume native-call error facts.
Existing optional collection behavior comes from the
accepted base; this port does not redefine it. Character support remains with
its owner. Native async support does not establish authored compiler parity.
