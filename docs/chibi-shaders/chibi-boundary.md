# Chibi-owned shader authoring and inspection

Design checkpoint, 2026-10-22. This placement fits the already authorized
primary-authoring-language goal; implementation requires no fresh human
permission gate. No runtime policy has been rewritten and PR 663 remains draft
for incomplete boundary, integration and proof work. This document does not
establish source compilation, rendering or Studio acceptance. The existing task plan is
[PLAN.md](PLAN.md); the repository-root plan belongs to compiler/toolchain work.

## Ownership

Ordinary Chibi source and the ordinary compiler's resolved facts define source
meaning. A Chibi shader-domain pass checks the supported subset and emits the
existing `shader_graph` representation. That same program continues to feed the
native CPU evaluator and GLSL emitter. There is one shader operation contract.

| Responsibility | Proposed owner |
| --- | --- |
| Names, imports, types, literals, argument/default binding, ordinary effects | Existing authored compiler, exported through its proposed private typed-facts API |
| Shader type/operation eligibility, reachable helper closure, recursion and effect rejection, field flattening, uniform slots and surface outputs | Chibi shader-domain pass |
| Contextual operation IDs, document/range mapping, helper expansion chains and principal source-result bindings | Chibi lowering/source-map modules |
| Captured source closure, current-document comparison, stale-result policy, source lookup and mapped diagnostics | Chibi inspection module |
| IR transport bounds, opcode/index/width/resource validation and owned native program storage | Narrow native adapter using `shader_graph_validate` |
| CPU operation execution, derivative workspace and resident image/scene callbacks | Existing `program.c` evaluator and runtime resources |
| GLSL generation, render-catalog publication and GPU/resource lifetime | Existing native renderer |
| Editor interaction and display | Studio consuming the shared authored inspection service through the SDK |

C remains responsible for safe native buffer sizes and borrowed-pointer bounds.
It need not understand documents, call chains, source revisions or editor
selection. Native allocation/transport errors remain structured transport errors;
the Chibi layer maps an evaluator's operation ID to authored source.

The C sampled-SDF diagnostic repair remains an evaluator fix. C/pico_unit tests
remain the test harness and invoke authored modules through the normal prepared
runtime. Moving policy does not require moving assertions into another language.

## Program and inspection lifecycle

1. Snapshot the ordered source/import closure, resolve it with the ordinary
   compiler, then lower its accepted typed facts in Chibi. Preserve exact typed
   constant bits and resolved declaration/field identities. Do not parse literal
   text again, use private compiler work records, or reconstruct bytecode.
2. Produce one immutable authored bundle: owned documents, origins, result
   bindings, uniform schema/defaults and the existing shader IR. Compilation
   identity includes the entry, compiler/catalog contract and lowering version,
   as well as every source document. Equal text alone cannot pair arbitrary IR
   with a source map.
3. A native bridge bounds and validates the transported IR, copies it into owned
   native storage and returns a program-generation token. Bind that token to the
   authored bundle. A token identifies a live immutable program, not an AST
   occurrence or a hash of source text. Never reuse a retired token.
4. Prepare the catalog/render products from that same program and publish the
   generation transactionally. Failed compilation or publication leaves the
   last valid render generation installed; its inspection becomes stale against
   changed source. A successful edit replaces source-map, CPU and GPU products
   together. Discard a late compile result after a newer request.
5. For inspection, pass the program token, explicit sample and uniform values
   to the native evaluator. It calls `shader_graph_eval_values_validated` on the
   validated program and returns all node values or an operation-ID diagnostic.
   The initial bridge is synchronous: native scratch and resolver views are
   borrowed only for the call; copy results into authored-owned arrays before
   releasing native storage. Pin program, uniform payload and resident-resource
   revision throughout the call, including derivative resampling. It retains
   no editor, graph or resolver pointers afterward.
6. Chibi joins values with the bundle's origins and principal-result bindings.
   It owns the snapshot's generation, sample/uniform request identity, values,
   source closure and limitation labels. Preserve nonfinite values and mark
   them; evaluator failure leaves the previous complete snapshot intact. A
   failure carries the attempted program generation and maps through that
   bundle's provenance, never the preserved snapshot's potentially reused IDs.
   Retain attempted provenance until the diagnostic has been consumed.
7. Every source-value query passes through Chibi's current-generation/source
   guard. Check the complete ordered identity/text closure, including imported
   helpers, and the active program identity before returning a current value.
   Distinguish stale, unmapped and evaluation failure. Historical snapshots can
   remain inspectable against their own documents, but cannot attach values to
   a changed editor buffer. Retire native generations after consumers release
   them; authored snapshots remain valid without the native program.

Validate a complete immutable current-document snapshot once when attaching a
value snapshot. Invalidate that attachment on any document/generation change;
each watch query checks the attachment identity instead of rescanning every
document. Keep provenance/call-depth budgets distinct from IR execution-node
limits, since aliases and origin-only callers need not emit nodes.

Keep the existing document/node/byte limits unless the implementation supplies
an independently justified replacement. Use length-carrying authored strings;
only actual native C-string boundaries need embedded-NUL checks. The initial
material uses no external images. Later image samples pin the catalog/resource
generation for evaluation and label the existing CPU base-level/gradient limits.
The evaluator's resource behavior does not become a Chibi reimplementation.

## Source provenance

Allocate nonzero operation IDs within each compiled bundle with collision
checking; the generation disambiguates different bundles. Preserve original
compiler occurrence IDs separately. A helper expansion records its enclosing
call-site ID, including calls that emit no node. Two calls to `TintGain` therefore
have separate contextual origins even when they use the same helper document.

Auxiliary IR nodes can share their originating expression. A principal source
result points to its actual result node; an immutable alias may point to an
earlier node. Do not infer either binding from IR order. A scalar or Float3
result has one principal node. A structural result such as Surface binds its
named fields individually; do not pretend that nine outputs fit one float4.
Retain declared types/field identities for display even when IR storage is flat.
Diagnostic source ranges and call chains come from these same authored tables.

## Smallest real material

Use the gradient source in [typed-facts.md](typed-facts.md), with `TintGain`
moved into an imported ordinary helper module. Keep two helper calls and add an
immutable alias for the first result. Proposed public domain records are Float2,
Float3, Sample and Surface in `game:shaders`; these are not currently available
shader source APIs. Parameters contains Float3 tint plus float gain/speed.

The entry returns an opaque, unlit Surface. Its calculation is:

```text
factor = sample.uv.x * parameters.gain + sample.time * parameters.speed
color = TintGain(parameters.tint, factor)
alias = color
emission = TintGain(alias, float(0.125))
```

Return color/emission with the existing surface defaults for all other fields.
The authored helper uses scalar float-field multiplication and explicit Float3
construction. This needs no vector arithmetic sugar, textures or new shader
operation. Use existing UV, time, parameter, constant, component, multiply/add
and vector-construction operations. Keep finite, explicitly float constants.

At UV.x=0.25, time=2, gain=0.5, speed=0.125 and tint=(0.8,0.4,0.2), independent
analytic expectations are factor=0.375, color=(0.3,0.15,0.075) and
emission=(0.0375,0.01875,0.009375). CPU source inspection must expose these
results, both helper contexts and the alias without inventing an extra node.

The owners' [source-material contract](source-material-contract.md) specifies
the proposed SourceMaterial/ShaderUniform constructors, exact typed defaults
and one authored finalization pass. It is agreed for source review, not
implemented or frozen. Use ordinary Build/Encode and the prepared-resource
path; produce existing shader IR/catalog resources directly without lowering
through an authored node-graph document a second time. The existing
Shader.parameters Text-of-asset-source contract and material/shape setters stay
unchanged for graph assets.

Install the derived shader in a minimal real project on one existing renderable
surface. The acceptance is that ordinary project preparation compiles the source
and helper, publishes the program, renders it through the existing material
catalog and inspects CPU samples from that same generation. A disconnected
facts probe or hand-constructed test IR does not satisfy this milestone.

## Dependencies and implementation sequence

Owner confirmations on 2026-10-22: reviewed main `5c28191b3` uses execution
ABI20; PR 665 reuses record-validation proofs but does not enable nested source
records. The compiler's Analyze/facts export is still proposed. Scalar package
constants and ordinary flat record results/projections are supported, while
DeclarePackage scalar-field restrictions and nested LiteralStep nominal context
still block the real fixture. The separate game-source library driver also
rejects constants, unions and nonflat records. Its restrictions cannot be
assumed away by using the package compiler. Shared resolution/provenance work
precedes a stable facts-export checkpoint. Do not import held pointer/query ABI
work or assume equal ABI numbers establish compatibility.

The typed-assets owner confirms committed source checkpoint `31098151b`, with
current-main integration and paired compiler/Studio proof still pending.
`Schemas.chibi` currently provides Graph/GraphDefinition.Named/GraphNode.Named;
there is no implemented source-material constructor. The dedicated typed
constructor and authored preparation seam now have an agreed source-review
proposal linked above, targeting existing shader IR/catalog resources directly.
The optional decoded cache `30cd287d1` is separate; this proposal
implies no asset schema/project-version change.

1. Agree the compiler owner's proposed private `compiler_facts` result: owned
   flat rows, result-local indices, exact typed bits, nominal/field/callable
   identities, ranges, source evaluation order, argument destination mapping,
   binding writes/ref escapes and final transitive effects. Record unsupported
   reachable forms with ranges. The ordinary Compile API stays separate. Facts
   schema and target ABI must match the reviewed owning checkpoint.
2. Prove the export/domain pass first with flat Float3 records, imported helper
   calls and aliases. Reject mutation, ref escape, nonzero effects, recursion
   and unknown natives at their originating ranges. This is an analysis and
   lowering milestone, not render acceptance.
3. Confirm compiler support for the real fixture's nested Sample.uv,
   Parameters.tint and Surface record fields/construction/projection. Scalar
   record support alone is insufficient. Keep any compiler/bootstrap work with
   its owner; do not substitute a native shader semantic pass for missing facts.
4. Agree the typed source-material constructor/resource seam, then implement
   authored lowering and inspection plus the narrow native transport/evaluation
   service. Directly replace overlapping C source-policy functions once those
   dependencies are concrete and source-reviewed. Keep one authoritative
   source-map/stale-check implementation without compatibility aliases.
5. Add the real fixture and a second material using the same shared path with
   different content, then expose the service in Studio. Package/SDK registration
   and compiler/typed changes require their respective reviewed checkpoints.

Expected ownership: `packages/game/shaders/` for public domain types;
`tools/compiler/shader_source/` for internal typed-facts validation, lowering,
source-map and inspection modules; a small `src/authoring/shader_source_host.[ch]`
for prepared invocation and native transport. These paths are proposals, not
reservations on another owner's files. Reuse `src/runtime/shaders/program.[ch]`
and renderer publication. Remove/replace only the redundant policy in
`source_debug.[ch]` in that authored implementation; retain the independent
evaluator repair.

## Implementation verification

### Independent authored source preparation

The new `tools/compiler/compiler_facts/Facts.chibi` schema is under compiler-owner
semantic review. Analyze production/export and nested records remain compiler
work; the schema is not evidence that source shaders compile.

Source-map policy can proceed independently of those compiler facts. Reviewed
main `d0de4a84` already loads/invokes closed prepared packages through
`compiler_host_load/invoke`, including authored records/arrays, strings and
full-width IDs. The new `tools/compiler/shader_source/SourceMap.chibi` prepares
that pure policy. Its production cutover needs a prepared closure, stable
root/entries, matching target ABI/layout, source/inventory/hash witness and SDK
facade. These resources are not yet supplied or generated by this lane. Do not
import pending typed-asset implementations to obtain a service facade.

The mechanical native facade will keep raw float lanes/resolvers/native memory
outside the policy payload. Use bounded document-pair matching entries and one
authored count/generation/all-match reducer for source attachment: serializing
two complete 8 MiB closures together could exceed the existing host's 64 MiB
JSON limit after escaping. Preserve the source byte limits and keep the final
staleness decision in Chibi. Install the prepared service and replace the C
policy calls together, with no C semantic fallback or runtime source compiler.
An installed consumer must run it with compiler/source discovery unavailable.

C/pico_unit cases run the actual authored pass through the prepared runtime.
Check analytic CPU values and typed error ranges, two imported helper contexts,
auxiliary nodes, aliases, exact constants and owned lifetime after input release.
Change caller/helper text and identity, entry/catalog/generation, sample and
uniforms independently; prove each stale/recapture outcome and failed-evaluation
recovery. Exercise rejected late publication and complete bundle replacement.

Prepare and render the real project with a supported backend. Compare controlled
interior samples using the renderer's actual color/output conversion and stated
tolerances; an unlit material still goes through that pipeline. Verify a source
edit changes the derived program, rendering and inspected values together.
CPU approximation and image/derivative limitations remain explicit. No CPU test
alone proves pixel equality or full-frame parity.

Run required repository groups and applicable build/format/canonicality checks
on the exact combined candidate. Studio checks follow its owning repository's
conventions. Keep platform/GPU gaps and actual elapsed times in the evidence.
No code or runtime check was run for this design checkpoint.
