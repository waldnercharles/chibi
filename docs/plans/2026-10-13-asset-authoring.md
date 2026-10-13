# Asset authoring and game creation

Implement the five approved asset-pipeline improvements and make material and
shader authoring the first complete example of shared CLI and Studio tools.
Readable project sources remain authoritative. Shared SDK modules own discovery,
creation, validation and inspection; the controller and Studio present them.

## Context

The engine candidate starts at `e16b0a286` and Studio at `78cb4d4`, in isolated
worktrees under `.worktrees/asset-authoring`. The primary checkouts have unrelated
changes and remain untouched. KDL/.chibi sources, reflected capabilities,
controller discovery, scenario record/replay, prepared resource manifests,
standard PBR material generation and bounded previews already exist.

## System impact

- Source files, not tool-specific state, remain the source of truth.
- Asset schemas describe structural grammar and reuse reflected field metadata.
  Incomplete schemas declare their limits; canonical loaders and compilers retain
  semantic authority.
- Imports gain owned immutable plans, dependency/destination snapshots, saved
  recipes and conflict-aware publication. Authored changes survive reimport.
- Resource inventory reports exact file identities, bounded metadata, direct
  literal consumers and actual inclusion policies. Unknown cost/use stays unknown.
- Material generation and shader reports are shared SDK capabilities consumed by
  both the JSON controller and Studio.
- Evidence records source/build/backend/parameter identities, reproduction inputs
  and coverage. Simulation hashes, CPU samples, GPU captures and timings retain
  separate meanings.

## Implementation

1. Add `assets/asset_schema` and shared reflected descriptions. Describe every
   creation family with canonical examples; implement structural schemas and
   precise diagnostics for supported declarative documents. Use them in discovery,
   KDL language assistance and controller validation.
2. Refactor `assets/model_import` into prepare/apply. Add metrics, structured
   warnings, dependency snapshots and `assets/model_import_recipe`. Reimport
   unchanged generated files; preserve edited material/prototype files; reject
   conflicts, stale plans and escaping paths before publishing.
3. Add `authoring/resource_catalog` with deterministic bounded raw-resource
   inventory, metadata and direct consumers. Connect it to controller discovery
   and prepared manifest inclusion explanations.
4. Add shared `authoring/shader_report` and standard-material creation. Extend
   `inspect_graph`, controller creation and Studio Properties/New Material flows.
5. Add reproducible evidence identities and recipes around existing validation,
   scenario recording/replay and bounded shader previews. Include raw-resource
   changes in replay freshness checks.
6. Document the broader interface for CLI and editor authoring of materials,
   shaders, meshes, SDFs, procedural fields, scenes and tilemaps. Identify current
   capabilities, missing capabilities and verifiable delivery milestones.

## Publication and recovery

Import recipes identify generated and editable outputs and last-published hashes.
Edited editable outputs are preserved in full, and the report says source updates
to those outputs were not applied. Output identity changes, edited obsolete files,
unexpected missing files and unrelated name collisions are conflicts. Prepare
writes nothing; apply rechecks inputs, stages outputs and recipe, publishes the
recipe last and rolls back reported failures. This is process-failure rollback,
not a guarantee of filesystem-wide crash atomicity.

## Verification

- Add meaningful C pico_unit cases to existing dispatchers for schema diagnostics,
  metadata, import repeatability/conflicts, resource metadata and shader generation
  and reports. Do not create a linked executable for each probe.
- Build the engine/controller/test drivers. Run relevant admitted selectors and
  required groups under the production supervisor's 60-second deadline.
- Verify installed SDK public headers and build Studio against the installed SDK.
- Build Studio for web and exercise the material workflow with bounded interaction
  evidence where admitted. A startup/build alone is not interaction evidence.
- Produce one bounded material preview with fixed inputs and retain its request,
  result and exact source/build identity. Report unexecuted coverage explicitly.
- Independently review the affected shared contracts and failure paths before
  publishing. No merge or deployment is implied by this implementation request.

## Progress

- [x] Inspect current sources and isolate clean engine and Studio candidates.
- [x] Confirm material/shader authoring as the first CLI/editor parity workflow.
- [x] Implement and integrate the shared modules and controller operations.
- [x] Implement Studio material creation and shader inspection.
- [x] Verify deterministic behavior, failure preservation and bounded evidence.
- [x] Write the broader interface recommendations and final evidence report.
