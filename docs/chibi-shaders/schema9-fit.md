# Shader dependency fit at the rich schema-9 checkpoint

Read-only source assessment, 2026-10-22. Input is immutable Git tree
`b029a6f59d1ff2f8185541538c90c1930a0e3cce`, based on
`5c28191b3ea53afa232c34e37b14fd9b01024aef`, in the compiler repository.
The supplied patch SHA256 is
`8e24028fde54b093ca41f3dcea4b51a5424bcc633de734ec859fd2cf51a0ee9b`.
All 28 source-file SHA256s in the supplied frozen manifest were checked against
Git blobs. References below are line numbers in that tree, not mutable worktree
files. Reproduce a source read with `git show <tree>:<path>`.

The checkpoint supplies useful native-catalog inputs, but not the proposed
owned shader typed-facts result. It still rejects the authored nested records
needed by the real gradient material. No source was imported, compiler code
edited, artifact generated or test/build run for this assessment. The reported
named Append effect/debug mismatch and broad/resource/default eligibility gates
remain with the compiler/coordinator owners.

## Facts available and still needed

| Shader requirement | Evidence at the pinned tree | Remaining dependency |
| --- | --- | --- |
| Native callable identity and classification | `src/toolchain/compiler_target.c:250–294` exports IDs/signature ABIs, names, qualified names, kind, actual linked intrinsic and direct-call admission. `Compile.chibi:149–159` defines CatalogNative. | Reuse these shared catalog decisions in the facts producer. Direct-call admission does not imply shader purity or a shader operation. No source-call expression/callee table is exported. |
| Native port types and defaults | `Compile.chibi:213–224` carries storage/container, nominal ID, C-string flag and default status/bits/text. `compiler_target.c:175–247` copies exact float32 bits; aggregate/inferred defaults remain unsupported. `Behavior.chibi:919–970` resolves eligible nominal native records/handles. | These are native declaration defaults, not typed source literals, construction fields or the descriptor's Float3 uniform defaults. They do not establish authored nested-record support. |
| Native and transitive effects | `compiler_target.c:352–356` exports matching native masks. `Compile.chibi:1975–1993` computes the call-graph fixed point; `Assembly:315–316` retains private summaries. | The facts API must expose final effects for each reached source callable and ranged writes/ref escapes. Do not infer purity from a catalog row or decode prepared entry effects. The named Append mismatch remains an independent eligibility hold. |
| Immutable documents, imports and syntax ranges | `source_snapshot/Snapshot.chibi:33–96` already exposes nodes, documents with identities/text/paths and ranged imports. | Reuse this snapshot infrastructure. It supplies syntax/provenance inputs, not resolved expression types, operation/conversion choices or contextual helper-expansion IDs. |
| Resolved source expressions, bindings and calls | Public `package_compiler.Result` is only Rejected or Compiled(module,bytes), `Compile.chibi:310–312`; Compile/CompileWithEffects return that result at `2042–2096`. No compiler_facts package/entry exists in the tree. | Export the agreed owned document/origin/type/field/function/binding/statement/expression/argument rows from actual semantic decisions. Exact float/bool constants, nominal types, conversions and unsupported reachable ranges remain missing as consumer facts. |
| Source argument order/default binding | `Steps.chibi:1467–1478` explicitly separates source evaluation order from parameter destination; CallStep implements binding/default handling. | Export those resolved bindings as owned facts, including defaulted arguments. NativeArgument/Value/Work are private implementation records, not a consumer API. |
| Source points and principal results | `Body.chibi:99–124` has occurrence/range/type debugger points/watches, later serialized at `Compile.chibi:1899–1918`. | Still need compiler occurrence facts and resolved result identities, then Chibi-owned helper expansion chains, aggregate field results and aliases. Prepared watch slots cannot serve as shader semantic facts. |

`Compile.chibi` above means `tools/compiler/package_compiler/Compile.chibi`;
Body/Steps/Behavior are in the same directory. Snapshot is beneath
`tools/compiler/source_snapshot/`.

The richer input route is specifically `CompileBehaviorsWithCatalog`, which
returns bytes (`Behavior.chibi:987–1000`). The ordinary package route still uses
legacy Native/Port inputs (`Compile.chibi:2051–2057`), so the earlier proposed
Analyze signature must be reviewed against the shared rich-catalog decisions.
Do not copy the legacy shape and silently drop nominal/default/intrinsic data,
or add a second catalog resolver in the shader pass.

Existing `unit_analysis.Analyze` is not a source-to-typed-facts producer:
`tools/compiler/unit_analysis/Analysis.chibi:8–116,851` takes already resolved
environment tables and returns unit-analysis results. Likewise
`effect_analysis/Effects.chibi:92–96` takes resolved tables to produce effect
arrays. These are reusable analyses after inputs exist, not a replacement for
the missing ordinary-compiler facts export. Neither is newly added by this
checkpoint's diff.

## Nested-record dependency map

| Operation needed by the material | Pinned source and status |
| --- | --- |
| Flat Float2/Float3 declarations | Scalar float fields fit `Compile.chibi:681–694,809–825`. Existing flat record support remains useful for the first analysis/lowering fixture. |
| Sample.uv, Parameters.tint, Surface.base_color/emission fields | Still rejected: DeclarePackage calls ScalarStorage, rejects non-scalars and records each field with `record = -1`, `Compile.chibi:809–825`. |
| Nested contextual construction, including supplying an existing Float3 | Still blocked independently: LiteralStep gives each field initializer absent nominal context and checks against `record = -1`, `Steps.chibi:1352–1359,1383–1387`; SameType requires exact record identity, `Body.chibi:885–886`. |
| Nested layout and prepared type requirements | Lifting the declaration guard alone is insufficient. Layout hashing assumes scalar field sizes/absent type names, `Compile.chibi:1349–1365`; requirements emit absent field type names, `1607–1625`. Resolve and preserve ordinary nested nominal/layout information throughout this path. |
| Nested field projection | Machinery preserves `field.record` and projects an address for a catalog-known nested field, `Steps.chibi:1197–1239`. Its inline-address case describes native records; that is not proof for authored nested record representation/copying. |
| Flat return/call/import identity | Signatures preserve declared record indices (`Compile.chibi:1197–1218,1264–1271`); qualified type lookup preserves imported identity (`580–603`). Return and call code retains that identity (`Steps.chibi:294–295,626–634,2160–2166,2450–2462,2524–2526`). These parts can support the flat imported TintGain milestone. |
| Separate executable-library route | PromoteRecords remains scalar-only and rejects field record identities, `Behavior.chibi:500–517`. The native source_graph_library adapter also admits scalar parameters/results and omits unlowerable functions (`src/authoring/source_graph_library.c:63–77,220–250`); it is not a complete authored shader frontend. |

Source tests cover flat construction/calls/projection/returns
(`test/language/package_compiler_chibi.c:1027–1047`), imported flat records
(`1604–1654`), named flat results (`924–965`) and rejected literals
(`2110–2133`). They do not establish the material's nested records. This
assessment did not execute them or extend their coverage claims.

## Smallest remaining sequence

1. After the compiler owner's shared resolution/provenance checkpoint is
   eligible, implement/source-review its owned facts export for flat Float3,
   imported TintGain called twice, an immutable alias, scalar add/multiply and
   exact constants. Include ranged rejects for mutation/ref/effects/recursion/
   unknown natives. Produce facts from ordinary semantics, not private table
   consumption or prepared-bytecode reconstruction.
2. Extend ordinary nested declarations, nominal layouts/requirements,
   contextual construction and authored projection/call/return/import handling
   together, with differential coverage for the actual Sample/Parameters/Surface
   shape. Native fixed-record port admission is not this step.
3. Implement the agreed typed [SourceMaterial contract](source-material-contract.md)
   after its migration checkpoint. Its authored finalization calls the Chibi
   shader pass once and returns existing shader IR/catalog resources directly.
4. Integrate generation-bound rendering and CPU source inspection and validate
   the real material. The flat facts probe cannot close that acceptance gate.

The checkpoint narrows catalog plumbing work; it does not remove either of the
first two compiler dependencies or make the source-only tree eligible to import.
