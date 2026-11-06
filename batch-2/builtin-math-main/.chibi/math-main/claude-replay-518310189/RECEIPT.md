# Claude replay of commit 518310189 (tree of 29c61e2da + #783 f32-max fix), 2026-10-28
Lightweight replay (no producer), Release, native test_language, jobs 1. All pass:
package-tests 6 cases/338 asserts (incl. math_constants.* on BYTECODE and SLJIT, new boundary test); semantic 80/1113; native-calls 7/758; program 209/14083; borrow 31/900; navigation 36/393; component-write 15/3344.
Not run: asset-packaging-chibi, package-compiler-chibi-types(-compiled), math-native, any producer, wasm, Linux.
Snapshot and build dirs deleted after the check.
