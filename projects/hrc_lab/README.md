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
`surface_thickness=0` make that distinction explicit.

The camera's `GameHRC2D.fog` is set to exactly `0.3`. In the current composite,
it weights received HRC fluence in the open air, creating the glow around
lights. It is not volumetric phase-function scattering. `GameHRC2D.ambient` is
set to exactly `0.0`, disabling the unlit scene-color contribution; no other
ambient-light component is present in this scene. The moving emitter and HRC
feedback provide its illumination. Since ambient is off, the colored panels
and blocks use a small material-emission tint (`0.012`; neutral floor `0.004`)
to keep their red, green and white palette visible. These are authored surface
emissions, not an ambient-light contribution or calibrated Cornell reflectance.

To capture the sweep with `preview_scene`, use 960×540 output, let simulation
advance, and capture a warmed sequence. The light completes one circuit in
eight seconds. The saved 15 fps video samples actual frames from that renderer
sequence without interpolation.
