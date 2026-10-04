# Generative model authoring implementation

Implement the authoring improvements identified by the Polaroid SDF and Blender comparison. Preserve the original reference model and comparison artifacts; new examples demonstrate each capability separately and in a combined asset. Work occurs in the existing declarative-modeling worktree.

## Scope and contracts

- Add surface bump, tangent normal maps, planar/cylindrical mapping, triplanar image sampling, and bounded decal masks. Detail runs at the final visible hit. Explicit linear image sampling is required for data maps. Surface detail cannot change a silhouette.
- Preserve transformed Part coordinates with analytic Jacobians, including nonuniform scale, reflection, and instances. Derivative sampling freezes the winning Part.
- Add profile Boolean operations and offsets, rounded and beveled extrusion, model field offsets/shells, bounded simple concave/Bezier profiles, and circular tube sweeps. Field thickness and tessellated curve limitations must be explicit.
- Add pivot transforms and named attachment frames with reusable instance exports.
- Add orthographic/reference alignment, selected-Part isolation and sections, and resource/Part cost inspection through shared APIs and Studio. Reference alignment is saved authoring data; preview inspection is transient.
- Add deterministic bounded model-to-mesh baking, live material/Part-coordinate preservation, OBJ output, provenance, and an atomic saved-source export service exposed through controller/MCP and Studio.

## Delivery sequence

1. Preserve source/index baseline and implement independent shared capabilities.
2. Register sources/tests and run focused CPU checks; repair semantic integration failures.
3. Author focused scenes and an improved reference asset through the public authoring tools.
4. Inspect native rendered scenes and an actual Studio authoring workflow; compare the SDF and baked result with matching views. Import the exported mesh in Blender.
5. Run the full native suite, build serial and worker Web, and exercise the Web editor.
6. Record graph/IR/resource costs, geometric limitations, validation evidence, and source preservation.

## Evidence rules

Correctness tests cover surface-frame behavior, bounded constructive geometry, invalid-input transactionality, source/reference round trips, bake normals/winding/material ownership, deterministic output and budget failures. Pixel evidence is reviewed visually. Fewer graph nodes are not a frame-time claim: report marching IR separately from per-hit material samples and mesh export size. No target GPU has been selected; the 4080 Super is available asset-generation hardware only.
