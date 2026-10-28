# Behavior code and declarative assets

CUT-70
and CUT-71
provide the shared native source frontend and direct behavior graph authoring.
The current requirement keeps declarative content in KDL and makes readable
`.chibi` the authority for executable behavior. LuaJIT is not part of this
implementation.

## Persistence contract

Project manifests are `project.kdl`, with project version 24 (the current
`CHIBI_PROJECT_VERSION` in `src/assets/project.h`). Scenes, prototypes,
event declarations, models, shapes, materials, fields, shader libraries,
scenarios and editor configuration use KDL 2. Behaviors, behavior libraries and
systems use `.chibi`. RML and RCSS remain separate formats.

Source text owns executable operations, declarations and comments. The shared
frontend retains a concrete syntax tree (CST) and resolves typed semantic records.
Graph views project those records; graph edits produce source edits and prepare
a new candidate before adoption. Graphs lay out automatically, and descriptions
are source comments. Breakpoints and watches are editor state outside the
project. Invalid candidates retain
diagnostics and cannot replace an applied executable generation.

The [language reference](code-language.md) records supported source forms.
[Authoring navigation](authoring-navigation.md) records shared language services
and editor/controller integration. The compiler lowers resolved source directly
into verified execution modules. Graphs do not mediate behavior execution.

## Native boundaries

`src/language` owns Tree-sitter syntax snapshots, semantic analysis and direct
lowering. Its syntax and language SDK targets are separate from the compiler
target that owns `lower.c`. `src/capabilities` owns shared native signatures,
stable IDs, reflected types and effects. Applications supply explicit native
tables rather than discovering execution contracts through graph descriptors.

`src/execution` owns verified modules, backends, live programs, state,
suspension and inspection. Immutable modules and their imported modules have
independent lifetimes from syntax snapshots and editor views. Portable prepared
artifacts resolve native identities and schemas against the player's supplied
linker; they do not persist native callback pointers.

`src/authoring` owns source preparation and collection generations, formatting,
navigation, refactoring, source edits and graph presentation. A collection
retains its declaration catalog, syntax, analysis and finalized modules. Working
drafts and applied generations remain separate; publication stages a complete
valid candidate before replacement. Studio and the controller consume these
shared services through public SDK interfaces.

`src/support/asset_text.*` implements declarative KDL values. Native shader,
model and field graphs have their own preparation paths under `src/graph`.
Those paths reject executable behavior domains. The offline compiler prepares
source modules and declarative assets; the standalone player consumes prepared
modules, scenes and declared resources without linking syntax or authoring.
The owning SDK targets and scoped headers are declared under `sdk`.
