# Numeric prerequisites for package ports

The existing owner decision is checked integer arithmetic on every backend,
with explicit wrapping inside `T(wrap: expression)`. Implement that contract
before adding hash, checksum and random-generator packages that depend on
overflow. Package fidelity must specify widths and arithmetic behavior instead
of inheriting the host compiler's rules.

Status on 2026-10-21: the default checked behavior and lexical wrapping direction
are approved in [language friction](language-friction.md#integer-overflow-wraps-silently).
The owner also approved rejecting invalid shift counts and discarding
shifted-out bits on 2026-10-21. That shift contract is implemented locally across
the execution backends and typed constant folders; see the
[verification checkpoint](package-library-plan.md#shift-prerequisite-evidence-2026-10-21).
Default checked arithmetic and lexical wrapping remain unimplemented. Other
edge-case recommendations below remain proposed for review.

## Current behavior

- [`src/execution/scalar.h`](../src/execution/scalar.h) performs integer add,
  subtract, multiply and negate through unsigned bits and truncates to the
  declared width. Division and remainder reject a zero divisor and signed
  minimum divided by `-1`. Bit shifts reject counts outside the resolved operand
  width, including negative counts, and discard shifted-out bits.
- [`docs/code-language.md`](code-language.md#values-and-operators) documents wrapping
  arithmetic and checked/wrap/clamp integer conversions. Floating-to-integer
  conversion truncates, saturates at the bounds and maps NaN to zero; this
  package program has not approved changing that behavior.
- `T(wrap: value)` currently selects a conversion mode. It does not yet carry
  a wrapping policy into each arithmetic operation in its argument. See
  [`semantic_calls.c`](../src/language/semantic_calls.c),
  [`semantic_comptime.c`](../src/language/semantic_comptime.c) and stage one's
  [`Steps.chibi`](../tools/compiler/package_compiler/Steps.chibi).
- The active typed constant evaluator in
  [`scalar_constant.c`](../src/language/scalar_constant.c) and the separate
  authored evaluator must agree with runtime shifts. The arbitrary-precision
  evaluator in [`numeric.c`](../src/language/numeric.c) is a private draft,
  currently unwired to source semantics. Its exact shifts have their own
  resource limit and remain unchanged.

## Prior art and the chosen direction

| Reference | Relevant behavior | Consequence for a Chibi port |
| --- | --- | --- |
| [Odin integer overflow](https://odin-lang.org/docs/overview/#integer-overflow) | Unsigned arithmetic wraps; signed overflow is defined and does not panic. | Preserve required modulo arithmetic explicitly in ported algorithms. |
| [Go integer overflow](https://go.dev/ref/spec#Integer_overflow) | Integer overflow has defined behavior; unsigned arithmetic is modulo the width. | Byte/word algorithms can depend on overflow without spelling it. Chibi ports must spell it. |
| [Rust overflow](https://doc.rust-lang.org/reference/expressions/operator-expr.html#overflow) | Checks depend on compilation settings; division by signed minimum and invalid shifts have specific rules. | Chibi's approved behavior must agree across build modes and backends. |
| [Zig 0.15.2 overflow](https://ziglang.org/documentation/0.15.2/#Integer-Overflow) | Ordinary overflow is illegal; explicit wrapping operators are available. | Explicit wrapping separates algorithm intent from accidental overflow. |
| [C# checked and unchecked](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/statements/checked-and-unchecked) | The overflow context applies to operations textually inside it, not to called function bodies. | This is the model already selected for Chibi's wrapping expression. |

## Proposed completion of the approved contract

1. Check every typed integer `+`, `-`, `*` and unary negation, including compound
   assignment. Overflow fails the invocation with a diagnostic; a constant
   expression that necessarily overflows fails compilation. Debug and Release
   use the same rule.
2. `T(wrap: expression)` makes integer arithmetic written inside `expression`
   wrap at each operation's own resolved type width, then performs the existing
   wrapping conversion to `T`. It does not silently retarget all intermediates
   to `T`, and it does not change arithmetic inside called functions.
3. Keep division/remainder by zero and signed minimum divided by `-1` as failures,
   including inside `wrap`. This preserves the current runtime's boundary.
   Wrapping division would need its own deliberate API if a port requires it.
4. Approved on 2026-10-21: reject shift counts outside `0 <= count < width` in
   ordinary and wrapping expressions. Shifts remain bit operations that discard
   outgoing bits. Implemented locally, replacing masked-count behavior.
   Rotates should have named operations with their own count contract.
5. Preserve bitwise AND/OR/XOR/NOT as fixed-width bit operations. Preserve the
   shared language rule that signed right shifts extend the sign bit and
   unsigned right shifts insert zero bits. Package contracts cite that rule
   and choose explicit byte order for serialized data.
6. Keep `T(clamp: value)` as a conversion. The approved arithmetic decision does
   not introduce a lexical saturating-arithmetic mode. Recommend that every
   nested conversion retain its own explicit mode: an outer wrapping expression
   does not make an inner checked conversion wrap. Arithmetic within the outer
   expression still uses its lexical wrapping policy.

Existing syntax, showing intended behavior after implementation:

```chibi
NextHash :: (hash: uint64, value: byte, prime: uint64) -> uint64 {
    return uint64(wrap: (hash ^ uint64(value)) * prime)
}

BufferSize :: (count: int, stride: int) -> int {
    return count * stride
}
```

`NextHash` intentionally wraps its multiplication. `BufferSize` must fail when
its multiplication exceeds `int`; it must not wrap into a small allocation.
`uint64(wrap: BufferSize(count, stride))` must still fail inside `BufferSize`.
These examples state the proposed acceptance behavior, not a current test pass.

## Implementation boundary

The semantic analyzer must preserve the selected arithmetic mode through typed
expressions, folding, lowering and prepared instructions. Stage one must emit
the same behavior. Audit interpreter, SLJIT and generated backends for direct
arithmetic paths; the scalar helper alone is not the complete implementation.
Keep language arithmetic distinct from compiler-generated pointer/index
operations so an incidental internal use of `BF_ADD` is not changed blindly.

Before changing prepared instruction meaning, inspect artifact/version and
bootstrap recovery compatibility. Choose the smallest explicit representation
that can distinguish checked and wrapping operations, and rebuild the bootstrap
only through its documented reproducible procedure when required.

The shift increment uses prepared format 19 to reject format-18 programs that
were built with masked counts. It updates the authored writer and target guards
and regenerates the bootstrap. The optimizer keeps a shift in the canonical
path unless its count is proven valid, so an unused result cannot suppress a
failure or lose earlier writes.

Audit existing compiler and strings hashes when making default arithmetic
checked. Their masks after multiplication do not prevent an earlier overflow.
Add explicit wrapping around the arithmetic that requires it. Audit bounds,
capacity and range loops for accidental overflow and fix their arithmetic at
the owning operation. Existing floating precision and conversion policy remain
owned by the language reference.

## Acceptance and remaining decisions

Exercise boundary values for each supported signed/unsigned width, including
zero, minimum, maximum, multiplication, compound assignment, negative values,
division, remainder and shifts. Compare literals with equivalent runtime input,
stage zero with stage one, and the supported execution backends. Cover lexical
wrapping around a call whose own body still checks arithmetic.

Use a real hash/checksum reference vector to prove intentional wrapping and a
buffer-size consumer to prove overflow cannot become a smaller allocation.
Extend the owning C/pico_unit tests using the [testing instructions](testing.md)
and report unrun target coverage explicitly.

Shift counts and shifted-out bits are settled. The next semantic review must
settle the remaining recommendations, including nested conversion boundaries
inside a wrapping expression. Implement the approved prerequisite before
assigning overflow-dependent package algorithms.
