---
name: chibi-graph-legibility
description: Make Chibi behavior, shader and field graphs read like code. Use when authoring, reviewing or cleaning up a graph asset (behavior, event, library, material, shape, shader library, field, system), or when a graph is hard to follow.
---

A legible graph reads like the code its author had in mind: formulas as
expressions, named values, one exec spine, no wire that crosses the canvas to
say something a name could. Work each graph through the passes below in order;
each ends on a checkable state.

## Passes

1. **See it.** Screenshot the graph as the editor draws it:
   `build/chibi --project-root <root> --open <Asset> [--graph "<Graph>"] --screenshot /tmp/<name>.png`,
   then read the image (downscale past 2000 px first: `sips -Z 1920`). Also
   `inspectGraph`. Done when you can state what the graph does in two
   sentences and list what reads badly.
2. **Record a baseline.** Before editing, capture evidence the change must
   preserve: shaders `previewShader` with `sample_grid` (8×8) and a fixed
   `time`; fields `previewField` `field_hash`; behaviors `runScenario`
   `world_hash` over enough frames and inputs to exercise every branch.
3. **Mechanical pass.** `optimizeGraph` with `dry_run: true`,
   `math_expressions: true`. Review, then apply with the returned hash. It
   folds getters into bindings, turns math trees into expressions and names
   values used three or more times as locals. Done when the baseline matches
   exactly.
4. **Naming pass.** Replace every placeholder the mechanical pass left:
   expression inputs `a`, `b`, … and locals `Value`, `Value2`, `X2`. Rename an
   expression input with `set_expression`, the same source with that one name
   replaced; renames pair old and new names in pin order, so change one name
   per operation (several operations may share a batch). Rename a local
   with `create_local` on its source output. Name by meaning in the asset's
   existing style (`crackMask`, `rim_glow`); single letters stay only as true
   math variables (`t`, `x`). Rewrite noisy literals as the author meant them:
   `x / 180`, not `0.0055555557 * x`, and accept baseline drift within float
   rounding for these. Done when no placeholder or generic name remains.
5. **Structure pass.** Apply every rule under Reference that the graph
   violates. Done when each rule is either satisfied or its exception is
   noted in the graph or asset description.
6. **Prove it.** `validateAsset`; re-run the baseline; `build/chibi_ctl
   --project-root <root> --format` after any hand edit; screenshot again and
   compare with pass 1.

## Reference

- **Expressions for formulas, nodes for control.** A calculation, assignment
  or condition is one Behavior Expression; sequencing, loops, waits and
  state changes stay explicit nodes on the exec spine.
- **Vectors, not components.** Bind or wire whole values: `length(delta)`,
  `move * speed`, `aim.y`, not paired `x`/`y` inputs and
  `sqrt(x * x + y * y)`. One Set Component with the fields it writes, not a
  Set per field.
- **Names over wires.** A value read in several places far apart becomes a
  local; state that must survive a frame becomes a variable. Locals are
  graph-only names and cost nothing at runtime.
- **Functions for repeated or nameable stages.** The same subgraph twice, or
  a stage with a name ("Crack Mask", "Formation Position"), becomes a
  function (Collapse to Function / `add_graph`) with named ports.
- **Switch, not branch chains.** Equality tests on one value choose with
  Switch on Int/Enum.
- **Language table.** Functions, their argument order and which graph kind
  has each: `docs/math-expressions.md`. Shader and field expressions are
  values only; `e` and `pi` are constants, never input names.
- **Shader wires.** Shader math nodes are wildcards that resolve on connect,
  so hand-wiring vectors to scalars is fragile; write the math as an
  expression, which infers widths and broadcasts scalars.
