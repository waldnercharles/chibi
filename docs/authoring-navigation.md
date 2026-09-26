# Authoring navigation

Studio users and MCP agents get the navigation and refactoring a language server gives code: find references, rename, go to definition and the rest below. Each feature is built once, in a module outside `src/editor/`, and ships in both Studio and the MCP. It is done when both expose it.

`searchNodes` set the pattern (#105). The editor and the MCP share `component_node_presets`, `support/text_search`, `GRAPH_HOST_EVENTS`, the System node list `game_system_node_supported`, node descriptions (`graph/authoring/describe.h`) and validation linked with the project's libraries (`graph/document/problems.h`) instead of keeping parallel copies.

The reference index (`src/assets/reference_index.h`) is the shared base for navigation, and `src/assets/refactor.h` for rename and safe delete: given the index and each asset's text, it plans every file an edit changes, in canonical form. Studio builds one from its authored state, unsaved edits included (`src/editor/workspace/asset_index.c`); the MCP catalog builds one from the project's files (`CATALOG_SNAPSHOT.references` in `src/automation/controller.c`). It records every reference below with its asset, graph, node, pin and role, and answers find uses, definitions and use counts.

## Symbols

A reference names a symbol by ID or by name. A rename rewrites every by-name reference in every asset.

| Symbol | Referenced by |
| --- | --- |
| Function, macro, graph | Graph ID; library calls also name their asset (`reference_asset`) |
| Function parameter | Declaration ID on each call's pins; a library call's pin without one, by name |
| Variable | Variable ID on Get/Set nodes and bindings |
| Local | Data edges from the output it names; only the local holds the name |
| Expression input | Name in the expression source |
| Event (host, definition or UI) | Name: `event_name` on Event nodes, `reference_name` on Send and Call Event, and a UI document's `data-event` or `data-keys` |
| Component, field | Stable key and field name, on component nodes and bindings |
| Prototype, material, shape, UI document | Asset name: in pin defaults such as `Spawn Prototype`'s and a `GameShader` value's, in component fields such as `GameUi.document` and `GameSpawnRequest.prototype`, and in `<use ui>` |
| UI event | Name in a UI document's `data-event` and `data-keys`, and in `GameActionBinding.action` |
| Event payload field | A variable of the event's definition: declaration ID and name on each sender's and handler's payload pin |

## Features

Status on 2026-09-26, after rename and safe delete landed. Update a row when its feature lands.

| Feature | Studio | MCP | Missing |
| --- | --- | --- | --- |
| Find references | Per node across the project, for functions, variables, events (host and undeclared too), components and assets; locals and events from Members' use counts, component fields from the component peek; rows label reads, writes, sends and handles | `findReferences`: the definition and every use by role (asset, graph, node, pin, or scene object and component field) of an asset, function, parameter, variable, local, event, component or field, with counts per role, a role filter and pages | Studio: parameters |
| Rename | Variables, locals, functions, parameters and expression inputs in the open asset. What other assets name goes through a popover listing every file it rewrites: library functions and their parameters, event payload fields, events (F2 or Rename Event on an Event, Send Event or Call Event node) and assets (F2 or Rename in Assets); Undo in its toast restores every file | `renameSymbol`: assets, events, functions, macros, parameters, variables and locals across assets, dry run first, hash-checked, validated and all or nothing; `buildGraph` renames within one asset | Both: scenes (the project file names them) and systems. Values the index does not record, such as a prototype name a library call passes on, are listed as mentions, not rewritten. Studio: files with unsaved edits must be saved first; cross-file Undo lasts while its toast shows |
| Go to definition | Alt+G and the node menu: call → graph, with breadcrumbs and Back/Forward; event node → event definition; Get/Set → the variable in Members; Spawn Prototype → prototype; component node → a peek of its fields with their use counts | `findReferences` returns each symbol's `definition`: an asset, graph node, pin or a component's reflected field type | — |
| Call hierarchy | Output ▸ Calls (Shift+Alt+H, the node, Members and graph menus): callers or callees of a function or event as a tree; a row opens its call, send or Event node, and More traces on from a row whose children were left out; call stack while paused | `callHierarchy`: callers and callees to a depth, through the Event handlers whose runs reach a call, ui and action senders, host events, with cycles marked | — |
| Safe delete | Deleting a used function or variable in Members, or a used asset or event definition in Assets (X or Delete), lists its uses in a popover before Delete; Undo in the toast restores a deleted file | `deleteSymbol` refuses while references remain and lists them; `force` deletes and lists what dangles | Both: scenes and systems; parameters and locals |
| Diagnostics | Live compile, node badges, Problems rows jump to the node; Problems ▸ Project lists every graph asset's errors and warnings, checked after saves, reloads and Refresh | Whole project (`getAssetCatalog`, linked like Studio's), `validateAsset` | Studio: systems in Problems ▸ Project |
| Code actions | Optimize, Math to Expressions, Collapse to Function/Macro, Expand Node, Promote to Variable, Name Value; dragging a function's or macro's inputs and outputs to reorder them | `optimizeGraph`, `create_local`/`expand_local`, `collapse_to_function`/`collapse_to_macro`, `expand_node`, `convert_to_expression`, `promote_to_variable`; Entry/Return pin edits, including `set_pin.order`, go through `graph_signature` | Studio: reordering from the keyboard (the kit's drag has no keyboard sensor) |
| Completion | Add menu ranked by the dropped pin (`wire_fit`) | `searchNodes` `wire_from`/`wire_to`, ranked by the same `wire_fit`; UI events from `ui_document_events` | Studio: UI events in the Add menu |
| Hover | Pin values while debugging; pin types in the Node tab; node titles show their description in a tooltip, and add-node results as a second line | `searchNodes` returns every node's and event's `description` | Both: descriptions of functions, variables and pins |
| Outline | Members panel, with the events a behavior handles; each function, macro, variable, named value and event shows its use count (senders and handlers for events), which opens Find | Catalog members; `inspectGraph` with `include_uses` counts uses by role for the asset, its functions and macros, and a behavior's variables | — |
| Resolved types | Node tab | `inspectGraph` lists each node's pins with their resolved types | — |
| Find | ⌘F within one asset | `searchAssets` matches member names | Both: find nodes across the project |

## Order

The optimize branch (#106) supplied function signatures and code actions, and step 1 the shared index the rest build on.

1. **Shared index.** Done: `src/assets/reference_index.{c,h}` replaced the core of Studio's asset index and the controller's `catalog_scan_references`, adding event, `Spawn Prototype` and UI links to the MCP.
2. **Read-only navigation.** Done: `findReferences` (with definitions) and `callHierarchy` in the MCP, over the index's pin names, definition names and the nodes each Event node's run reaches (`reference_index_reaches`), and `src/assets/call_hierarchy.{c,h}`; Studio's Go to Definition, Output ▸ Calls and Members use counts.
3. **Multi-asset edits.** Done: `src/assets/refactor.{c,h}` plans rename and safe delete from the index; the MCP publishes a plan as one hash-checked batch (`automation_assets_publish`) and Studio through `editor/workspace/asset_refactor.h`, then follows the moved files in memory.
4. **The rest.** Code-action parity, hover descriptions, completion from a pin, resolved types, project-wide find.
