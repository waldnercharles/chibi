# Authored flat records: in-progress implementation

This Draft feature implements the first library-contained record slice needed by
Studio's Outliner. It is not complete or eligible to land yet. In particular,
leak-enabled validation and the affected existing-route/SDK/integration checks
remain open. Focused globals, headless UI and state/watch closure now pass. No
Studio panel has been converted by this change.

## Source contract

The owner selected the following declaration form and required/defaulted-field,
value-copy and owned-string behavior. A small example uses the same field types
and construction rules as the bounded fixtures:

```chibi
library Rows {
    OutlinerRow :: struct {
        key: string;
        label: string;
        icon: string = "node";
        prototype: string = "";
        drop: string = "";
        depth: int = 1;
        behaviors: int = 0;
        active: bool = true;
        marked: bool = false;
        hidden: bool = false;
        locked: bool = false;
        renaming: bool = false;
    }

    Make :: (key: string, label: string) -> OutlinerRow {
        return {key: key, label: label};
    }
}
```

- The initial declaration scope is a library. Fields are flat `string`, `int`
  and `bool`; nested records, generic containers as fields and packed layouts are
  outside this slice. Existing reflected `struct<"SceneObjectRow">` syntax is
  retained.
- An omitted field needs an explicit declared default. `key` and `label` above
  are required. Defaults use bounded scalar compile-time values/operators;
  calls, mutable reads, field/default dependencies and aggregate/index/member
  expressions are rejected, including in short-circuited branches.
- Nominal identity includes the qualified library/type. Storage compatibility
  also checks the actual immutable layout; a same-name changed layout cannot be
  reinterpreted as the previous one. Default-only changes do not alter storage
  layout identity. The focused live replacement case preserves values across a
  default-only edit and resets them for a renamed-field incompatible layout,
  checking both outcomes before any new event can overwrite the evidence.
- Rows use value semantics and owned text at language/API snapshot boundaries.
  Ordinary outer arrays retain their value semantics. Growing a record array
  without constructed rows is rejected before changing the result; shrinking,
  slicing and clearing do not invent uninitialized required fields.
- Record equality uses the declared field types and owned layout. Existing
  low-level null-array equality semantics are unchanged; fixtures compare null
  headers/identity explicitly rather than assuming that helper returns true.

## Runtime and SDK ownership

An immutable carried `pin_record` owns its fields and names in one contiguous
closure. Generic runtime values resolve this carried layout, never an ambient
same-name native type. Finalized functions intern their owned definitions;
outer-array snapshots share one closure across their header and all elements.
Scalar constructions can still allocate their own closure. No general
allocation reduction or production Outliner scaling result is claimed.

Snapshot-return lowering publishes the complete retained header, including
null-array metadata. Bytecode, SLJIT, Wasm and generated-C contracts agree on the
header representation and native-header clearing. Native C shallow-copy helpers
keep their existing borrowing contract; language ownership does not depend on
those helpers. Low-level direct execution contexts still require caller-valid
ABI inputs; the focused fixtures are not a complete adversarial entry verifier.

The public `pin_value`/`pin_array` ABI changes require matching SDK headers and
libraries. Execution prepared artifacts use version 14 and reject older versions
with a rebuild diagnostic. The owned execution codec serializes validated flat
layout tables and enforces shared allocation and metadata-work budgets. Native
legacy value-wire bytes remain unchanged in the checked recipes. Rebuild
prepared execution artifacts; do not mix old SDK binaries with these headers.
This additive declaration is not used by an existing checked-in project, so it
does not require a project-format migration/version bump.

Complete document input requires every field, including source-defaulted fields;
it is a value codec, not source construction. It owns the carried layout and
strings, preserves metadata on null/empty headers and registers collection
traces. Invalid output metadata fails the whole nested projection before an
existing destination key is replaced. Generic globals now snapshot replacement
values into fresh owned storage before committing a store change, and preflight
host output headers before mutation. Callback results keep invocation-owned text
and metadata while preserving the existing setter-result conversion contract.
The focused native service cases cover scalar/array ownership after producer
retirement, atomic mismatch rejection and the real headless UI data/revision
callback. Existing native scalar/handle compatibility still needs final checks.

Actual program and inspector cases cover scalar/array persistent state,
compatible/incompatible replacement and retained scalar/allocated-empty/three-row
watches after state overwrite and program destruction. Every admitted dispatch
reached its requested bytecode, SLJIT or generated-Wasm backend, including the
inspected call, with zero fallback. The synthetic frame-input case uses private
fixture metadata to exercise the exported reconstruction API; it does not prove
authored pause-frame lowering. Small watch snapshots do not establish exhaustion
behavior for the inspector's 4 MiB/512-value limits.

## Validation and remaining gates

The companion checkpoint evidence lists independent exact-source/build reviews,
finite C/pico_unit commands and preserved failures. All runtime commands use the
existing production supervisor's 60-second whole-route ceiling. Generated-C and
installed-consumer children also retain the existing 10-second child ceiling.
New routes are standalone, outside aggregate groups; none re-enables quarantined
suites. Configuration/build timing is separate from runtime measurement.

Native checkpoints use ASan/UBSan with leak detection disabled because the
available tracing environment cannot execute LeakSanitizer. A prior supported
escalation failed before shell launch. These results do not establish leak
freedom, and the Draft/leak/landing hold remains. C-Wasm foundation/semantic/
document runs are distinguished from actual authored generated-Wasm execution.

Remaining acceptance includes:

1. Existing native scalar/handle global compatibility and independently admitted
   affected existing-language/format/native contracts.
2. Fresh coherent SDK/build closure, current-main integration and exact
   published-head validation of the cumulative feature. Staged checkpoint
   results remain scoped to their recorded heads.
3. Leak-enabled validation. The Draft/leak/landing hold remains; full feature
   and Studio acceptance are not inferred from the focused checkpoints.

The existing inspector capacity rules remain in force; the record-specific
focused route covers small retained snapshots rather than a capacity stress test.

The first Outliner conversion also retains its separate typed scene-operation,
history, lifecycle, exact SDK pair and actual UI parity gates. This feature does
not introduce a C controller facade, closure syntax or packed storage APIs.
