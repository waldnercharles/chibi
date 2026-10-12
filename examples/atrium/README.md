# Atrium

A sunlit courtyard at golden hour that shows the 3D renderer's features in one
scene, written only as Chibi assets: a KDL scene, shader-graph materials, a
particle shader library and two small behaviors.

| What you see | Feature |
| --- | --- |
| Terracotta, teal and cream walls tinting the floor and each other; crystals lighting the shade around them | Global illumination (`global_illumination "High"`): probes trace the solids' `Material` colors and glow |
| The sky behind the walls, its light in the shade and in every reflection | HDR sky (`kiara_dawn.hdr`) as background, ambient light and image-based reflections |
| Column stripes on the floor and the east wall | Directional sun with soft shadows (`angular_diameter`) |
| Round light spots from the bronze screen | Masked materials (`Lattice`) cast their cut-outs in shadows |
| The bronze sculpture in the corner | Spot light with shadows |
| Street lanterns under the loggia | Imported glTF model (base color, metal-roughness, emission and normal maps) lit by point lights with shadows |
| Five spheres on plinths | Metal-roughness PBR, from a mirror to rough plaster |
| The orb by the pool | Blended glass with Fresnel edges |
| The pool | Bump-mapped ripples reflecting the sky |
| Flagstones, marble veins, plaster and patinated bronze | Procedural shader-graph materials with `Shader Bump` |
| Contact shadows where solids meet the ground | Ambient occlusion (`occlusion "High"`) |
| Glow around the crystals and the gyroscope | Emission and bloom |
| The overall exposure and color | HDR with auto exposure and AgX tone mapping |
| The spinning gyroscope | `VisualMotion` on box-frame and triangle-outline primitives |
| The wisp circling the gyroscope | `Trail` and a moving point light, driven by `Orbit.chibi` |
| Fireflies drifting in the shade | `ParticleField3D` with `Fireflies.Drift` from a shader library, clocked by `FieldClock.chibi` |

The scene also uses every 3D primitive but the flat triangle: box, sphere,
capsule, cone, triangle prism, octahedron, tetrahedron, chevron pyramid, box
frame and triangle outline.

From the engine checkout, after `make install`:

```sh
chibi examples/atrium
```

High-quality global illumination traces 128 rays for each of up to 4096 probes
every frame, which suits a desktop GPU; set the `Environment` entity's
`global_illumination` to `"Medium"` or `"Low"` on smaller ones. To render a still
without a window, see `docs/agent-execution.md`:

```sh
echo '{"operation":"preview_scene","kind":"scene","name":"Atrium","width":640,"height":360,"gpu_warmup_frames":8}' > /tmp/atrium.json
python3 tools/agent.py preview /tmp/atrium.json --project-root examples/atrium --output /tmp/atrium-result.json
```

## Credits

- `content/skies/kiara_dawn.hdr`: "Kiara 1 Dawn" from [Poly Haven](https://polyhaven.com/a/kiara_1_dawn), CC0.
- `content/meshes/Lantern.mesh` and `content/textures/Lantern`: "Lantern" from the
  [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Lantern),
  © 2017 Microsoft, CC0. Its textures were scaled to 512 pixels.
