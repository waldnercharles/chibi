#ifndef CHIBI_TEST_FLOAT_CONVERSION_CASES_H
#define CHIBI_TEST_FLOAT_CONVERSION_CASES_H

/* Floating conversions both compilers admit or reject alike, and the literal
 * answers of the admitted ones, independent of compiler folding or runtime
 * cast helpers. Runtime specials and exact bits also have C-fed tests. */
typedef struct
{
    const char *name, *source;
    bool accepted;
    const char *arguments, *expected;
    bool constant;
} float_conversion_case;
static const float_conversion_case FLOAT_CONVERSION_CASES[] = {
    { "implicit-narrow-declaration",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-assignment",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "0.0; result = value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-return",
      "package facts\npub Fold :: (value: f64) -> number { return value; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (value: f64) -> number { return Take(value); }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-typed-constant",
      "package facts\nWide: f64 : 16777217.0;\nNarrow: number : Wide;\npub "
      "Fold :: (value: f64) -> number { return Narrow; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true },
    { "implicit-narrow-typed-constant-return",
      "package facts\nWide: f64 : 16777217.0;\npub Fold :: (value: f64) "
      "-> number { return Wide; }\n",
      false,
      "[1.5]",
      "[1.5]",
      true },
    { "explicit-narrow-declaration",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "number(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-assignment",
      "package facts\npub Fold :: (value: f64) -> number { result: number = "
      "0.0; result = number(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-return",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (value: f64) -> number { return Take(number(value)); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-declaration",
      "package facts\npub Fold :: (value: number) -> f64 { result: f64 = "
      "value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-assignment",
      "package facts\npub Fold :: (value: number) -> f64 { result: f64 = "
      "0.0; result = value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-return",
      "package facts\npub Fold :: (value: number) -> f64 { return value; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-argument",
      "package facts\nTake :: (value: f64) -> f64 { return value; }\npub "
      "Fold :: (value: number) -> f64 { return Take(value); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-declaration",
      "package facts\npub Fold :: (unused: number) -> number { result: number "
      "= "
      "1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-assignment",
      "package facts\npub Fold :: (unused: number) -> number { result: number "
      "= "
      "0.0; result = 1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-return",
      "package facts\npub Fold :: (unused: number) -> number { return 1.5; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-argument",
      "package facts\nTake :: (value: number) -> number { return value; }\npub "
      "Fold :: (unused: number) -> number { return Take(1.5); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "round-even-lower",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[16777217]",
      "[16777216.0]" },
    { "round-even-upper",
      "package facts\npub Fold :: (value: f64) -> number { return "
      "number(value); }\n",
      true,
      "[16777219]",
      "[16777220.0]" },
    { "explicit-overflow",
      "package facts\npub Fold :: (value: f64) -> bool { x: number = "
      "number(value); return x > 0.0 && x == x + x; }\n",
      true,
      "[1e300]",
      "[true]" },
    { "explicit-negative-overflow",
      "package facts\npub Fold :: (value: f64) -> bool { x: number = "
      "number(value); return x < 0.0 && x == x + x; }\n",
      true,
      "[-1e300]",
      "[true]" },
    { "explicit-nan",
      "package facts\npub Fold :: (unused: f64) -> bool { z: f64 = 0.0; "
      "x: number = number(z / z); return x != x; }\n",
      true,
      "[1.5]",
      "[true]" },
    { "explicit-negative-zero",
      "package facts\npub Fold :: (unused: f64) -> bool { z: f64 = -0.0; "
      "x: number = number(z); return x == 0.0 && 1.0 / x < 0.0; }\n",
      true,
      "[1.5]",
      "[true]" },
    { "constant-round-even",
      "package facts\nValue: number : number(f64(16777217.0));\npub Fold :: "
      "(unused: number) -> number { return Value; }\n",
      true,
      "[1.5]",
      "[16777216.0]",
      true },
};
#endif
