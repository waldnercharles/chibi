# HRC Lab

## Cornell-inspired flat 2D scene

Launch the 2D HRC comparison scene with the Release build:

```sh
build-release/chibi --play --project-root projects/hrc_lab \
  --scene scenes/HRCLabCornell2D.kdl
```

`HRCLabCornell2D` is a top-down flat composition built from 2D appearances and
materials. It has a neutral floor, red and green side strips, a white back
strip, two colored rectangular blockers, and a warm rectangular emitter that
moves on an eight-second ellipse. It uses the ordinary 2D HRC transport path;
the scene has no `GameGeometry3D`, SDF surface, camera-relative depth mode, or
thickness estimate. `surface_mode="Blur"`, `surface_depth_gain=0`, and
`surface_thickness=0` make that distinction explicit. `surface_radius=0`,
`surface_relief=0`, `surface_bevel=0`, and material `edge_light=0` leave the
ordinary 2D surface-lighting fill disabled as well.

HRC feedback is enabled with `bounce_intensity=0.35`. The bounce shader feeds
surface albedo multiplied by received radiance back into the next transport
frame. `evidence/cornell2d-bounce-comparison.png` compares that setting with
`bounce_intensity=0.0`, captured at the same simulation frame. It compares this
approximate HRC feedback path, not a path-traced reference.

The camera's `GameHRC2D.fog` is set to exactly `0.3`. In the current composite,
it weights received HRC fluence in the open air, creating the glow around
lights. It is not volumetric phase-function scattering. `GameHRC2D.ambient` is
set to exactly `0.0`, disabling the unlit scene-color contribution; no other
ambient-light component is present in this scene. The moving emitter and HRC
feedback provide its illumination. Only the moving area emitter has nonzero
material emission (`0.08`). The walls and blocks retain red, green, white,
cyan and amber albedos and are not emissive; with ambient off, surfaces outside
the 2D HRC lighting reach can remain dark. The scene does not add artificial
glow to fill them in.

The renderer has separate fluid density, absorption, emission, and visibility
handling, but this scene does not use a volumetric scattering phase function.
`evidence/cornell2d-render.png` is a native-renderer still of the scene.

To capture the sweep with `preview_scene`, use 960×540 output, let simulation
advance, and capture a warmed sequence. The light completes one circuit in
eight seconds. The saved 15 fps video samples actual frames from that renderer
sequence without interpolation.
