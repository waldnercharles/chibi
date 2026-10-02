# Imported mesh capability fixture

`ImportedFoldedCard` is an independent two-panel mesh with a four-color image, a banked pose, and no ship, combat, or particle behavior. Its mesh source is `content/meshes/ImportedFoldedCard.mesh.json`. Material, prototype, camera and scene were authored and validated through Chibi MCP.

The native Metal preview in `imported-folded-card.png` uses a 640 × 400 output and fixed half-resolution HRC (320 × 200). The texture also supplies emission so its four regions remain visible without external lights. This is a correctness fixture, not a performance benchmark.

`GameCamera.focal_distance` optionally projects meshes in perspective. Collision and HRC light transport remain planar. Studio's scene selection bounds and mesh picking also use planar coordinates, so they do not yet follow perspective projection.

Run the CPU and native GPU mesh regressions with:

```
build/test_suite --mesh-material
```

The regressions reject malformed, non-finite, overflowing, zero-normal and degenerate mesh data while preserving the previous valid asset. They verify imported UV retention, transformed picking and normals, rotated imported bounds, frontmost depth, asymmetric image coordinates, and A/B/A GPU mesh cache identity changes. An unrelated Color Ramp material in the catalog verifies that the mesh program declares ramp uniforms before generated graph functions.

Headless scenario contexts initialize and mount a scoped virtual filesystem for image-backed material sampling. GPU previews retain ownership of their existing filesystem.
