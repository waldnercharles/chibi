# Whole-function integer operations

September 12, 2026, following `4c1850fd`.
[Raw trials and executable hashes](whole-integers-results.json).

## Native coverage

The whole-function IR now supports signed I32/I64 Add, Subtract, Multiply,
Divide, Modulo, Negate, Abs, Min, Max and Clamp; the existing I32 comparison
nodes; U32 Has Any Flags; and explicit numeric casts. Arguments, results,
variables and local snapshots support the scalar integer widths. SLJIT emits
integer instructions, typed loads/stores and branches for these operations.
They introduce no runtime C helper calls. The portable VM and generated C
consume the same lowered instructions.

To Byte, To Integer and To Integer64 truncate and saturate floating-point
inputs, testing NaN and bounds before conversion. NaN produces zero. To Real
accepts U8/I32/I64; I32-to-I64 sign-extends. These match the existing explicit
conversion nodes. Ordinary connections still coerce only F32/F64.

Signed Add/Subtract/Multiply/Negate/Abs now have explicit wrapping semantics in
the oracle, VM and generated C. In particular, negating or taking the absolute
value of MIN retains MIN. Divide and Modulo reject zero and MIN/-1 before
executing a division. This removes C undefined behavior from the oracle's
overflow and remainder cases.

Quotient and remainder over identical input snapshots share a single checked
DIVMOD operation. Evaluation order determines the failing source node; fusion
does not cross an execution safe point. The secondary result currently uses a
stack slot. Integer bodies use the checked compiler path, so partial variable
writes remain visible if a later operation fails. The guarded optimizer must
learn about those failures before it can defer integer stores.

Pure inlining has separate source scopes from effectful frames. An error inside
a pure function nested in an effectful call reports the original graph, node,
pin and complete logical call stack without creating a runtime child activation.
Pure source scopes do not introduce new flow budgets.

Native 32-bit targets still exclude whole I64/U64 bodies pending register-pair
lowering. Their general compiled path remains available. WASM uses the portable
VM and executes the wide-integer fixtures.

## Performance

Apple M4 Pro, Clang 23.1.1, Release `-O3`, sanitizers disabled. Five trials of two
million calls rotate backend order. The integer body returns quotient, remainder
and `A*A+B` through the same typed ABI for all backends. Reference arithmetic
runs outside the timed interval. Timing includes input updates, invocation,
status checking and an output checksum.

| Body | SLJIT | Generated C | Bytecode | SLJIT / C |
| --- | ---: | ---: | ---: | ---: |
| I32 | 3.299 ns | 2.764 ns | 35.639 ns | 1.194 |
| I64 | 3.282 ns | 2.789 ns | 36.630 ns | 1.177 |

These medians meet the existing 1.5 performance bound. The scalar, argument,
inlined-call, pure-body, math-body and scheduler gates also pass. The complete
raw ranges include desktop scheduling outliers; these timings are not hard
latency bounds.

An initial implementation saved five context registers even in integer bodies
without C calls. The final emitter uses scratch registers when the target has
enough available. Actual C calls and targets with fewer scratch registers retain
the saved convention. Values live across C calls still spill or use preserved
registers. Sequential before/after trial medians fell from 4.238 to 3.299 ns for
I32 and from 4.033 to 3.282 ns for I64. Both variants and their hashes are retained
in the results file; these were not interleaved trials.

The isolated bodies show an approximately elevenfold gap between SLJIT and
bytecode. Similar Arena timings do not contradict that result. Most authored
events still use the general compiled executor and its shared runtime and host
operations. This change does not establish an Arena frame-time improvement.

## Acceptance and remaining work

The tests compare oracle, bytecode and native execution over integer limits,
overflow, signed comparisons, flags, narrow values, saturating casts, NaN and
infinities, loops, budgets, values surviving math calls, and nested errors.
Compiled pure calls assert zero callee runtime-store allocation and zero oracle
calls. Generated-C checks additionally cover missing arguments, both
division/remainder evaluation orders, partial state, budgets and source scopes.

Reproduce with `make test`, `make build`, `make web`,
`build/test_suite --behavior-parity`, `build/test_suite --compiled-arena`, and
`python3 tools/bench_behavior.py --trials 5 --iterations 2000000` using the
optimized `build-performance` directory. The native build with
`CHIBI_ENABLE_SJLIT=OFF` must pass `check-behavior-backend` by rejecting execution.
The Emscripten smoke asserts bytecode, no SLJIT, and zero oracle calls through
nested integer bodies, loops and a saturating I64 conversion.

The strict catalog gate remains incomplete: events 1/33, effectful locals 1/31,
pure locals 26/39, libraries 1/4. Integer exclusions now expose subsequent
component bindings and engine operations. Next priorities are vector/struct
field lowering and direct component reads/writes, followed by the remaining
loops, managed values, host effects, resumable frames and whole-function
instrumentation. [The migration checklist](behavior-completion.md) remains open.
