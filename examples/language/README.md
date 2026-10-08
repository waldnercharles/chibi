# Executable SDK examples

These scripts use the small catalog declared in `test/language/program.c`.
They are SDK consumer examples; loading them as native game project assets
requires the corresponding project declarations.

- `accumulator.chibi` handles a typed `Impulse(amount: double, enabled: bool)`
  event and changes persistent state when enabled.
- `motion.chibi` runs for entities with both `Position` and `Velocity`, moves
  `Position.x` using the step's `dt`, then waits ten seconds. Removing `Velocity`
  cancels the pending work before it can change `Position.y`.
- `binding.chibi` keeps `Position.y` equal to twice `Position.x`.

The fixture supplies `Position { x: double, y: double }`,
`Velocity { x: double }`, the typed `Impulse` event, the `Delay` action,
and the `self` and `dt` globals. Each source file is compiled and executed
through both the bytecode VM and SLJIT. The tests check state and ECS results;
an inline negative case also rejects cyclic bindings.

From the repository root:

```sh
make language-build
make test-language-program
```

The exact test dispatcher selector is `--program`. The make target runs it
under the shared 60-second supervisor.
