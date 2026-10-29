# Compositional records

Nested records and array fields are implemented in both compiler stages and the
existing VM backends. Arrays can contain scalars, non-null pointers and records.
Map and union fields, nested-container element types and full LLVM aggregate
lowering remain separate work.

## Fields and construction

Records can contain other records and arrays in the first stage. The following
stage adds map and tagged-union fields using the same rules. These use existing
type annotations and record literals; they need no new declaration syntax.

For example, a library description can carry its inputs instead of passing
sources and flags beside it:

```chibi
CompileOptions :: struct {
    optimize: bool;
}

LibrarySpec :: struct {
    name: string;
    options: CompileOptions;
    sources: string[];
    flags: string[];
}

Support :: () -> LibrarySpec {
    return {
        name: "support",
        options: {optimize: true},
        sources: ["src/support/array.c"],
        flags: []
    };
}
```

Every aggregate field requires an explicit value at construction, including an
empty array or map. There is no implicit empty-container or recursive default
construction. Existing scalar-default and nullable-pointer rules remain in
force; aggregate field defaults are deferred. Both compiler stages must apply
the same construction rules and report a missing required field.

## Values and pointers

Copying a record copies each field as a value. Mutating an array or map inside
one record copy leaves the other copy unchanged. The same rule applies through
nested records and the active payload of a tagged union. Copy-on-write storage
may make these copies cheap without changing the observable result.

Copying a pointer field preserves its target identity. Records containing the
same pointer therefore observe writes to that target. Assigning a new value
through a pointer replaces the target's value while preserving its identity.
Composition does not introduce a second collection ownership model or require
authors to spell moves, retain operations or drops.

## Finite types and layouts

An inline record cannot contain itself, directly or through other inline records
or union payloads. The compiler rejects these cycles before computing sizes or
lowering bodies and identifies the involved fields. Forward declarations and
cross-package fields are legal when their dependencies resolve to finite
layouts. Recursive container definitions such as `Node` containing `Node[]`
are deferred, even though the container header has a finite size. Existing
recursive links through managed pointer fields remain legal.

The existing maximum of 16 nested record layouts applies through inline fields
and record-array element types. A layout closure can own at most 64 MiB of
metadata, and sizing/validation admits at most 1,048,576 expanded record and field
visits, including duplicate-name comparisons. Preflight rejects excessive work
before allocation. These bounds apply to type metadata, not array lengths or
test durations. Stage one caches depth, alignment and child hashes so repeated
child types do not cause repeated compiler walks.
The VM reader enforces the precise byte bound. Stage one's source checks use
target-independent depth and work summaries, so unusually large metadata can
still be rejected when its prepared module is loaded.

Offsets, size and alignment follow the compilation target. Field metadata carries
the complete field type, including container, nominal identity and owned child
layouts. Storage compatibility includes the relevant child layouts; a same-name
changed child must not be reinterpreted using an old layout. Defaults remain
separate from storage identity. This contract does not establish a native C ABI
for aggregate-bearing authored records.

Retained values own the metadata needed to read, copy, trace and release their
children after the producer or its catalog is gone. Copy and drop operations
follow the actual child type and only the active union payload. Aliased backing
storage and pointer cycles use the existing ownership and collection contracts.

## Prepared modules and replacement

Prepared modules carry enough validated child metadata to reconstruct the same
layout and ownership behavior without consulting a replaceable catalog. The
authored writer and runtime reader must agree on type references, layout hashes
and format version. Invalid or incomplete metadata fails before publication.
Changing the prepared format requires matching compiler artifacts and runtime
seeds. An additive source feature alone does not require a project version bump.
This slice uses prepared format 27. Rebuild prepared modules and use matching SDK
headers and libraries; the new field metadata changes the C descriptor size.

Hot reload uses the established state-migration policy: retain compatible values,
convert supported changes and use new initialization where migration cannot
preserve a value. Supporting aggregate fields does not by itself implement
migration of nested containers, maps or union payloads. Layout ownership must
keep old values safe until migration or release completes. A pointer that reaches
a changed layout follows the existing reset policy until in-place cell migration
is separately supported.

## Completion evidence

Each stage needs fixed-value regressions through both compiler stages for
construction, reads, replacement, nested mutation, value-copy isolation and
pointer identity. Native boundary checks cover retained metadata, tracing,
release on early exits, malformed prepared inputs and producer destruction.
Prepared round trips must preserve the same results without the original
catalog. Reload checks must distinguish preserved, converted and reset values.

VM, JIT and native support are reported separately. Native lowering remains a
later stage until its aggregate layout and ownership paths pass the same behavior
checks. A successful parse or VM execution is not evidence of native support.

General scalar-default parity in stage one remains a separate prerequisite.
This slice preserves its existing nullable-pointer defaults and stage zero's
scalar defaults; it does not add partial constant-expression evaluation.
