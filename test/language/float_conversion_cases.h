#ifndef CHIBI_TEST_FLOAT_CONVERSION_CASES_H
#define CHIBI_TEST_FLOAT_CONVERSION_CASES_H

/* Approved admission and literal answers, independent of compiler folding or
 * runtime cast helpers. Runtime specials and exact bits also have C-fed tests. */
typedef struct
{
    const char *message;
    int line, column;
} float_conversion_rejection;
typedef struct
{
    const char *name, *source;
    bool accepted;
    const char *arguments, *expected;
    bool constant;
    float_conversion_rejection stage_zero, stage_one;
} float_conversion_case;
static const float_conversion_case FLOAT_CONVERSION_CASES[] = {
    { "implicit-narrow-declaration",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      2,
                      40 },
      .stage_one = { "local value differs from its type", 2, 40 } },
    { "implicit-narrow-assignment",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "0.0; result = value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      2,
                      61 },
      .stage_one = { "assigned value differs from its local", 2, 61 } },
    { "implicit-narrow-return",
      "package facts\npub Fold :: (value: double) -> float { return value; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      2,
                      47 },
      .stage_one = { "returned value differs from the result type", 2, 40 } },
    { "implicit-narrow-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (value: double) -> float { return Take(value); }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      3,
                      52 },
      .stage_one = { "argument differs from its parameter type", 3, 47 } },
    { "implicit-narrow-typed-constant",
      "package facts\nWide: double : 16777217.0;\nNarrow: float : Wide;\npub "
      "Fold :: (value: double) -> float { return Narrow; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true,
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      3,
                      17 },
      .stage_one = { "a constant of another type needs a conversion", 3, 17 } },
    { "implicit-narrow-typed-constant-return",
      "package facts\nWide: double : 16777217.0;\npub Fold :: (value: double) "
      "-> float { return Wide; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true,
      .stage_zero = { "Incompatible types: expected float, got double; convert "
                      "with float(value)",
                      3,
                      47 },
      .stage_one = { "returned value differs from the result type", 3, 40 } },
    { "explicit-narrow-declaration",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "float(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-assignment",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "0.0; result = float(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-return",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (value: double) -> float { return Take(float(value)); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-declaration",
      "package facts\npub Fold :: (value: float) -> double { result: double = "
      "value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-assignment",
      "package facts\npub Fold :: (value: float) -> double { result: double = "
      "0.0; result = value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-return",
      "package facts\npub Fold :: (value: float) -> double { return value; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-argument",
      "package facts\nTake :: (value: double) -> double { return value; }\npub "
      "Fold :: (value: float) -> double { return Take(value); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-declaration",
      "package facts\npub Fold :: (unused: float) -> float { result: float = "
      "1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-assignment",
      "package facts\npub Fold :: (unused: float) -> float { result: float = "
      "0.0; result = 1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-return",
      "package facts\npub Fold :: (unused: float) -> float { return 1.5; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (unused: float) -> float { return Take(1.5); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "round-even-lower",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[16777217]",
      "[16777216.0]",
      .constant = false },
    { "round-even-upper",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[16777219]",
      "[16777220.0]",
      .constant = false },
    { "explicit-overflow",
      "package facts\npub Fold :: (value: double) -> bool { x: float = "
      "float(value); return x > 0.0 && x == x + x; }\n",
      true,
      "[1e300]",
      "[true]",
      .constant = false },
    { "explicit-negative-overflow",
      "package facts\npub Fold :: (value: double) -> bool { x: float = "
      "float(value); return x < 0.0 && x == x + x; }\n",
      true,
      "[-1e300]",
      "[true]",
      .constant = false },
    { "explicit-nan",
      "package facts\npub Fold :: (unused: double) -> bool { z: double = 0.0; "
      "x: float = float(z / z); return x != x; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = false },
    { "explicit-negative-zero",
      "package facts\npub Fold :: (unused: double) -> bool { z: double = -0.0; "
      "x: float = float(z); return x == 0.0 && 1.0 / x < 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = false },
    { "constant-round-even",
      "package facts\nValue: float : float(double(16777217.0));\npub Fold :: "
      "(unused: float) -> float { return Value; }\n",
      true,
      "[1.5]",
      "[16777216.0]",
      .constant = true },
    /* Fixed literal answers: two halfway parities, source dependencies,
     * signed zero, overflow and exact powers of two. No production cast
     * computes an expected value. Body arithmetic exercises ordinary lowering. */
    { "constant-round-even-upper",
      "package facts\nValue: float : float(double(16777219.0));\npub Fold :: "
      "(unused: float) -> float { return Value; }\n",
      true,
      "[1.5]",
      "[16777220.0]",
      .constant = true },
    { "constant-round-even-negative",
      "package facts\nValue: float : float(double(-16777217.0));\npub Fold :: "
      "(unused: float) -> float { return Value; }\n",
      true,
      "[1.5]",
      "[-16777216.0]",
      .constant = true },
    { "constant-forward-widen",
      "package facts\nWide: double : Narrow;\nNarrow: float : 1.5;\npub Fold "
      ":: (unused: float) -> double { return Wide; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = true },
    { "constant-named-body-arithmetic",
      "package facts\nValue: float : 1.5;\npub Fold :: (unused: float) -> "
      "float { return Value + Value; }\n",
      true,
      "[1.5]",
      "[3.0]",
      .constant = true },
    { "constant-explicit-overflow",
      "package facts\nWide: double : 1e300;\nValue: float : float(Wide);\npub "
      "Fold :: (unused: float) -> bool { return Value > 0.0 && Value == Value "
      "+ Value; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-negative-zero-alias",
      "package facts\nZero: float : 0.0;\nNegative: float : -Zero;\nWide: "
      "double : Negative;\npub Fold :: (unused: float) -> bool { return Wide "
      "== 0.0 && 1.0 / Wide < 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-subnormal-widen",
      "package facts\nTiny: float : "
      "1."
      "401298464324817070923729583289916131280261941876515771757068283889791082"
      "68586060148663818836212158203125e-45;\nWide: double : Tiny;\npub Fold "
      ":: (unused: float) -> bool { return Wide == "
      "1."
      "401298464324817070923729583289916131280261941876515771757068283889791082"
      "68586060148663818836212158203125e-45; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-subnormal-tie-zero",
      "package facts\nValue: float : "
      "float(double(7."
      "006492321624085354618647916449580656401309709382578858785341419448955413"
      "42930300743319094181060791015625e-46));\npub Fold :: (unused: float) -> "
      "bool { return Value == 0.0 && 1.0 / Value > 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
};
#endif
