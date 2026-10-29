---
name: chibi-graph-legibility
description: Make Chibi behavior, shader and field graphs read like code. Use when authoring, reviewing or cleaning up a graph asset (behavior, event, library, material, shape, shader library, field, system), or when a graph is hard to follow.
---

A legible graph uses expressions for formulas, named values for shared reads,
and explicit control flow. Executable behavior, library and system source lives
in `.chibi`; declarative assets live in KDL. Edit these files directly. Optional
graph layout and debug metadata never define execution.

## Passes

1. **Inspect.** Read the asset and its referenced declarations. Use the native
   controller's read-only inspection when resolved graph facts are needed.
   State what the asset does and which names or relationships are unclear.
2. **Record a baseline.** Choose a check that exercises the affected
   behavior. For rendering or simulation, follow `docs/agent-execution.md` and
   the test instructions in `docs/testing.md`. Preserve inputs, timing and output
   evidence so the same check can compare the edit. An unavailable check remains
   unverified. Use the native controller for routine captures.
3. **Simplify.** Edit source formulas, field bindings and named values directly.
   Preserve evaluation count, side effects, suspension and event order. Reusing
   a lazy expression does not guarantee that its producer runs only once.
   Confirm the chosen baseline still matches.
4. **Name.** Replace placeholder inputs and locals with names that describe their
   meaning in the asset's existing style, such as `crackMask` or `rim_glow`.
   Single letters remain useful for mathematical variables. Keep readable
   formulas such as `x / 180`; document any accepted rounding difference.
5. **Structure pass.** Apply every rule under Reference that the graph
   violates. Done when each rule is either satisfied or its exception is
   noted in the graph or asset description.
6. **Verify.** After hand edits run `build/bin/chibi_ctl --project-root <root>
   --format`, inspect its result, and rerun the selected check. For
   layout changes, compare a rendered view when the permitted inspection path
   supports it. Report checks run and any unverified behavior separately.

## Reference

- **Expressions for formulas, nodes for control.** A calculation, assignment
  or condition is one Behavior Expression; sequencing, loops, waits and
  state changes stay explicit nodes on the exec spine.
- **Vectors, not components.** Bind or wire whole values: `length(delta)`,
  `move * speed`, `aim.y`, not paired `x`/`y` inputs and
  `sqrt(x * x + y * y)`. One Set Component with the fields it writes, not a
  Set per field.
- **Names over wires.** Name a value used in several places. Choose a lazy
  expression, eager local or persistent state according to its evaluation and
  lifetime requirements. These choices are executable language semantics.
- **Functions for repeated or nameable stages.** The same subgraph twice, or
  a stage with a name ("Crack Mask", "Formation Position"), becomes a
  function with named parameters and results in the owning asset.
- **Switch, not branch chains.** Equality tests on one value choose with
  Switch on Int/Enum.
- **Inspect capabilities.** Use the language catalog and existing assets to find
  supported operations, exact parameter types and effects. Confirm callable
  names and argument order before editing source.
- **Language table.** Functions, their argument order and which graph kind
  has each: `docs/math-expressions.md`. Shader and field expressions are
  values only; `e` and `pi` are constants, never input names.
- **Shader wires.** Shader math nodes are wildcards that resolve on connect,
  so hand-wiring vectors to scalars is fragile; write the math as an
  expression, which infers widths and broadcasts scalars.
