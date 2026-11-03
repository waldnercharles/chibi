# Native host clock

`host:clock.Now` and `Between` keep the public `ClockError`, `time.Instant`
and `time.Duration` interfaces. Native `Sample` normalizes the private
`RawSample` host operation. An ordinary native program starts without a
grant and returns `Denied`. The CLI does not grant clock access.

An embedding executable links `native_link.ClockHostRuntime(triple)` once.
Its `chibi_clock_grant_init(grant, provider, state)` initializes caller-owned
storage and assigns a distinct, nonzero process-local domain on every call.
It returns zero. A null grant or exhausted domain counter asserts. Images
cannot define another factory. The strong factory definition also prevents
linking two authorities into one executable.

The grant has the target's natural C layout:

| Field | Type |
| --- | --- |
| domain | u64 |
| provider | function pointer |
| state | pointer |

It occupies 24 bytes on the supported desktop targets and 16 bytes on
Wasm32. The provider has signature `i32(state, i64 *nanoseconds, i32 *error)`.
Status zero succeeds, one means unavailable, and two means failed. Every
other status asserts. A successful negative sample becomes `Failed` with
`ERANGE`; unavailable returns error code zero. Clock samples and domains
keep their integer bits throughout.

A null provider selects the platform implementation. Linux uses
`CLOCK_MONOTONIC`; macOS uses `CLOCK_UPTIME_RAW`. Windows and Wasm report
`Unavailable`. Each read uses stack scratch and allocates no memory.

Reloadable family images export `chibi_host_set_clock(grant)`. A null grant
revokes access. A nonnull grant requires a nonzero domain and provider;
rejection leaves the previous binding intact. Keep the grant and borrowed
provider state alive until every call has returned and the binding has been
revoked or its image has closed. Configure bindings only while calls are
quiescent. Provider state may change between calls. A provider may reenter
the family collector; the source operation retains its callers' managed
roots across that boundary.

The explicit Wasm path uses `native_driver.ModuleClock` and
`native_link.ClockWebAssemblyAdapter`. The caller supplies an actual
seven-pointer context prefix, 28 bytes on Wasm32. Its first five fields
retain the existing ABI. Slot numbers are zero based: slot five is the managed family owner and slot six
is the borrowed clock grant. A caller may append private callback state.
Callbacks receive the original context address.

`chibi_clock_program(context, context_bytes, grant, result)` rejects a null
context, null result, wrong prefix extent or malformed grant with status one,
before changing the context or result. Status zero publishes the authored
integer result. Status two preserves the source failure in context slot one
and leaves the result untouched. The adapter restores the previous grant on
both normal and source-failure returns. The context belongs to one synchronous
call at a time.

Generic five-pointer Wasm output rejects clock operations. The compile-only
asset facade keeps that existing five-pointer contract and never samples a
clock. The C VM implementation remains until its remaining consumers retire;
it is not a native fallback.

`test/native_aot/host_clock/run.sh COMPILER LLVM` covers the private compiler
boundary, factory guards, two loaded images and collector reentry at O0/O2.
Appending `EMCC WASM_LLVM NODE` also runs the real Wasm source and callback
tests, including legacy-context rejection and recovery after source failure.
