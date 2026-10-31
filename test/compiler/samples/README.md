# Compiler samples

Small programs the authored compiler must accept and run, or reject with an
exact diagnostic, ported from the sample tables of
`test/language/package_compiler_chibi.c`. Each folder is a `chibi test` root,
one per former sample group. Each sample is a folder of packages: `facts`, and
`lib` where the sample imports one, as `project:<sample>/lib`. `chibi test`
compiles each sample in its own closure.

- A sample that runs holds `@test Calls` in `facts/SampleTests.chibi`, asserting
  the results its functions return.
- A sample the compiler rejects ends its failing file with
  `// error: LINE:COL: message`, as Zig's test cases do. It passes only when the
  compile fails with exactly that first diagnostic.
- A call that must fail at run time is an `@test` that calls
  `testing.ExpectFailure` (`core:testing`) with the kind of failure right
  before it.
