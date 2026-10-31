# Authoring navigation

Navigation and refactoring live in shared C modules outside `src/editor/`.
Studio, the SDK and file inspection consume the same resolved identities and
source ranges. Agents edit asset files directly; there is no agent mutation
server or MCP parity requirement.

## Canonical source facts

`language/syntax.h` owns immutable Tree-sitter snapshots, incremental edits and
recovery diagnostics for `.chibi`. `language/semantic.h` resolves declarations,
uses, types, scopes, calls, queries and bindings against an explicit catalog.
Invalid drafts keep the facts that can be resolved.

`authoring/source_catalog.h` discovers project declarations, including external
KDL event and prototype definitions and selected KDL/RML asset references.
Each input map action is defined by its exact node name in `content/input.kdl`.
An action is the event of that name, so `input.<Action>` reads, `on <Action>`
handlers and the map's node resolve to one identity for definitions, references
and rename.
`authoring/source_collection.h` owns a complete project generation: its catalog,
syntax, analysis, preparation diagnostics and finalized execution modules.
Document identity comes from the content-relative asset path, independently of
its displayed name. Prepared runtime modules contain no parser references.

Consumers use these shared APIs:

Static [allocation and copy explanations](../archive/docs/cost-explanations.md) expose finalized
cost evidence through the SDK, with source ranges and explicit uncertainty.
They do not measure runtime traffic. Their command, `chibi check
--explain-costs`, is archived with game projects.

- `authoring/source_navigation.h` indexes executable documents and exact external
  declaration ranges. Definitions, references and callers use resolved IDs;
  authored event aliases retain their own identity while call hierarchy can
  normalize them to their underlying event.
- Highlighting, completion, hover, rename and source edits moved to `archive/`
  (see `archive/README.md`); the compiler and formatter do not need them.
- `authoring/source_prototype_flow.h` finds the string constants that reach a
  prototype parameter, such as Spawn Prototype's, through locals, state, authored
  function parameters and their defaults, in any document. Navigation indexes
  each as a reference to that prototype, so find references and rename cover
  `kind := "Drone"` before `spawn(kind)` and a library's `Prototype: name =
  "Drone"` default. It does not follow component fields or event payloads.
- `authoring/source_references.h` maps these facts into the shared asset reference
  index, including source ranges and owning callable IDs.

`assets/asset_source.h` remains the declarative KDL language service.
`authoring/reference_index.h`, `assets/refactor.h` and `assets/call_hierarchy.h`
cover project asset relationships, rename/deletion planning and callers/callees.
`assets/reference_adapters.h` supplies native graph, scene, component and UI
collectors; the owning reference index has no graph or document dependency. Executable source
facts come from canonical analysis, rather than reconstructed executable graphs.

Completion uses visible lexical declarations and catalog signatures. Entity
receivers expose reflected components and component receivers expose their
fields. Assignment and call argument contexts constrain suggested types.
Catalog types supply system component aliases and qualified event payloads.
Completion recovers unfinished qualified library members and prototype arguments
from resolved declarations and parameter identities, including named and quoted
parameter labels. The [navigation cases](../test/language/navigation.c) cover
replacement ranges and lexical shadowing as well as partial calls. Prototype
component-presence ranking still needs owning metadata; it must not infer meaning
from matching text.

## Presentation and source editing

`authoring/source_presentation.h` builds an optional graph view from resolved
analysis. Every presentation origin carries its semantic identity and exact
source range. Presentation descriptors have no execution callbacks. The source
module remains the execution authority, and presentation graphs never enter the
native behavior compiler.

`authoring/source_format.h` formats complete canonical snapshots while preserving
comments, literal bytes, leaf tokens and CST structure. Incomplete syntax returns
a diagnostic. Editor state snapshots (`authoring_source_presentation_metadata`)
bind to exact source text and are never saved to the project. Malformed or
stale snapshots cannot change executable meaning.

The Text Editor's `asset_code` session retains draft syntax, current resolved
facts and the last valid generation. Invalid edits retain the last valid runtime
and presentation generation. History owns source text and optional editor state;
save checks pending validation and source disk conflicts. Source-backed
presentation edits use exact origin ranges followed by canonical reanalysis.
There is no graph-to-source parser reconciliation authority.

Studio exposes Find References, Go to Definition, Calls, Problems, completion,
hover and rename previews. F12 and Shift+F12 navigate definitions and references;
Vim normal mode provides `gd` and `gr`. Control+O/Control+I traverse navigation
history; standard mode uses Mod+[/Mod+]. Control+Space requests completion.
F2 opens rename and Shift+Alt+H opens Calls; Vim provides `gR` and `gc`.

Graph, Code and Both consume one source session and save/history state. A broken
`.chibi` draft opens with diagnostics and can be repaired while the previously
applied runtime remains active. Paused source locations belong to a retained
execution generation; a changed draft must not display a location against
unrelated text. Model tests establish ownership and exact range behavior, but do
not establish rendered graph gestures, interactive stepping or debugger parity.
Studio's [bounded acceptance inventory](https://github.com/waldnercharles/chibi-studio/blob/main/tests/acceptance/README.md)
covers selected history, keyboard, dirty-prompt and rendered graph/asset pointer
interactions. Debugger-source-history covers invalid drafts and undo/redo against
a retained paused generation. Valid live-generation replacement and broader
graph gestures remain outside those cases. The
[test inventory](test-inventory.md) describes how to report selected and unrun coverage.
