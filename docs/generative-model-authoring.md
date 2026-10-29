# Generative model authoring

The construction graph describes volume, named parts, dimensions and attachment frames. Material graphs describe color, roughness, metallic response and small surface detail. The saved graphs remain editable when a mesh is exported.

## Build from an image

1. Save the reference and attribution with the asset. Match the main camera projection and large silhouettes first. A single photograph leaves hidden surfaces uncertain; keep inferred features simple and inspect front, side and back.
2. Name meaningful parts and expose only dimensions that should vary. Use profile extrusion/revolution for manufactured shapes, bounded sweeps for cables and handles, and attachment frames for reusable assemblies.
3. Keep silhouette changes and actual holes in construction. Put shallow grooves, grain, fine grip texture and printed markings in materials when they do not need silhouette, collision or independent occlusion.
4. Select mapping explicitly. Part coordinates keep texture attached to reusable components; object coordinates span an assembly. Planar mapping suits labels, cylindrical mapping suits a barrel, and triplanar color sampling avoids a UV unwrap on irregular surfaces.
5. Compare smooth, detailed and geometric controls in a focused scene. Check a reflected or nonuniformly scaled instance too. A convincing front view does not establish a usable three-dimensional asset.
6. Inspect construction and material budgets separately. Bake a mesh only after the editable construction is satisfactory, then compare the baked result at the intended view and resolution.

## Surface detail

Bump changes the shading normal using a scalar height gradient. A normal map supplies tangent-space normal direction and needs a mapping orientation. Both are evaluated at the resolved visible surface, so they do not add samples to the distance march. They do add per-hit material work. Neither changes the silhouette or creates geometric cavities.

Normal and other data textures use Linear sampling. Color images may use sRGB. The Normal Map node exposes OpenGL/DirectX conventions. Keep roughness, color and detail strength explicit instead of embedding them in copied expressions. A bounded Decal Mask controls a region of a material; the material graph chooses which properties to blend.

The existing HRC relief Height output and the new Bump normal output have different jobs. A bump normal alone does not become a heightfield occluder. This work does not change HRC into volumetric three-dimensional GI.

## Bounded construction

Profile Boolean operations can form holes before extrusion. Rounded and beveled extrusion control the rim without a separate graph for each edge. Field offsets and shells operate on the distance field; after nonuniform scale, their thickness is not necessarily a uniform physical wall thickness.

Concave polygons must be simple and consistently ordered. Bezier paths are converted to a bounded polygon or polyline; the sampled result defines the modeled shape. Circular tube sweeps have spherical joints/caps. They do not provide an arbitrary transported cross-section. Keep curve resolution small until the silhouette requires more samples.

## Inspection and export

Reference alignment belongs to saved authoring data. View orientation, overlay opacity, Part isolation and section cuts are temporary inspection controls. Inspect individual parts before adding details that hide assembly mistakes.

Construction IR counts describe work inside the distance and hit programs. Per-Part counts include shared inputs and cannot be summed. Material reports describe their own IR, uniforms, image variants, ramp data and sampling expressions. These counts help identify cost; they are not GPU timings.

Mesh baking samples a fixed validated parameter snapshot with explicit cell, triangle, sample and work budgets. Studio's Bake Limits panel exposes the triangle, sample-query and node-work budgets; defaults remain bounded. The reference camera's finer test uses cell size `0.2`, 150,000 triangles (a bake makes at most 166,496), 8,000,000 sample queries and 10,000,000,000 node work. Thin features below grid resolution can disappear. Reported vertex residual is a sampled field diagnostic, not a guaranteed global geometric error. Material ownership is chosen per triangle and can approximate a boundary.

The engine mesh retains live material references, Part coordinates and coordinate frames. OBJ carries geometry, normals, approximate box UVs and material slot names with neutral placeholders. It does not translate arbitrary engine shaders into Blender materials. Provenance records source hashes, parameters, bounds and bake settings. Exporting into an existing destination is rejected, so earlier output and editable source survive a failed bake.
