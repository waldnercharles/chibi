---
title: >-
  c-blueprints ECS prototypes and behavior composition
category: concepts
tags: [c, ecs, game, architecture]
sources: [projects/c-blueprints]
summary: >-
  Game entities use reflected flat component columns for shared state, while prototypes and reusable graph assets define how entities are assembled.
provenance:
  extracted: 0.83
  inferred: 0.14
  ambiguous: 0.03
base_confidence: 0.59
lifecycle: draft
lifecycle_changed: 2026-08-27
created: 2026-08-27T05:10:02Z
updated: 2026-08-27T05:23:15Z
---

# c-blueprints ECS prototypes and behavior composition

The game layer is a consumer of the core runtime, not a dependency of it. `ecs/` owns fixed entity slots, generation-safe handles, component columns, occupancy indices, deferred structural commands, and the world hash. `game/` adds prototypes, behavior assets, scene documents, and the attachment policy. (docs/adr/0017-entity-behavior-is-composed-blueprint-instances.md:42-48; docs/game-spec.md:38-107,388-400)

## Component storage

Each component type has a runtime descriptor with an ID, name, size, `type_desc`, layout hash, and occupancy-index choice. Values live in a flat column addressed by entity slot. The default occupancy index is a bitset; a dense entity list is available for rare components that are iterated frequently. Generated accessors serve typed C systems, while generic graph code uses the runtime descriptor. (docs/adr/0018-reflected-flat-column-component-storage.md:33-59; ecs/ecs.h:21-49)

Reflection emits the component registry, layout metadata, typed accessors, and `Get`, `Set`, `Has`, `Add`, and `Remove` nodes from annotated C declarations. Generic serialization and inspection use `type_desc` plus `pin_read_field` and `pin_write_field`; components do not need bespoke serializers. (docs/adr/0018-reflected-flat-column-component-storage.md:50-59; game/generated.h:1-33)

Structural operations are recorded with sort keys and applied at a synchronization point. This keeps iteration valid and gives concurrent producers a deterministic result. (docs/adr/0018-reflected-flat-column-component-storage.md:65-74; docs/game-spec.md:84-103)

## Prototype and behavior ownership

A Prototype is a data document. It lists component defaults and graph asset names, does not occupy an entity slot, and spawns one entity plus one Blueprint instance per attached graph. Including another prototype is allowed when the including document can override included values. (docs/game-spec.md:109-153)

An entity can own several independent Blueprint instances, one per graph asset named by its Prototype. The instances share the entity's components, but not their variables or execution state. Durable state that another graph, a C system, rendering, or serialization must observe belongs in a component. Behavior-local variables are transient and private. (docs/adr/0017-entity-behavior-is-composed-blueprint-instances.md:25-40; docs/game-spec.md:183-219)

This makes behavior composition the unit of authoring: a new enemy can combine reusable behavior assets instead of requiring one monolithic graph. A graph that needs a continuation uses a per-entity instance; uniform arithmetic over many entities may remain a C system or batch graph. (docs/adr/0017-entity-behavior-is-composed-blueprint-instances.md:31-33,50-68)

## Related

- [[projects/c-blueprints/concepts/runtime-evaluation-and-identity]]: defines what each attached instance owns.
- [[projects/c-blueprints/concepts/live-authoring-and-runtime-targets]]: describes propagation into live entities.
- [[projects/c-blueprints/c-blueprints]]: project boundary and Phase 10 scene plan.

## Sources

- `docs/adr/0017-entity-behavior-is-composed-blueprint-instances.md`
- `docs/adr/0018-reflected-flat-column-component-storage.md`
- `docs/game-spec.md`
- `ecs/ecs.h`, `game/prototype.h`, `game/behavior.h`, `game/generated.h`
