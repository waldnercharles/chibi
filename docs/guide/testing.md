# Testing

Tests are Chibi functions. Mark a package-level function that takes and
returns nothing with `@test`, and check what it computes with `assert`:

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

`assert(condition, message)` does nothing when the condition holds. When it
does not, the test fails with `Assertion failed: message` at the position of
the `assert` call, even inside a function the test calls. Without a message,
it reports the condition's source text instead.

## Running tests

`chibi test [ROOT]` prepares every package below ROOT, the working directory
by default, and runs each test alone:

```text
PASS scoring.DoublesPoints
FAIL scoring.HalvesPoints
  /work/scoring/Scoring.chibi:14:5: Assertion failed: four halves to two
2 tests, 1 failed
```

It exits 0 when every test passes and 1 when one fails or a package is
invalid, so it fits a pre-commit hook or CI.

## What a test can do

A test runs without a game. Its packages may call the functions that need no
running game: core, text, arrays, vectors, math and value operations. A
package that calls anything else, such as audio or entities, does not compile
for `chibi test`, which reports where.

A test also fails:

- on any runtime failure, such as reading past the end of an array;
- when it waits, with `await` or `yield`, or starts a task, because nothing
  would resume it;
- after 100 million instructions, so a loop that never ends still stops.

## Where tests live

ROOT holds only package files, each starting with `package NAME`, and tests
sit beside the code they check. A game project's behaviors and libraries are
not packages, so they hold no tests yet.
