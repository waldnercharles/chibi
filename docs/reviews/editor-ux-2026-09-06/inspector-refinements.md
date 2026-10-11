# Inspector refinements

Follow-up to the September 6 usability review. These are design refinements and proposed engine work, not implemented native-editor features.

The subsequent [Apple control review](apple-inspector-review.md) removes Saved badges and the Inspector status/Undo footer, unifies the search field boundary, adds an identity icon, and specifies shared ImGui controls.

## Component and behavior selection

Use the same searchable checkbox dropdown for both sections. The section-header button can say “Manage,” because the picker adds and removes attachments. Both attached and available items appear in a stable order, with checked boxes for current attachments. Search filters visibility without changing selections. Keep the popup open across toggles, preserve keyboard focus, cap the result list height, and include a selected count. Done, Escape, and clicking outside dismiss the popup; changes apply as each box is toggled, so Escape does not undo them.

Each toggle is one Undo operation. Do not reorder results when a checkbox changes. The native implementation should preserve component order and disclosure choices when reattaching. Search gains focus on open and contains its own clear action.

The revised concept includes both pickers and retains edited component values when deselected and reselected. This retention is local to the concept session. Production retention must survive saving, closing the editor, and reopening the project.

Position fields use `X | value   Y | value`. Each letter sits outside the colored rule. Apply the rule to the value cell, not its label/container. The current transform is two-dimensional; add Z only for a three-dimensional field.

## What deselection means

Deselecting an attachment removes it from the current object's active composition while retaining its authored settings. A detached component must be absent from runtime ECS presence checks and queries. A detached behavior must receive no events and run no activations. Merely hiding its Inspector section or adding a cosmetic “disabled” flag would not satisfy this behavior.

Keep an authored attachment record with stable identity, presence, stored values or overrides, and ordering. Runtime entities contain only the active attachments. Rechecking uses the retained configuration before considering defaults. Component header removal must use the same operation as checkbox deselection.

Retain data at the correct owner:

- Prototype component defaults belong to that prototype.
- Instance overrides belong to that scene instance. Preserve their inheritance relationship so unchanged fields can continue to inherit prototype defaults; keep a recovery snapshot where needed to avoid losing data if the prototype/schema changes.
- Shared behavior graph edits remain in the behavior asset. Unchecking an attachment must not modify that graph or its other users.
- Per-attachment authored variable overrides, when supported, belong to the attachment and must also survive deselection. Do not confuse these with runtime variables such as an elapsed timer or a suspended execution. Reattachment starts a fresh runtime instance from authored settings; resuming live execution would be a separate feature.

This requires persistence changes. [prototype_remove_component](/Users/cwaldner/Personal/c-blueprints/game/prototype.c:1268) currently frees the component data. Adding it again allocates zeroed values. [prototype_remove_graph](/Users/cwaldner/Personal/c-blueprints/game/prototype.c:1308) removes an asset-name reference, while scene attachment history records before/after lists. Undo can preserve an earlier edit, but it is not a durable store for later reattachment after an editor restart. The current prototype representation has active components and graph references, with no retained-attachment section.

“Soft delete” is a useful description of retaining a removed attachment. Global asset archival should remain a separate Navigator action, because a shared behavior or component definition can be used by many prototypes. If asset archival is added later, retain its identity and contents, mark references explicitly, and provide Restore. Never archive a shared asset merely because one object unchecks it. Permanent removal of retained settings should be an explicit operation separate from normal deselection.

Keep authoring composition changes unavailable during Play initially, matching the current component UI. Do not silently stop the scene when a checkbox is toggled. If removing Health makes an attached behavior invalid, retain the requested configuration and report the missing dependency; block running that invalid composition until it is resolved. Do not silently remove dependent behaviors or reattach components behind the user's back.

Acceptance examples:

1. Set Player Health.Current to 73, uncheck Health, and recheck it. The value remains 73 and its position in the Inspector remains stable.
2. Uncheck Health, save the prototype, close the editor, reopen it, and recheck Health. The stored value is still 73.
3. Customize a behavior graph, uncheck its attachment on Player, and recheck it. The same graph asset is attached; its edits and other users are unchanged.
4. Remove an instance component with local overrides, change unrelated prototype defaults, and restore it. Explicit local overrides survive; fields still marked inherited follow their base.
5. Filter either picker while some offscreen items are selected. Clearing search restores the full list with identical selection state.
6. Undo or Redo one checkbox change. Both attachment presence and retained settings restore together.

## Creating new components in the editor

Yes, this is feasible. It is an engine authoring feature beyond changing the Inspector picker.

Today, normal gameplay components are C types in [components.h](/Users/cwaldner/Personal/c-blueprints/game/components.h). The build runs the reflection generator and compiles its component and graph-node pack. [CMakeLists.txt:258](/Users/cwaldner/Personal/c-blueprints/CMakeLists.txt:258) shows that path. The picker chooses existing registered types; it cannot currently define a new one.

There is useful infrastructure already. [ecs_world_register_component](/Users/cwaldner/Personal/c-blueprints/ecs/ecs.c:438) accepts a runtime descriptor and allocates its storage. [type_desc](/Users/cwaldner/Personal/c-blueprints/core/type.h:9) describes fields, sizes, and offsets. The ECS also has a component migration operation. However, the graph [registry](/Users/cwaldner/Personal/c-blueprints/core/registry.h:18) enumerates an explicit compiled pack list, and migration currently matches fields by name and zeroes incompatible/new fields. These pieces do not yet provide a complete editor-defined schema system.

Two implementation routes are possible:

| Route | User experience | Implications |
| --- | --- | --- |
| Generate C from a component editor | Create fields in the editor, rebuild, then reload/restart to use the component. | Reuses reflection and compiled native access. Requires a compiler in the authoring environment and a managed generated-code path; no arbitrary live schema edits. |
| Store a component schema as an asset | Create a component, define fields/defaults, then attach and use it in graphs without rebuilding C. | Needs dynamic schema/graph registration, stable identities, persistence, migration, and build/export support. Native systems still use their compiled component contracts. |

I recommend schema assets for user-authored gameplay data, while keeping engine components such as Transform and renderer inputs native. This would make an editor-created `Poison` component with `DamagePerSecond`, `Duration`, and `Stacks` practical. A behavior or system graph supplies the poison logic; adding a component defines data, not a new engine capability by itself.

Start the schema editor with booleans, integers, numbers, and known vector types. Add references, strings, arrays, and nested user types only with defined ownership, serialization, and migration behavior. Give component and field identities persistence independent of their display names. Renaming “Duration” must not lose values or disconnect graph pins.

The smallest complete schema feature needs:

1. A Component Definition asset with name, stable IDs, fields, types, defaults, and schema version.
2. Validation of names/types and calculated layout, with a registry that can own the schema for the required lifetime.
3. Inspector generation and generic typed Get/Set-field graph operations, plus query inclusion. Integrate dependency and compile diagnostics.
4. Save/load and project export of schemas and all active/retained component values. Load schemas before dependent prototypes and graphs.
5. Schema migration for active objects and retained attachments. Preserve matching field IDs; give added fields authored defaults. Explicitly handle removed fields and incompatible type changes without quietly destroying stored customizations.
6. Invalidation/recompilation of affected graph/JIT data and descriptor users at a safe boundary. Begin with schema changes allowed only outside Play.

Runtime registration alone is insufficient: native C code still expects its compiled memory layouts, and changing those behind it is unsafe. User-defined schemas should use generic data access. Editing a custom component in the editor should not mutate the layout of a built-in native type.

Implement retained attachment settings before adding schema authoring. That persistence model will also protect values when a custom component definition evolves. The searchable pickers and revised field layout can be delivered independently of the schema editor.
