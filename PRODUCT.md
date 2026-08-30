# c-blueprints product direction

## Goal

Build a data-driven game engine and editor in which a human can create and run
Neon Swarm-style gameplay without hand-authoring gameplay code. The editor should
feel like a focused professional tool: discoverable, inspectable, undoable, and
verifiable through real interaction.

## Current priority

Clean the repository and establish one coherent vocabulary before adding more
features. Remove legacy Behavior Asset/spec/ADR machinery, consolidate correctness
checks, and make the current source of truth easy to find.

## Product model

- A **Prototype** authors entity composition and component defaults.
- A **Behavior Asset** is a reusable executable definition.
- A **Graph** is an executable behavior inside a Behavior Asset.
- A **Behavior Instance** is runtime state attached to an Entity.
- The **Prototype Editor** and **Behavior Editor** provide focused authoring
  surfaces over the same reflected data model.
- The **Inspector** and **Graph Canvas** are shared, context-sensitive surfaces.

## Principles

- Human authoring is the product; automation must exercise the same commands.
- Data and reflection are preferred over per-type editor code.
- Durable shared state belongs to Components; transient execution state belongs
  to Behavior Instances.
- Native compiled execution is the performance path. WebAssembly uses portable
  bytecode from the shared graph semantics.
- Keep the smallest model that supports the gameplay slice. Delete dead or
  duplicate abstractions instead of preserving them for history.
- Correctness tests are mandatory regression gates. Computer-use verification is
  mandatory for editor usability.

## First gameplay slice after cleanup

Author and run, entirely from the editor:

1. player movement driven by input;
2. one weapon that creates projectiles;
3. one enemy that can be targeted and damaged.

This slice must exercise reflected component fields, Behavior Asset composition,
entity creation, graph execution, runtime inspection, and save/reload. Wave
orchestration, progression, shops, bosses, and visual polish follow only after
this loop is usable and verified.

## Verification

- `test_suite` is a consolidated pico_unit regression executable.
- Benchmarks remain separate and report performance explicitly.
- Computer-use drives the real editor for authoring and runtime acceptance.
- A future JSON adapter may expose the typed C command/query API to an isolated
  external agent; it is not a second authoring model.
