# Authoring navigation

Studio users and MCP agents get the navigation and refactoring a language server gives code: find references, rename, go to definition and the rest below. Each feature is built once, in a module outside `src/editor/`, and ships in both Studio and the MCP. It is done when both expose it.

`searchNodes` set the pattern (#105). The editor and the MCP share `component_node_presets`, `support/text_search`, `GRAPH_HOST_EVENTS`, the System node list `game_system_node_supported`, node descriptions (`graph/authoring/describe.h`) and validation linked with the project's libraries (`graph/document/problems.h`) instead of keeping parallel copies.

The reference index (`src/assets/reference_index.h`) is the shared base for navigation, and `src/assets/refactor.h` for rename and safe delete: given the index and each asset's text, it plans every file an edit changes, in canonical form. Studio builds one from its authored state, unsaved edits included (`src/editor/workspace/asset_index.c`); the MCP catalog builds one from the project's files (`CATALOG_SNAPSHOT.references` in `src/automation/controller.c`). It records every reference below with its asset, graph, node, pin and role, and answers find uses, definitions and use counts.

Behavior compiler diagnostics retain the original library asset, graph, node and
pin identities. Studio and MCP share the source mapper: an imported failure
highlights its importing call and names the library and graph in its message.

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
| Prototype, material, shape, UI document | Asset name: in pin defaults such as `Spawn Prototype`'s and a `GameShader` value's, in component fields such as `GameUi.document` and `GameSpawnRequest.prototype`, in `<use ui>`, and in a scene's `ui:` or, for scenes that name none, `project.kdl`'s |
| UI class, custom property, save key | Name, in ui assets and stylesheets (`assets/ui_source.h`): a class in `class`, `data-class-` and selectors; a `--token` in `var()` and declarations; a key in `save.<key>` bindings |
| UI event | Name in a UI document's `data-event` and `data-keys`, and in `GameActionBinding.action` |
| Event payload field | A variable of the event's definition: declaration ID and name on each sender's and handler's payload pin |

## Features

Current implementation on 2026-10-06, including KDL declarative editing and readable `.code` behavior source. These entries describe code paths; final integration gates are tracked separately.

| Feature | Studio | MCP | Missing |
| --- | --- | --- | --- |
| Find references | Per node across the project, for functions, variables, events (host and undeclared too), components and assets; locals and events from Members' use counts, component fields from the component peek; rows label reads, writes, sends and handles; in the Text Editor (Shift+F12), a ui class, custom property, save key or `<use ui>` widget, each place in its part (`ui_code_places`) | `findReferences`: the definition and every use by role (asset, graph, node, pin, or scene object and component field) of an asset, function, parameter, variable, local, event, component or field, with counts per role, a role filter and pages; scenes' pages; and a ui class, custom property or save key by file, line and column (`ui_source_references`) | Studio: parameters |
| Rename | Variables, locals, functions, parameters and expression inputs in the open asset. What other assets name goes through a popover listing every file it rewrites: library functions and their parameters, event payload fields, events (F2 or Rename Event on an Event, Send Event or Call Event node) and assets (F2 or Rename in Assets); Undo in its toast restores every file. Scene objects rename with F2, the Outliner or Properties as one Undo step; names used by Studio chrome are fixed C lookups | `renameSymbol`: assets, events, functions, macros, parameters, variables and locals across assets, dry run first, hash-checked, validated and all or nothing; `buildGraph` renames within one asset; `buildScene` supports `rename_entity` as a hash-checked operation, and later operations in that batch address the object by its new name. Renames in a project named Studio return a `warnings` array | Both: scene assets themselves (the project file names them) and systems. Values the index does not record, such as a prototype name a library call passes on, are listed as mentions, not rewritten. Studio: files with unsaved edits must be saved first; cross-file Undo lasts while its toast shows |
| Go to definition | Alt+G and the node menu: call → graph, with breadcrumbs and Back/Forward; event node → event definition; Get/Set → the variable in Members; Spawn Prototype → prototype; component node → a peek of its fields with their use counts; a UI document → its source in the Text Editor (`studio_open_ui_source` opens a line); in the Text Editor, F12 or ⌘-click on a `<use ui>` widget, a `var(--token)` or a class (`ui_code_definition`), whose jumps Back and Forward retrace (Mod+[ and Mod+], mouse buttons 4 and 5, the Text Editor's header) in the same history as the graphs' (`studio_nav_jump`) | `findReferences` returns each symbol's `definition`: an asset, graph node, pin or a component's reflected field type; for a ui class or custom property, the selectors and declarations that define it | — |
| Call hierarchy | Output ▸ Calls (Shift+Alt+H, the node, Members and graph menus): callers or callees of a function or event as a tree; a row opens its call, send or Event node, and More traces on from a row whose children were left out; call stack while paused | `callHierarchy`: callers and callees to a depth, through the Event handlers whose runs reach a call, ui and action senders, host events, with cycles marked | — |
| Safe delete | Deleting a used function or variable in Members, or a used asset or event definition in Assets (X or Delete), lists its uses in a popover before Delete; Undo in the toast restores a deleted file | `deleteSymbol` refuses while references remain and lists them; `force` deletes and lists what dangles | Both: scenes and systems; parameters and locals |
| Diagnostics | Live compile, node badges, Problems rows jump to the node; Problems ▸ Project lists every graph asset's errors and warnings, checked after saves, reloads and Refresh; in the Text Editor, a ui part's syntax errors as typed and RmlUi's warnings once the text rests, a stylesheet's too (`ui_check_stylesheet`), as gutter markers underlining the range they are about, with a count that steps through them (`ui_code_markers`); Problems lists the shown UI document's, and Problems ▸ Project every ui asset's and stylesheet's, rows opening the place (`studio_code_problems`); behavior source syntax/type diagnostics from `assets/code_source.h` and declarative KDL diagnostics from `assets/asset_source.h` appear in the Text Editor; invalid edits keep the last valid preview | Whole project (`getAssetCatalog`, linked like Studio's), `validateAsset`; for a ui asset, tree-sitter's syntax errors and every RmlUi warning, with lines (`ui_source`, `render/ui_check.h`); `readAsset` with `complete: {line, column}` returns behavior source diagnostics from the shared native frontend and declarative KDL diagnostics from `asset_source`, including incomplete source | Studio: systems in Problems ▸ Project. MCP: stylesheets, which are no asset kind |
| Code actions | Optimize, Math to Expressions, Collapse to Function/Macro, Expand Node, Promote to Variable, Name Value; dragging a function's or macro's inputs and outputs to reorder them; the UI designer's moves (by pointer, or from the keyboard: Space, the arrows, Space, over `ui_design_slots`), adds, duplicates, wraps, deletes and attribute, class, style, text and rule value edits, a rule's added and removed declarations and Extract to Class (`editor/documents/ui_design.h`, over the same `ui_source` operations) | `optimizeGraph`, `create_local`/`expand_local`, `collapse_to_function`/`collapse_to_macro`, `expand_node`, `convert_to_expression`, `promote_to_variable`; Entry/Return pin edits, including `set_pin.order`, go through `graph_signature` | UI documents take no MCP code actions: agents edit their RML and RCSS as text |
| Completion | Add menu ranked by the dropped pin (`wire_fit`); UI events of the GameUi documents on what attaches the open behavior (`reference_index_attached_documents`), described as the player's action; component fields that name assets pick from assets of their kind (`reference_named_field_kind`); in the Text Editor, as you type and on Control+Space, ui tags, attributes and bindings, widgets, classes, RCSS properties, custom properties and save keys (`ui_code_complete` over `ui_source_complete`); behavior source offers parser-resolved scoped values, callable signatures and typed members (`code_intelligence_complete`); declarative KDL offers reflected component names, fields, enum values and project asset names (`asset_source_complete`), as you type or with Control+Space | `searchNodes` `wire_from`/`wire_to`, ranked by the same `wire_fit`; UI events from `ui_document_events`, described by the same `ui_events_add`, and with `behavior` only those of the documents on what attaches it (`reference_index_attached_documents`); `inspectUi`'s `complete`: the ui items the Text Editor offers at a line and column (`ui_source_complete`); `readAsset` with `complete: {line, column}` returns the same behavior items from `code_intelligence_complete` or declarative KDL items from `asset_source_complete`, with replacement byte ranges | — |
| Hover | Pin values while debugging; pin types in the Node tab; node titles show their description in a tooltip, and add-node results as a second line; in the Text Editor, a card for the ui widget, class, custom property, save key, property or binding under the pointer, with the diagnostics of its line (`ui_source_hover`); behavior source shows resolved type/callable signatures and descriptions (`code_intelligence_hover`); declarative KDL shows reflected fields and asset-field labels (`asset_source_hover`) | `searchNodes` returns every node's and event's `description`; `findReferences` returns a ui symbol's `description` (`ui_source_describe`); `readAsset` with `complete: {line, column}` returns the same behavior hover from `code_intelligence_hover` or declarative KDL hover from `asset_source_hover` | Both: descriptions of functions, variables and pins |
| Outline | Members panel, with the events a behavior handles; each function, macro, variable, named value and event shows its use count (senders and handlers for events), which opens Find; a UI document's Hierarchy (its `ui_source` elements) and, in Details, its rules | Catalog members; `inspectGraph` with `include_uses` counts uses by role for the asset, its functions and macros, and a behavior's variables; `inspectUi` outlines a ui asset's elements and rules with its props, events, slots, widgets and tokens | — |
| Resolved types | Node tab | `inspectGraph` lists each node's pins with their resolved types | — |
| Find | ⌘F within one graph asset, and in the Text Editor within a UI document's part | `searchAssets` matches member names | Both: find nodes across the project |
| Inspect UI | Inspect Element (⇧⌘C) picks any element Studio draws: its chrome and panels, a graph node, or a game's UI in the Scene. The Inspector lists the element tree, styles by rule with their files and lines (`ui_style_origin`), computed values and attributes; Edit Source opens the element's ui asset at its `data-src` line | `captureRuntimeSnapshot`'s `ui`: what behaviors set on a running scene's documents, by entity and selector | MCP: the element tree and styles of a running scene's UI |

## Source editing and graph navigation

The shared source APIs are `assets/asset_source.h` for declarative KDL
highlighting, reflected completion, hover, and comment-preserving edits,
`assets/code_source.h` for behavior parsing, typed graph construction,
compilation diagnostics, source locations, and graph reconciliation, and
`assets/code_source_intelligence.h` for behavior semantic intelligence.
The frontend publishes resolved declarations, uses, types, lexical scopes,
call targets, and completion contexts. Shared consumers answer definitions,
references, signature hover, typed completion, rename, and call hierarchy from
those facts. They do not infer behavior semantics with another lexical parser.
Semantic highlighting augments lexical comments, strings, and punctuation.

ECS entity receivers offer registered component types, then reflected fields on a
component receiver. Known owning prototypes rank components present on all
owners above components present on some owners and optional components. That
ranking does not guarantee presence on a live entity. Entity-handle fields can
continue the typed chain. System header aliases carry their reflected component
types; optional members require a guarded read in executable source.
Root `bind` destinations and values, and qualified component-event receivers,
use those same ECS member facts and destination-type completion contexts. The
runtime relationship is executable source, separate from optional sidecar layout
and debug metadata. Graph views expose the semantic Set for each binding;
compiler-generated maintenance guards are hidden, and source/debug navigation to
a guard selects that Set. Shared reads remain available to authored consumers.

Studio and the controller use these shared APIs. `analyzeCode` returns
severity, rule, graph, node and pin identities alongside source byte ranges and
line/column positions, as well as candidate graph data and canonical source.
An `offset` requests completion, hover, definition/reference spans and direct
caller/callee ranges. `rename` with an offset proposes new source after scope
capture checks and frontend validation, without publishing. Native project
navigation continues to use the shared reference index for cross-asset symbols.
Imported semantic targets retain their library name alongside native identities;
metadata restoration remaps only identities owned by the current source.

In the Text Editor, F12 opens a draft symbol's declaration; Shift+F12 lists its
source references in Output. Vim normal mode provides `gd` and `gr`;
Control+O and Control+I traverse the shared Back/Forward history. Standard
mode uses Mod+[ and Mod+], and both modes retain their editor undo/redo bindings.
Control+Space requests completions. F2 opens the shared source rename preview;
Shift+Alt+H opens source callers/callees in Calls. Vim normal mode provides
`gR` for rename and `gc` for Calls. Local source renames apply as one document history step after draft validation
and a stale-source check. Exported library function and parameter renames use
the shared project preview and atomic publication, including every caller.
Affected unsaved files must be saved first; source changes after opening the
preview require reopening Rename.

Behavior source and graph views share candidate models through
`asset_code_sync_graph`. Invalid source retains the last valid graph. Graph edits
reconcile into source through the native frontend; unsupported structured
rewrites report a diagnostic before replacement. F9 toggles a breakpoint at the
source statement's executable node. Paused runtime nodes map back to source
ranges for the execution marker. During an invalid draft, an unchanged prefix or
suffix can retain the paused generation's exact marker; changing the executed
statement hides its marker until a valid matching generation is available.
F5 continues, F10 steps over, F11 steps into and
Shift+F11 steps out through Studio's existing graph debugger commands. These
commands do not introduce a separate source interpreter.

Graph, Code and Both use one document history and save state. The text view
shows executable source; optional identity, layout and debug metadata stays in
the saved document and does not fill the editor. A malformed saved `.code` file
opens in Code with source diagnostics even when its graph cannot load. Repair
and Save materialize its graph, after which Graph and Both become available.
The existing scene runtime stays active throughout repair. Live mode publishes
valid compatible edits when safe; On Save holds source changes until Save, and
paused or incompatible runtime generations report that they are waiting.
System root state persists per matched entity; `shared` state belongs to the system.

## Order

The optimize branch (#106) supplied function signatures and code actions, and step 1 the shared index the rest build on.

1. **Shared index.** Done: `src/assets/reference_index.{c,h}` replaced the core of Studio's asset index and the controller's `catalog_scan_references`, adding event, `Spawn Prototype` and UI links to the MCP.
2. **Read-only navigation.** Done: `findReferences` (with definitions) and `callHierarchy` in the MCP, over the index's pin names, definition names and the nodes each Event node's run reaches (`reference_index_reaches`), and `src/assets/call_hierarchy.{c,h}`; Studio's Go to Definition, Output ▸ Calls and Members use counts.
3. **Multi-asset edits.** Done: `src/assets/refactor.{c,h}` plans rename and safe delete from the index; the MCP publishes a plan as one hash-checked batch (`automation_assets_publish`) and Studio through `editor/workspace/asset_refactor.h`, then follows the moved files in memory.
4. **The rest.** Code-action parity, hover descriptions, completion from a pin, resolved types, project-wide find.
