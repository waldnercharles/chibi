# Imported host packages

This clock-first foundation supplies private host kernel lookup, explicit
clock grants, macOS/Linux sampling, and portable time values. The public
fallible clock API remains pending the results workstream.
Filesystem, process and networking services follow in later increments.

## Registration and names

A host supplies ordinary authored package sources through an explicit `host`
collection and selects their kernel declarations in its native capability table.
The source package uses the existing package syntax and `pub` exports. Its
callers use an ordinary import, such as `import watch "host:clock"`.

A kernel keeps its canonical catalog name, such as `host:clock.Sample`. Only
the owning `host:clock` package can call the exact private member `Sample`.
Importers cannot call that kernel directly. Public names and import aliases
use the existing exact, lexical package lookup, including local shadowing.
This does not change the older ambient `build.*` vocabulary.

Both compiler stages preserve each kernel's identity, ABI, ports and effects.
The stage-one view preserves native row indices and changes only the owning
package's private source spelling. No prepared-format change is introduced.
This foundation uses source-backed wrappers; native-only package declarations
and migration of the build catalog remain separate work.

This forward port preserves the accepted Prepared 26 runtime, bootstrap minimum
6, and runtime pin. The frozen clock artifact predates that runtime and remains
a separate historical receipt. The matching reviewed artifact and final bootstrap
version will be generated from the coordinated integration source.

A `host:` package without a selected kernel rejects before execution with
`Host package is unavailable for this target`. Registering a package makes it
compilable. It does not grant its service or create an OS sandbox.

## Clock grants and lifetime

SDK embedders select `Chibi::host_clock` and register `host_clock_package()`
alongside their other native packs. Supplying a `HOST_CLOCK_SERVICE` binding
whose value is an initialized `host_clock_grant` grants access. Omitting that
binding produces a recoverable denied status through the private transport.

`host_clock_grant_init(&grant, NULL, NULL)` selects the platform clock. A host
may instead supply a provider and borrowed state, which allows deterministic
tests and controlled hosts. Each initialization assigns a distinct process-local
clock domain. Grant objects and provider state must remain alive until all
calls using them have returned. Providers run synchronously, retain no authored
arguments, and own no language values.

The transport returns four initialized scalars: nanoseconds, domain, status and
platform error. Success returns nonnegative `i64` nanoseconds and the grant's
domain. Denial and unavailable/failure statuses return zero sample and domain.
Platform failure keeps its error code. An invalid negative provider sample
becomes a failed sample with `ERANGE`. These integer status codes are private
binding details, not the public standard-library error convention.

Clock reads have a host effect and are not reusable cached data reads. The
conservative host effect also prevents them from being treated as pure test
work, constants or query predicates. Two reads must observe two provider calls.

`core:time` owns scalar value types `Instant` and `Duration`, plus exact
`Nanoseconds` and floating `Seconds` conversion. Copies own values and retain
no OS resource. An instant has no wall-clock epoch. Its domain is meaningful
only within the running process; do not persist it or compare it across
processes. Checked duration/domain operations will use the agreed recoverable
result contract.

## Targets

The native adapter uses `CLOCK_UPTIME_RAW` on macOS and `CLOCK_MONOTONIC` on
Linux. It checks the system sample and its conversion to nonnegative `i64`
nanoseconds without allocating. Nanosecond units do not promise nanosecond
resolution. Suspend accounting is platform-dependent.

Windows and browser builds have no platform clock adapter in this increment.
The narrow C reader explicitly reports unavailable there. Hosts normally omit
the unsupported package from their selected capabilities. Pure `core:time`
helpers do not require a host service. A browser adapter would need an explicit
clock origin and precision contract; it must not silently substitute wall time.

## Public error prerequisite

The results workstream owns optional-type and named-result compiler parity.
The intended Go/Odin-style shape is still pending that implementation:

```chibi
@fallible
pub Now :: () -> (instant: time.Instant, error: ClockError?)
```

At baseline `c086475d5`, a real compiler-host invocation rejects that shape with
`named result type is not supported yet`. Stage one's `ReadType` rejects general
optional types, and `FunctionSignature` also rejects named union results.
The focused probe used an `Instant` with `i64`/`u64` fields and a tagged
`ClockError` with `Denied` and `PlatformFailure(code: int)` alternatives.
The owner explicitly kept this parity with the results chat. No scalar error
fallback, generic `Result`, exceptions or propagation syntax is introduced here.

## Validation

`language-host-packages` exercises the native boundary and ordinary imports on
bytecode and SLJIT, with authored tests for time values. The independent
`language-host-packages-compiler` route recovers the current stage-one compiler,
compiles source-backed host wrappers against an explicit target, links their
prepared closure and executes it on both VMs. The wrappers are test-local and
do not publish the pending clock API.

Coverage includes private-kernel precedence over a same-named ambient native,
denied/failed/unavailable grants, retained samples above
`2^53`, successive reads, domain uniqueness, host effects, private and inexact
names, target unavailability, and a real nondecreasing native-clock smoke check.
Tests use injected outcomes rather than sleeps or precision thresholds. Actual
platform execution, build settings and elapsed results belong to the validation
report; source support alone is not platform execution proof.

## Prior art and next services

Odin separates [monotonic ticks from wall time](https://pkg.odin-lang.org/core/time/).
Rust's [Instant contract](https://doc.rust-lang.org/std/time/struct.Instant.html)
distinguishes monotonic measurements from wall time and documents platform
differences. Lua's [host-selected library registration](https://www.lua.org/manual/5.4/manual.html#luaL_requiref)
and [explicit coroutine cleanup](https://www.lua.org/manual/5.4/manual.html#lua_closethread)
are useful embedding precedents. These are semantic references, not ports.

Read-only filesystem metadata and size-limited regular-file reads follow after
the error/owned-byte contracts. Internal descriptors must close on every exit,
and returned bytes must outlive the native call. The existing build reader's
empty-success result for a missing file is not the public filesystem contract;
its root is a resolution base rather than containment. Process execution,
cancellation and networking add request lifetime and asynchronous cleanup and
remain subsequent work. Portable packages can proceed independently.
