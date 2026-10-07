# Behavior code and declarative assets

[CUT-70](https://linear.app/cute-engine/issue/CUT-70/replace-kdl-with-canonical-code-assets-and-first-class-graph-authoring)
and [CUT-71](https://linear.app/cute-engine/issue/CUT-71/build-the-shared-c-code-frontend-and-direct-behavior-graph)
provide the shared native source frontend and direct behavior graph authoring.
The current requirement keeps declarative content in KDL and makes readable
`.code` the authority for executable behavior. LuaJIT is not part of this
implementation.

## Persistence contract

Project manifests are `project.kdl`, with project version 7. Scenes, prototypes,
event declarations, models, shapes, materials, fields, shader libraries,
scenarios and editor configuration use KDL 2. Behaviors, behavior libraries and
systems use `.code`. RML and RCSS remain separate formats.

Behavior code describes executable operations through readable syntax. Graph
editing and text editing consume the same native model. Stable identities,
layout and comments belong to the source document; a generated executable graph
or identity sidecar is not an independent authoring authority. Unsupported
source or graph edits must produce a diagnostic before replacement.

The [language reference](code-language.md) records supported source forms.
[Authoring navigation](authoring-navigation.md) records shared language services
and editor/controller integration. The graph runtime and its native compiler
remain the execution path.

## Native boundaries

`src/support/code_lex.*` provides shared tokens and source locations.
`src/support/asset_text.*` implements KDL declarative values.
`src/assets/code_value.*` parses executable source literals.
`src/assets/code_source.*`, `code_program.*` and `code_statements.c` build
behavior models through engine reflection and graph APIs; `code_print.*`
prints readable programs from those models. Existing graph
validation and compilation consume candidate models. This frontend uses no
Node subprocess or MCP graph-edit replay. Examples live under
`examples/behaviors`.

Source objects own text, byte ranges, diagnostics and candidate models. Compile
and build APIs return independently owned models. Callers retain the previous
valid model when parsing or compilation fails. Studio and the controller use
these shared APIs rather than separate language implementations.

## Declarative migration evidence

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

## Executable migration evidence

All 105 existing behaviors, libraries and systems now contain readable source.
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
