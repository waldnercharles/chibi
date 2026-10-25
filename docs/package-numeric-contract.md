# Numeric prerequisites for package ports

Package fidelity must specify widths and arithmetic behavior instead of
inheriting the host compiler's rules. The owner decision of 2026-10-24 is that
no integer operation fails at run time, and zero answers the cases that have no
answer. It replaces the 2026-10-20 plan for trapping overflow and the
2026-10-21 rule that rejected shift counts; see
[language friction](language-friction.md#integer-overflow-wraps-silently).

## Run time

Implemented across the VM, SLJIT, generated C and Wasm, in
[`src/execution/scalar.h`](../src/execution/scalar.h) and
[`src/types/scalar.h`](../src/types/scalar.h):

- Integer add, subtract, multiply and negate wrap at the declared width,
  unsigned included.
- `x / 0` is 0 and `x % 0` is `x`. The signed minimum divided by `-1` wraps to
  the minimum, with remainder 0.
- A shift count at or past the resolved operand width shifts every bit out: 0,
  or -1 for a signed right shift of a negative value. A negative count is read
  as unsigned, so it is past the width.
- `T(v)` between integers clamps a value that does not fit to the nearest
  bound; `T(wrap: v)` keeps the low bits. Floating-to-integer conversion
  truncates, saturates at the bounds and maps NaN to zero.

Hash, checksum and random-generator ports can rely on wrapping without
spelling it, though a development build reports it (see below). Because
nothing fails, an unused integer operation or conversion can be pruned, and
the optimizer may delay stores past one.

## Compile time

Implemented in stage zero
([`semantic_constant.c`](../src/language/semantic_constant.c)); pending in
stage one:

- Mixing integer types in a binary operation or compound assignment is an
  error, except a shift count. `char` matches the 32-bit and wider integers.
- A shift count must be unsigned, and a negative constant count is an error.
  A shift has its left operand's type; an untyped literal there takes its
  type from context, never from the count.
- An integer constant expression is evaluated exactly and must fit its type,
  including the operand of `T(v)`; `T(wrap: v)` keeps the low bits of a
  constant that does not fit. In a constant expression, a shift count above
  4096, the exact evaluator's limit, is an error.
- A constant zero divisor, including in `/=` and `%=`, and negating an
  unsigned constant are errors.
- `T(clamp: v)` is removed, since `T(v)` clamps.

Stage zero folds each integer constant expression to its exact value before
lowering, and a call evaluated at compile time follows the run-time rules.

## Development builds

A development build reports each overflow, zero divisor and clamping
conversion once per source site. The program keeps the values a shipped build
computes. A host enables reports with `execution_integer_reports_enable(true)`
before it publishes modules; Studio does so at startup. Modules published
afterwards carry the checks
([`src/execution/integer_report.c`](../src/execution/integer_report.c)):

- Integer add, subtract, multiply, divide, remainder, negation and absolute
  value report, and so does `T(v)` to an integer type. `T(wrap: v)` and
  address arithmetic never report.
- A conversion from a float reports NaN and a value whose truncation does not
  fit.
- The VM checks the operands. SLJIT emits the checks inline: the overflow or
  carry flag, a narrow result that truncation changes, the high word of an
  unsigned multiply, a zero divisor, the signed minimum divided by -1 and the
  conversion bounds. An event stores one byte for its site and calls nothing.
- Wasm runs reportable operations through the machine helper, so the VM
  checks them.
- Generated C, LLVM native code and shipped builds have no checks. Neither do
  the generic arithmetic natives in
  [`src/runtime/arithmetic.c`](../src/runtime/arithmetic.c) yet.
- The guarded fast body does not fold a constant operation that reports.

`execution_module_take_integer_reports` returns each site of a module and its
imports that reported since the last take, with its source range and node.
`execution_module_reset_integer_reports` lets every site report again, as at
the start of a Play session.

Intentional wrapping, such as a hash, reports too. Marking the operations
inside `T(wrap: ...)` as intentional is the next step.

## Prior art

| Reference | Relevant behavior |
| --- | --- |
| [Odin integer overflow](https://odin-lang.org/docs/overview/#integer-overflow) | Overflow wraps. Shifts past the width give 0. `-integer-division-by-zero:zero` makes `x / 0` 0 and `x % 0` `x`. |
| [Go integer overflow](https://go.dev/ref/spec#Integer_overflow) | Overflow wraps; constants are exact and must fit. |
| [Rust overflow](https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow) | Debug builds panic and release builds wrap; float-to-int `as` saturates. Chibi keeps Rust's bug-finding in development reports, without a behavior split. |
| [Zig 0.15.2 overflow](https://ziglang.org/documentation/0.15.2/#Integer-Overflow) | Ordinary overflow is illegal; explicit wrapping operators are available. |

## Acceptance

`test/language/integer_semantics.c` (`tool language-integer-semantics`) runs
every width's boundary values through stage zero and stage one, on bytecode and
SLJIT. Prepared instructions keep their encoding: a cast mode of EXACT clamps at
run time like CLAMP, and compile time still rejects a constant that does not fit.
Prepared format 21 marks code compiled under these semantics, so code prepared
for format 20, which could trap, is rebuilt instead of loaded.
