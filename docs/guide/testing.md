# Testing

A test is a Chibi function. To write a test:

1. Declare a package-level function that has no parameters and no result.
2. Put `@test` before the function.
3. Use `assert` to check the values that the function calculates.

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

`assert(condition, message)` does nothing when the condition is true. When the
condition is false, the test fails with `Assertion failed: message`. The
failure shows the position of the `assert` call. This is also true when the
`assert` call is in a function that the test calls. Without a message, the
failure shows the condition's source text instead.

## Running tests

`chibi test [ROOT]` prepares each package below ROOT and runs each test alone.
ROOT is the working directory by default.

```text
PASS scoring.DoublesPoints
FAIL scoring.HalvesPoints
  /work/scoring/Scoring.chibi:14:5: Assertion failed: four halves to two
2 tests, 1 failed
```

The command exits with 0 when all tests pass. It exits with 1 when a test fails
or a package is not valid. Thus you can use it in a pre-commit hook or in CI.

## What a test can do

A test runs without a game. Its packages can call the functions that do not
need a running game: core, text, arrays, vectors, math and value operations. A
package that calls a different function, such as an audio or entity function,
does not compile for `chibi test`. The command reports the location.

A test also fails in these conditions:

- A runtime failure occurs, such as a read past the end of an array.
- The test waits with `await` or `yield`, or it starts a task. Nothing would
  resume the test.
- The test runs for 100 million instructions. Thus a loop that does not end
  still stops.

## Where tests live

ROOT contains only package files. Each file starts with `package NAME`. Tests
are beside the code that they check. The behaviors and libraries of a game
project are not packages. Thus they contain no tests yet.
