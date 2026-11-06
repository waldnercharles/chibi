# Testing

Use `@test` on a package function with no parameters and no result.
Use `assert` to check its output.

## Write a test

Save this file as `packages/scoring/Scoring.chibi`:

```chibi
package scoring

Double :: (points: int) -> int {
    return points * 2;
}

@test
DoublesPoints :: () {
    assert(Double(3) == 6, "three doubles to six");
}
```

Run the package tests:

```sh
chibi test packages
```

```text
PASS scoring.DoublesPoints
1 tests, 0 failed
```

`chibi test [ROOT]` finds each package below ROOT. Without a path, it uses
the working directory. Each package source starts with `package NAME`.
Keep tests beside the package functions they check.

## Parallel execution

No CI setup is needed: independent tests run on a bounded thread pool by default.
The compiler infers which test closures have no external effects, including
called and imported functions. Those tests have private invocation storage.
Tests with random, host, state or other external effects run alone, between
completed parallel spans. Results and failures are printed in source order,
not completion order. The instruction limit remains per test.

The pool uses the runtime default: up to eight hardware threads (performance
cores on supported macOS hosts). For serial debugging, or when CI already
parallelizes multiple Chibi processes, set a smaller worker budget:

```sh
CHIBI_TEST_WORKERS=1 chibi test packages
```

`CHIBI_TEST_WORKERS` accepts 1–32, capped to available hardware. Leave it unset
for the automatic default. It controls authored `@test` functions, not the
repository’s C/pico_unit cases or its `CHIBI_TEST_JOBS` selector processes.

## Read a failure

`assert(condition, message)` fails when the condition is false. The failure
reports the location of the assertion, including assertions in called functions:

```text
FAIL scoring.DoublesPoints
  /work/packages/scoring/Scoring.chibi:9:5: Assertion failed: three doubles to six
1 tests, 1 failed
```

Without a message, `assert` reports the condition's source text.
The command exits with 0 when all tests pass. It exits with 1 when a test
fails or a package is invalid.

## Test limits

Each test has its own execution state, without a game. Packages can use core, text, array,
vector, math and value operations. Functions that need a running game, such
as audio or entity functions, cannot compile for `chibi test`.

A test fails when it:

- reads past an array boundary or causes another runtime error
- waits with `await` or `yield`
- starts a task
- reaches 100 million instructions

TODO: Tests in game behavior and library source. The command currently accepts
package source only.
