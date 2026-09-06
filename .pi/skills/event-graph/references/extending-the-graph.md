# Extending the Graph

Read this when existing nodes cannot express the requested behavior.

## Seams

- Generic graph semantics: `core/pin.h`, `core/node.h`, `core/graph.[ch]`, `core/compile.[ch]`, and `core/flow.[ch]`.
- Built-in nodes and registry packs: `core/*_nodes.[ch]`, `core/registry.h`, and the CMake pack list.
- Reflected Components, calls, and conversions: `game/components.h`, `game/interface.h`, and `game/game_nodes.c`.
- Runtime attachment and dispatch: `core/instance.[ch]` and `game/behavior.[ch]`.
- Prototype composition: `game/prototype.[ch]`.
- Authoring and context filters: `editor/editor.[ch]`; host events live in the host's `editor_event_signature` catalog and dispatch sites.

Reflection writes `build/generated/game_registry.c`; edit its annotated inputs, not the generated file. Preserve Component identities in `game/component_ids.tsv`.

## Contract

A reader is pure; a mutation or side effect has execution flow. Declare exact pin types and `node_access` effects. Give the node a display name, category, keywords, registry pack, and every intended editor-context allowlist.

Verify the smallest authored graph that composes the primitive, including behavior, serialization when its shape is dynamic, and spawn-menu discovery when the authoring surface changes.
