# Authoring navigation

Studio users and MCP agents get the navigation and refactoring a language server gives code: find references, rename, go to definition and the rest below. Each feature is built once, in a module outside `src/editor/`, and ships in both Studio and the MCP. It is done when both expose it.

`searchNodes` set the pattern (#105). The editor and the MCP share `component_node_presets`, `support/text_search` and `GRAPH_HOST_EVENTS` instead of keeping parallel copies.

## Symbols

A reference names a symbol by ID or by name. A rename rewrites every by-name reference in every asset.

| Symbol | Referenced by |
| --- | --- |
| Function, macro, graph | Graph ID; library calls also name their asset (`reference_asset`) |
| Function parameter | Declaration ID on each call's pins |
| Variable | Variable ID on Get/Set nodes and bindings |
| Local | Data edges from the output it names; only the local holds the name |
| Expression input | Name in the expression source |
| Event (host or definition) | Name: `event_name` on Event nodes, `reference_name` on Send and Call Event |
| Component, field | Stable key and field name, on component nodes and bindings |
| Prototype, material, shape, UI document | Asset name: in pin defaults such as `Spawn Prototype`'s, in component fields, and (on `cwaldner/game-ui`) in `GameUi.document` and `<use ui>` |

## Features

Status on 2026-09-25, after the optimize branch merged (#106). Update a row when its feature lands.

| Feature | Studio | MCP | Missing |
| --- | --- | --- | --- |
| Find references | Per node across the project, for functions, variables and assets | Per asset only (`inspectAssetDependencies`), without event send/handle or `Spawn Prototype` links | Both: component fields, locals, parameters; reads vs writes, senders vs handlers. MCP: any per-node results |
| Rename | Variables, locals, function parameters (calls in the same asset), expression inputs | Graphs, variables, locals, expression inputs | Both: events, assets and prototypes across assets; library functions and parameters across callers |
| Go to definition | Call → graph, with breadcrumbs and Back/Forward | `inspectGraph` resolves calls | Both: event → event asset, Get/Set → variable, Spawn → prototype, component node → component |
| Call hierarchy | Callers through Find References; call stack while paused | Outgoing calls per graph | Both: a caller/callee tree, event send → handle chains |
| Safe delete | — | — | Both: list references before deleting a variable, function, event or asset |
| Diagnostics | Live compile, node badges, Problems rows jump to the node | Whole project (`getAssetCatalog`), `validateAsset` | Studio: project-wide Problems |
| Code actions | Optimize, Math to Expressions, Collapse to Function/Macro, Expand Node, Promote to Variable, Name Value | `optimizeGraph`, `create_local` | MCP: collapse, expand, promote; signature edits through `graph_signature` so calls follow |
| Completion | Add menu ranked by the dropped pin (`wire_fit`) | `searchNodes` `wire_from`/`wire_to`, ranked by the same `wire_fit` | — |
| Hover | Pin values while debugging; pin types in the Node tab | Pins in `searchNodes` | Both: node descriptions (`node_desc` has none), in `searchNodes` results and as Studio tooltips, which also need to wrap (`.tooltip` is `white-space: nowrap`) |
| Outline | Members panel | Catalog members, `inspectGraph` | Both: use, sender and handler counts per member |
| Resolved types | Node tab | `inspectGraph` omits types that wildcard pins resolve to | MCP |
| Find | ⌘F within one asset | `searchAssets` matches member names | Both: find nodes across the project |

## Order

The optimize branch (#106) supplied the asset index, function signatures and code actions these features build on, so work starts with step 1.

1. **Shared index.** Move the asset index core (`asset_index_add_graph_references`, `asset_index_find_uses`) from `src/editor/workspace/asset_index.c` into the graph layer. Replace the controller's `catalog_scan_references` with it, which adds the missing event and `Spawn Prototype` links.
2. **Read-only navigation.** Find references, go to definition and call hierarchy on that index: an MCP tool each, plus Studio views.
3. **Multi-asset edits.** Rename and safe delete, as one atomic, hash-checked change with a dry run that lists every affected asset.
4. **The rest.** Code-action parity, hover descriptions, completion from a pin, resolved types, project-wide find.
