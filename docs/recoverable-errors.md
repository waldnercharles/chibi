# Recoverable errors

The owner selected ordinary Odin/Go-style results on 2026-10-27. A fallible
operation returns its values followed by a typed optional error. A nil error
means success. Errors are ordinary values that callers can inspect, report,
store or return. There are no exceptions or new propagation operators.

The compiler's existing named-result syntax is the source form. These proposed
package signatures depend on the stage-one parity work tracked in `PLAN.md`:

```chibi
@fallible
pub Decode :: (input: u8[]) -> (data: u8[], error: DecodeError?)

@fallible
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

`@fallible` opts a function into this contract. Its final result must be optional
and is the error that requires acknowledgment. The marker is part of exported
source metadata, so an import alias does not change it. Ordinary optional
absence-returning APIs remain unaffected. Existing `@test` spelling and the
ordinary attribute grammar remain in use; no new parser form is needed.

This keeps each package's typed errors without introducing a universal built-in
error interface or erasing their details. A shared error abstraction would need
a separate concrete consumer and a type-composition contract.

For native asynchronous calls, awaiting, cancelling, passing, storing or returning
the task acknowledges the task handle. Copying it into unused locals does not.
After a successful await, the returned error has its own acknowledgment
obligation. Cancellation acknowledges the decision to stop waiting; it does not
promise that the caller observed a failure from work that already completed.

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

The authored checker currently supports synchronous source calls. Marked async
calls and marked calls inside generated query evaluators receive an explicit
unsupported diagnostic. Optional collections and character optionals remain
outside this prerequisite. Native async support does not establish authored
compiler parity for those cases.
