# Deletion and tests

## Deletion

Code volume is a cost: every line is read, built and maintained. A replacement is not finished while the replaced mechanism is still reachable.

- **Survivors.** Is an old implementation, alias, flag or legacy path still reachable next to its replacement?
- **Twin implementations.** Does the change extend both of two implementations of the same behavior? Extend the one that will remain, and retire the other.
- **Unused code.** Are there unused locals, parameters, imports or declarations, variables declared mutable that are never mutated, or new declarations with no caller?
- **Leftovers.** Are there dead experiments, commented-out code, TODOs the diff already completed, or comments describing old behavior?
- **Accreted special cases.** Is a special case being added to a subsystem that has already accreted many? Name the existing ones; the fix may be a representation change instead.
- **Unexplainable code.** Is this code the author couldn't explain after a few weeks away? Rewrite it now, while the context is fresh.
- **Net size.** Does the diff add more lines than the behavior it adds justifies? Name the infrastructure that does nothing by itself.

## Tests

- **Transform tables.** Is each pure transform tested with tables of inputs and expected outputs, without mocks?
- **Growth test.** For a change to a pass, the build graph or a baker over user-sized input, is there a test at n and 4n that checks the operation-count ratio, so quadratic growth fails in CI even at fixture sizes?
- **Fuzzing.** Does each parser of external input have a fuzz harness and a checked-in, minimized corpus that is replayed as a test?
- **Exhaustive float tests.** Does a unary `f32` function have an opt-in test over all 2³² inputs against a reference?
- **Performance oracle.** Does an optimized or specialized path have a simple reference implementation it is checked against on the same inputs?
- **Performance tripwire.** For a change whose point is speed or memory, is there a benchmark selector or a recorded before-and-after number that would show a regression?
- **Real path.** Is the behavior exercised through a scene, a scenario or a real program, not only through a helper? Is "the compiler compiles itself" the only evidence that a language change works?
- **General rule.** Does the test check the general rule, or hard-code the current corpus or the shape of the implementation?
- **Rejection path.** Is the rejection of invalid external input tested?
- **Determinism.** Does the test rely on accidental ordering, timing or shared global state? Was a bootstrap or scenario-hash expectation changed deliberately, with the reason stated?
- **Flaky failures.** Was a flaky failure reproduced in a loop before being changed? Does a workaround say what remains unexplained?
- **Layout refactors.** Does a representation change keep a test that runs on both the old and the new layout until the old one is deleted?
- **Regression.** Does a fixed bug have a case that fails without the fix?
- **Registration.** Is a new selector registered in exactly one place?
