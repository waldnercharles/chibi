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
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      2,
                      38 },
      .stage_one = { "local value differs from its type", 2, 38 } },
    { "implicit-narrow-assignment",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "0.0; result = value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      2,
                      60 },
      .stage_one = { "assigned value differs from its local", 2, 60 } },
    { "implicit-narrow-return",
      "package facts\npub Fold :: (value: f64) -> number { return value; }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      2,
                      45 },
      .stage_one = { "returned value differs from the result type", 2, 38 } },
    { "implicit-narrow-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (value: f64) -> number { return Take(value); }\n",
      false,
      "[1.5]",
      "[1.5]",
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      3,
                      50 },
      .stage_one = { "argument differs from its parameter type", 3, 45 } },
    { "implicit-narrow-typed-constant",
      "package facts\nWide: f64 : 16777217.0;\nNarrow: number : Wide;\npub "
      "Fold :: (value: f64) -> number { return Narrow; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true,
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      3,
                      18 },
      .stage_one = { "a constant of another type needs a conversion", 3, 18 } },
    { "implicit-narrow-typed-constant-return",
      "package facts\nWide: f64 : 16777217.0;\npub Fold :: (value: f64) "
      "-> number { return Wide; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true,
      .stage_zero = { "Incompatible types: expected number, got f64; convert "
                      "with number(value)",
                      3,
                      45 },
      .stage_one = { "returned value differs from the result type", 3, 38 } },
    { "explicit-narrow-declaration",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "number(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-assignment",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "0.0; result = number(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-return",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "explicit-narrow-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (value: f64) -> number { return Take(number(value)); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-declaration",
      "package facts\npub Fold :: (value: number) -> f64 { result: f64 = "
      "value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-assignment",
      "package facts\npub Fold :: (value: number) -> f64 { result: f64 = "
      "0.0; result = value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-return",
      "package facts\npub Fold :: (value: number) -> f64 { return value; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "implicit-widen-argument",
      "package facts\nTake :: (value: f64) -> f64 { return value; }\npub "
      "Fold :: (value: number) -> f64 { return Take(value); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-declaration",
      "package facts\npub Fold :: (unused: number) -> number { result: number "
      "= "
      "1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-assignment",
      "package facts\npub Fold :: (unused: number) -> number { result: number "
      "= "
      "0.0; result = 1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-return",
      "package facts\npub Fold :: (unused: number) -> number { return 1.5; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "contextual-literal-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (unused: number) -> number { return Take(1.5); }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = false },
    { "round-even-lower",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[16777217]",
      "[16777216.0]",
      .constant = false },
    { "round-even-upper",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[16777219]",
      "[16777220.0]",
      .constant = false },
    { "explicit-overflow",
      "package facts\npub Fold :: (value: f64) -> bool { x: number = "
      "number(value); return x > 0.0 && x == x + x; }\n",
      true,
      "[1e300]",
      "[true]",
      .constant = false },
    { "explicit-negative-overflow",
      "package facts\npub Fold :: (value: f64) -> bool { x: number = "
      "number(value); return x < 0.0 && x == x + x; }\n",
      true,
      "[-1e300]",
      "[true]",
      .constant = false },
    { "explicit-nan",
      "package facts\npub Fold :: (unused: f64) -> bool { z: f64 = 0.0; "
      "x: number = number(z / z); return x != x; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = false },
    { "explicit-negative-zero",
      "package facts\npub Fold :: (unused: f64) -> bool { z: f64 = -0.0; "
      "x: number = number(z); return x == 0.0 && 1.0 / x < 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = false },
    { "constant-round-even",
      "package facts\nValue: number : number(f64(16777217.0));\npub Fold :: "
      "(unused: number) -> number { return Value; }\n",
      true,
      "[1.5]",
      "[16777216.0]",
      .constant = true },
    /* Fixed literal answers: two halfway parities, source dependencies,
     * signed zero, overflow and exact powers of two. No production cast
     * computes an expected value. Body arithmetic exercises ordinary lowering. */
    { "constant-round-even-upper",
      "package facts\nValue: number : number(f64(16777219.0));\npub Fold :: "
      "(unused: number) -> number { return Value; }\n",
      true,
      "[1.5]",
      "[16777220.0]",
      .constant = true },
    { "constant-round-even-negative",
      "package facts\nValue: number : number(f64(-16777217.0));\npub Fold :: "
      "(unused: number) -> number { return Value; }\n",
      true,
      "[1.5]",
      "[-16777216.0]",
      .constant = true },
    { "constant-forward-widen",
      "package facts\nWide: f64 : Narrow;\nNarrow: number : 1.5;\npub Fold "
      ":: (unused: number) -> f64 { return Wide; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = true },
    { "constant-named-body-arithmetic",
      "package facts\nValue: number : 1.5;\npub Fold :: (unused: number) -> "
      "number { return Value + Value; }\n",
      true,
      "[1.5]",
      "[3.0]",
      .constant = true },
    { "constant-explicit-overflow",
      "package facts\nWide: f64 : 1e300;\nValue: number : number(Wide);\npub "
      "Fold :: (unused: number) -> bool { return Value > 0.0 && Value == Value "
      "+ Value; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-negative-zero-alias",
      "package facts\nZero: number : 0.0;\nNegative: number : -Zero;\nWide: "
      "f64 : Negative;\npub Fold :: (unused: number) -> bool { return Wide "
      "== 0.0 && 1.0 / Wide < 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-subnormal-widen",
      "package facts\nTiny: number : "
      "1."
      "401298464324817070923729583289916131280261941876515771757068283889791082"
      "68586060148663818836212158203125e-45;\nWide: f64 : Tiny;\npub Fold "
      ":: (unused: number) -> bool { return Wide == "
      "1."
      "401298464324817070923729583289916131280261941876515771757068283889791082"
      "68586060148663818836212158203125e-45; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-subnormal-tie-zero",
      "package facts\nValue: number : "
      "number(f64(7."
      "006492321624085354618647916449580656401309709382578858785341419448955413"
      "42930300743319094181060791015625e-46));\npub Fold :: (unused: number) "
      "-> "
      "bool { return Value == 0.0 && 1.0 / Value > 0.0; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-parenthesized-negative",
      "package facts\nValue: f64 : f64(-(1.5));\npub Fold :: (unused: "
      "number) -> f64 { return Value; }\n",
      true,
      "[1.5]",
      "[-1.5]",
      .constant = true },
    { "constant-parenthesized-positive",
      "package facts\nValue: number : number(+((1.5)));\npub Fold :: (unused: "
      "number) -> number { return Value; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = true },
    { "constant-nested-negative",
      "package facts\nValue: f64 : f64(-(-1.5));\npub Fold :: (unused: "
      "number) -> f64 { return Value; }\n",
      true,
      "[1.5]",
      "[1.5]",
      .constant = true },
    { "constant-explicit-negative-overflow",
      "package facts\nWide: f64 : -1e300;\nValue: number : number(Wide);\npub "
      "Fold :: (unused: number) -> bool { return Value < 0.0 && Value == Value "
      "+ Value; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-round-to-infinity",
      "package facts\nWide: f64 : "
      "340282356779733661637539395458142568448.0;\nValue: number : "
      "number(Wide);\npub Fold :: (unused: number) -> bool { return Value > "
      "0.0 "
      "&& Value == Value + Value; }\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
    { "constant-contextual-conversion-overflow",
      "package facts\nValue: number : number(1e300);\npub Fold :: (unused: "
      "number) -> number { return Value; }\n",
      false,
      "[1.5]",
      "[true]",
      .constant = true,
      .stage_zero = { "Numeric literal is outside the range of number", 2, 24 },
      .stage_one = { "Numeric literal is outside the range of number", 2, 24 } },
    { "constant-contextual-parenthesized-overflow",
      "package facts\nValue: number : number((1e300));\npub Fold :: (unused: "
      "number) -> number { return Value; }\n",
      false,
      "[1.5]",
      "[true]",
      .constant = true,
      .stage_zero = { "Numeric literal is outside the range of number", 2, 25 },
      .stage_one = { "Numeric literal is outside the range of number", 2, 25 } },
    { "constant-contextual-signed-overflow",
      "package facts\nValue: number : number(-(1e300));\npub Fold :: (unused: "
      "number) -> number { return Value; }\n",
      false,
      "[1.5]",
      "[true]",
      .constant = true,
      .stage_zero = { "Numeric literal is outside the range of number", 2, 26 },
      .stage_one = { "Numeric literal is outside the range of number", 2, 26 } },
    { "body-contextual-conversion-overflow",
      "package facts\npub Fold :: (unused: number) -> number { return "
      "number(1e300); }\n",
      false,
      "[1.5]",
      "[true]",
      .constant = false,
      .stage_zero = { "Numeric literal is outside the range of number", 2, 56 },
      .stage_one = { "Numeric literal is outside the range of number", 2, 56 } },
    { "constant-represented-double-overflow",
      "package facts\nValue: number : number(f64(1e300));\npub Fold :: "
      "(unused: number) -> bool { return Value > 0.0 && Value == Value + "
      "Value; "
      "}\n",
      true,
      "[1.5]",
      "[true]",
      .constant = true },
};
#endif
