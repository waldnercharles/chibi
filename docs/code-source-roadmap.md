# Behavior code and declarative assets

[CUT-70](https://linear.app/cute-engine/issue/CUT-70/replace-kdl-with-canonical-code-assets-and-first-class-graph-authoring)
and [CUT-71](https://linear.app/cute-engine/issue/CUT-71/build-the-shared-c-code-frontend-and-direct-behavior-graph)
provide the shared native source frontend and direct behavior graph authoring.
The current requirement keeps declarative content in KDL and makes readable
`.chibi` the authority for executable behavior. LuaJIT is not part of this
implementation.

## Persistence contract

Project manifests are `project.kdl`, with project version 18. Scenes, prototypes,
event declarations, models, shapes, materials, fields, shader libraries,
scenarios and editor configuration use KDL 2. Behaviors, behavior libraries and
systems use `.chibi`. RML and RCSS remain separate formats.

Source text owns executable operations, declarations and comments. The shared
frontend retains a concrete syntax tree (CST) and resolves typed semantic records.
Graph views project those records; graph edits produce source edits and prepare
a new candidate before adoption. Optional `.chibi.meta` stores presentation and
debug metadata. Execution remains valid without it. Invalid candidates retain
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

## Historical declarative migration evidence (version 7)

The following records describe the earlier persistence migration and its graph
execution implementation. They preserve the original proof names and do not
certify the current version 18 frontend or prepared player.

The requirements restart was captured at tree
`48a953d7578ba091afde0df7120ef30ec3537fea` in
`build/requirements-restart-baseline`. The declarative conversion reads current
values rather than restoring repository files, preserving edits made since the
original snapshot.

511 declarative documents were converted from the intermediate code storage to
KDL. Their emitted KDL was parsed and compared semantically before activation.
313 nested parameter/mesh-part payloads were converted and compared separately.
All nine manifests were updated to version 7 and KDL start-scene paths. The
105 executable source documents were reserved for the separate readable source
migration below. Original declarative documents contained no lexical comments;
authored graph comment/layout metadata remains in their native model fields.

Proof and exact before/after files are stored under
`build/requirements-declarative-migration`. The earlier all-code migration's
proof under `build/cut70-migration` describes the superseded persistence choice.
Native canonical checks, source parity and final runtime/build gates for the
mixed persistence contract are separate validation work; the conversion proof
does not claim those gates have passed.

## Historical executable migration evidence (version 7)

At that revision, all 105 existing behaviors, libraries and systems contained readable source.
The final1277 comparison preserves the original raw document facts and proves
matching executable models both without metadata and after independently
reopening the optional authoring metadata. Both models compile without errors;
system queries match. The three existing source examples remain readable code.
Activation checked every current file against its expected hash, backed up all
105 files, and verified every written file.

Generated formulas with shared bodies, aggregate captures or precision
boundaries sometimes become ordinary typed functions. Their operations and
evaluation semantics remain in code. Editing those functions owns their bodies;
the former formula-regeneration provenance is discarded. Some unused pin
inference flags, cached displays, historical socket labels and four promoted
parameter lineage records are also discarded or regenerated. Caller signatures,
state initializers, actual numeric widths, executable connections and resource
names remain checked.

66 assets retain every original identity and presentation record; 88 retain all
matched surviving records. Representation changes remove 44 authoring records.
No breakpoint, watch, comment or comment-box value was lost or changed. Four
regenerated helper nodes in ArenaPickup lose their old nonzero positions. A
pre-existing Flipper graph/pin ID collision is repaired by moving graph 1926 to
2666 while retaining execution pin 1926.

Exact per-asset changes, frozen compiler/input identities and activation backups
are under `build/requirements-behavior-migration`, particularly
`final1277-authored-review.json`, `final1277-loss-summary.json` and
`final1277-authored-activation.json`. Full integration results are tracked
separately from this migration proof.

Postcanonical verification compares every current asset with the immutable
activated source using the final loader. All 105 complete native models and
their authoring metadata match; metadata-free execution, queries, compilation
and persist/reopen comparisons also pass. Callable expression owners and full
aggregate field ancestry keep optional identities stable. Three assets affected
by intermediate formatting were restored from the activation snapshots with
hash guards and complete native-model comparisons. These records are in
`postcanonical-final-report.json`, `postcanonical-final-provenance.json` and
`authoring-recovery-review.json` under the same migration evidence directory.
