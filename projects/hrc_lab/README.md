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
`surface_thickness=0` make that distinction explicit. It retains the ordinary
surface-lighting response: `surface_radius=128`, `surface_relief=1.5`,
`surface_bevel=1`, and `edge_light=1` on the six non-emitter receivers. Those
relief/bevel and receiver gain match the renderer defaults. The 128-unit
radius reaches across this 700×350 room to the emitter's 108-unit vertical
offset. The pre-Depth revision supports all of these controls, so the
historical flat comparison keeps them matched.

HRC feedback is enabled with `bounce_intensity=0.35`. The bounce shader feeds
surface albedo multiplied by received radiance back into the next transport
frame. `evidence/cornell2d-bounce-comparison.png` compares that setting with
`bounce_intensity=0.0`, captured at the same simulation frame. It compares this
approximate HRC feedback path, not a path-traced reference.

The camera's `GameHRC2D.fog` is set to exactly `0.3`. In the composite it weights
received HRC fluence in open camera-plane regions, creating glow around lights;
it is not volumetric phase-function scattering. `GameHRC2D.ambient` is exactly
`0.0`, so unlit scene color adds no constant fill. There is no other ambient
light component. The moving emitter and HRC feedback provide illumination;
only that emitter has nonzero material emission (`0.08`). The walls and blocks
retain red, green, white, cyan and amber albedos with zero self-emission. Their
received-light surface response remains enabled, though surfaces beyond the
HRC radius can still be dark. The scene adds no artificial constant brightness.

The renderer has separate fluid density, absorption, emission, and visibility
handling, but this scene does not use a volumetric scattering phase function.
`evidence/cornell2d-receivers-enabled.png` is a native-renderer sample with the
received-light surface path enabled. The labeled
`evidence/cornell2d-bounce-comparison.png` compares feedback on at
`bounce_intensity=0.35` with feedback off at `0`; both captures use the same
ambient, fog, receiver, and emitter settings above. At frame 239, the rendered
areas differ by 1.75/255 mean absolute pixel value, with 21% of pixels
different by more than 2/255. This is a small but measurable HRC feedback
effect, not a ground-truth indirect-light reference.

To capture the sweep with `preview_scene`, use 960×540 output, let simulation
advance, and capture a warmed sequence. The light completes one circuit in
eight seconds. The saved 15 fps video samples actual frames from that renderer
sequence without interpolation.
