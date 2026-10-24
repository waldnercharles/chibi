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
spelling it. Because nothing fails, an unused integer operation or conversion
can be pruned, and the optimizer may delay stores past one.

## Compile time

Pending, in stage zero and stage one:

- Mixing integer types in a binary operation is an error, except a shift count.
- A shift count must be unsigned.
- An integer constant expression is evaluated exactly and must fit its type.
- A constant zero divisor and negating an unsigned constant are errors.
- `T(clamp: v)` is removed, since `T(v)` clamps.

## Development builds

Pending: a development build reports each overflow, zero divisor and clamping
conversion once per source site, as a Studio diagnostic. The program keeps the
values a shipped build computes, and a shipped build has no checks.

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
