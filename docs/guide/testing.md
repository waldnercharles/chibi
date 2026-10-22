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

Each test runs alone, without a game. Packages can use core, text, array,
vector, math and value operations. Functions that need a running game, such
as audio or entity functions, cannot compile for `chibi test`.

A test fails when it:

- reads past an array boundary or causes another runtime error
- waits with `await` or `yield`
- starts a task
- reaches 100 million instructions

TODO: Tests in game behavior and library source. The command currently accepts
package source only.
