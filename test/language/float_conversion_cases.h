#ifndef CHIBI_TEST_FLOAT_CONVERSION_CASES_H
#define CHIBI_TEST_FLOAT_CONVERSION_CASES_H

/* Approved admission and literal answers, independent of compiler folding or
 * runtime cast helpers. Runtime specials and exact bits also have C-fed tests. */
typedef struct
{
    const char *name, *source;
    bool accepted;
    const char *arguments, *expected;
} float_conversion_case;
static const float_conversion_case FLOAT_CONVERSION_CASES[] = {
    { "implicit-narrow-declaration",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-assignment",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "0.0; result = value; return result; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-return",
      "package facts\npub Fold :: (value: double) -> float { return value; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (value: double) -> float { return Take(value); }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-typed-constant",
      "package facts\nWide: double : 16777217.0;\nNarrow: float : Wide;\npub "
      "Fold :: (value: double) -> float { return Narrow; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "implicit-narrow-typed-constant-return",
      "package facts\nWide: double : 16777217.0;\npub Fold :: (value: double) "
      "-> float { return Wide; }\n",
      false,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-declaration",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "float(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-assignment",
      "package facts\npub Fold :: (value: double) -> float { result: float = "
      "0.0; result = float(value); return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-return",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "explicit-narrow-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (value: double) -> float { return Take(float(value)); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-declaration",
      "package facts\npub Fold :: (value: float) -> double { result: double = "
      "value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-assignment",
      "package facts\npub Fold :: (value: float) -> double { result: double = "
      "0.0; result = value; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-return",
      "package facts\npub Fold :: (value: float) -> double { return value; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "implicit-widen-argument",
      "package facts\nTake :: (value: double) -> double { return value; }\npub "
      "Fold :: (value: float) -> double { return Take(value); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-declaration",
      "package facts\npub Fold :: (unused: float) -> float { result: float = "
      "1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-assignment",
      "package facts\npub Fold :: (unused: float) -> float { result: float = "
      "0.0; result = 1.5; return result; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-return",
      "package facts\npub Fold :: (unused: float) -> float { return 1.5; }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "contextual-literal-argument",
      "package facts\nTake :: (value: float) -> float { return value; }\npub "
      "Fold :: (unused: float) -> float { return Take(1.5); }\n",
      true,
      "[1.5]",
      "[1.5]" },
    { "round-even-lower",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[16777217]",
      "[16777216.0]" },
    { "round-even-upper",
      "package facts\npub Fold :: (value: double) -> float { return "
      "float(value); }\n",
      true,
      "[16777219]",
      "[16777220.0]" },
    { "explicit-overflow",
      "package facts\npub Fold :: (value: double) -> bool { x: float = "
      "float(value); return x > 0.0 && x == x + x; }\n",
      true,
      "[1e300]",
      "[true]" },
    { "explicit-negative-overflow",
      "package facts\npub Fold :: (value: double) -> bool { x: float = "
      "float(value); return x < 0.0 && x == x + x; }\n",
      true,
      "[-1e300]",
      "[true]" },
    { "explicit-nan",
      "package facts\npub Fold :: (unused: double) -> bool { z: double = 0.0; "
      "x: float = float(z / z); return x != x; }\n",
      true,
      "[1.5]",
      "[true]" },
    { "explicit-negative-zero",
      "package facts\npub Fold :: (unused: double) -> bool { z: double = -0.0; "
      "x: float = float(z); return x == 0.0 && 1.0 / x < 0.0; }\n",
      true,
      "[1.5]",
      "[true]" },
    { "constant-round-even",
      "package facts\nValue: float : float(double(16777217.0));\npub Fold :: "
      "(unused: float) -> float { return Value; }\n",
      true,
      "[1.5]",
      "[16777216.0]" },
};
#endif
