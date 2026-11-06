# A reactive arena first iteration

Reviewed September 9, 2026. This is the visual target for a playable editor-authored increment. Timing, palette and cost values below are proposed tuning targets, not measured properties of Sektori or completed engine capabilities.

Build the existing movement, aiming, shooting, collision and enemy encounter into a convincing small game. The visual work should make each action readable, give actors deliberate solid construction, and make the floor register nearby activity. Additional weapons, dash mechanics and moving arena topology are outside this increment. A definitive fiction or theme can follow once this presentation works.

## What the reference shows

The three supplied images were inspected at their original aspect ratios.

| Reference | Visible evidence | Useful direction |
| --- | --- | --- |
| Attachment 1, filename ending `c79d.png` | A near-black triangular floor remains visible between pink hollow square bodies. Local groups of floor lines become gold near bright action. The player has a small bright core and cyan surrounding light. Pink perimeter lines remain thin. | Let events reveal the floor locally. Give the player a recognizable core, and preserve dark gaps between effects. |
| Attachment 2, filename ending `5528.png` | Fine purple paths overlap the upper-left combat group; gold floor triangles sit beneath that cluster. Cyan shells, solid pink bodies and a dark empty area remain distinct. | Vary the scale and thickness of marks. A bright effect can occupy a large region while its individual lines stay much thinner than actor silhouettes. |
| Attachment 3, filename ending `f6ed.png` | Hollow triangular bodies, compact cubes, repeated facets and a large radial object have visible side faces. Large saturated red floor regions coexist with gold borders and smaller cyan, yellow and pink actors. | Shape construction and height matter. A small collection of composed polygon bodies can establish depth before any SDF renderer exists. The saturated floor is a peak reference, not the resting state for our first encounter. |

The linked [Nintendo Switch 2 launch trailer](https://www.youtube.com/watch?v=3QfnePRu-KE) loaded in the in-app browser after the web extractor was throttled. Frames around 0:08 and 0:30 were inspected. They show dense near-overhead combat, localized cyan and orange light, thin projectile paths, dark patterned gaps and large faceted bodies with dark side faces. This was a bounded visual inspection, not a frame-by-frame measurement of impact or floor-light timing.

The [developer's Sektori page](https://www.gimblll.com/sektori/) describes deliberately detailed, saturated visuals and handcrafted attack waves combined with variation. That supports composing encounters and effects together. Neither that page nor the inspected frames establish a specific lighting algorithm, SDF rendering, or the causal trigger for every bright floor patch. The clarified target is emissive panel activation, with its implementation owned by this engine.

This extends the [previous visual review](arena-visual-direction.md). The earlier diamond floor and individual box enemies were useful construction tests. The first playable visual target requires a triangular floor, local event response and a more deliberate actor silhouette.

## Corrected floor behavior, September 9

The user clarified that the panels themselves light up beneath players and
projectiles, like a disco floor. A physically lit surface does not satisfy this
request. Sample an activity signal once at each triangle's cell position and use
that shared value to drive the panel's own emission. The whole panel responds
coherently, with restrained intensity and a brief decay after the source leaves.
Actual projectile lighting is secondary and subtle.

Keep activity separate from the floor's representation. A later holographic mesh
or force field can use the same event positions, radii and age to drive geometric
warps or waves. Geometry Wars-style displacement is an experiment for later;
this increment proves panel activation and event decay first.

## Event palette and reaction targets

Use one persistent palette while the theme remains open. Color expresses role; silhouette, location and motion supply additional distinctions. These durations are starting values to tune in played combat at the normal camera distance.

| Event or role | Color and shape | Proposed response |
| --- | --- | --- |
| Idle floor | Charcoal with dim cool gray-blue triangle edges | Readable during motion, below the default bloom threshold. No continuous whole-floor pulse. |
| Player | Cyan outer body, pale ice core, unmistakable forward point | A compact floor pool follows movement. Firing creates a short forward flash and a narrow shot wake. Keep the core readable throughout. |
| Player projectile | Pale ice core with cyan halo | Illuminate roughly one to two floor cells locally. A wake decays over 0.12–0.25 seconds and stops when the projectile is destroyed. |
| Enemy body | Coral or rose, with a dark inset and faceted outer profile | Brief spawn anticipation if the encounter needs it. Movement retains a stable silhouette; decoration must not resemble a projectile. |
| Hostile projectile, when already present | Orange-red compact core with a shape distinct from friendly shots | A smaller warm floor response follows its actual trajectory. Do not add a new enemy attack solely to demonstrate this. |
| Projectile impact | Tiny pale core followed by amber directional fragments | Flash for about 0.04–0.08 seconds, then decay. A two-to-three-cell floor pulse lasts about 0.15–0.30 seconds. Orient fragments from contact information. |
| Enemy destroyed | Amber flash mixed with fragments in the body's color | A recognizable outward breakup over about 0.2–0.45 seconds. The event is stronger than an ordinary hit, while incoming threats keep their visible cores. |
| Player hit | A short warm accent at the player with a distinct expanding outline | Make damage legible at the player's position. Keep any camera displacement bounded and subordinate to aim. |

Changing a palette or timing should mean editing an asset, material control or effect parameter in the editor. A renderer should not contain rules such as "pink means enemy" or name checks for an Arena asset.

## Triangular floor

Start with a stable equilateral triangle lattice aligned to world coordinates. Use a coarse cell pitch around the width of an ordinary enemy, then inspect it at 1280×720 and 1920×1080. Approximately 24–32 world units is a useful first range for the current 640×360 arena. Keep the resting lines thin and antialiased. A faint inset triangle per cell can provide detail after the base pattern reads clearly.

Treat floor response as a separate signal from the lattice pattern. Persistent sources follow actors and projectiles. One-shot sources carry position, color, radius, strength and age. The floor material samples the signal at each triangle center to illuminate its panel and edges coherently. Keep those values editable; do not make every projectile a tiny entity that must be placed manually in the scene.

Overlapping sources must not turn the entire floor white. Limit decorative accumulation while preserving the brightest local events, and retain a distinct actor core above the floor response. The authored material converts this signal directly into panel emission. Actual contact shadows remain a separate depth improvement.

## Actor geometry now, SDF later

Use rasterized, composed polygon solids for this increment. Make the player a compact pointed body with an inset core or short side pieces. Give the existing pursuer an outer shell and dark center, or a short radial arrangement of facets. Visible side faces, a deliberate negative space and a stable top silhouette will do more than changing a box's color or adding bloom.

The editor should support reusable visual construction and preview. Keep part transforms relative to their parent, shared material choices and ordinary saved assets. A modest body with about three to eight pieces is enough to evaluate the authoring workflow. It should still read at its real play size. Physics colliders remain independent of this visual detail.

The [3D rendering research](arena-3d-rendering.md) already recommends raster geometry as the playable baseline, with bounded SDF objects as a later experiment. That decision still fits. A hollow frame or faceted pursuer has no demonstrated need for sphere tracing. Revisit SDFs when one required body depends on smooth blends, subtraction or continuous deformation that cannot be authored cleanly with the polygon tools. Compare one such body with a mesh version at the actual camera distance before expanding the renderer.

The current implementation step adds a selectable octahedron with eight flat faces, independent width/depth/height and exact convex picking. Its [mesh construction](../../src/render/arena_geometry.c) shares the existing prism path, and [render checks](../../test/arena3d.c) cover rotated silhouettes, empty corners inside the authoring bounds, front and underside views, and repeated-instance batching. This supplies a useful faceted enemy body. It does not implement parented visual parts, hollow constructions or the composed-body authoring requirements above.

## Engine work with immediate value

| Capability | Reuse and authoring requirement | Existing foundation |
| --- | --- | --- |
| Reactive floor signal | Component-authored moving sources and graph-triggered impulses. Expose useful color, extent, falloff and lifetime controls. Make material sampling independent of Arena-specific names. | Surface graphs, world position and instance parameters are already part of the [material workflow](../arena-development.md#spatial-authoring-and-surface-materials). |
| Event effects | Trigger hit, destruction and spawn effects from the existing behavior/contact flow. Expose reusable presets or coherent library functions, with the event decision visible at graph root. | [Emit Directed Particles](../../src/graph/nodes/particle_nodes.c) already exposes direction, color, lifetime, size, spread, drag and emission. [Particle storage](../../src/render/particles.h) has a 2,048-particle cap. Extend this path only where the rendered result demands it. |
| Composed solids | Relative part transforms, material reuse, preview, selection, save/reopen and undo. A single prototype should own its visual construction. | [3D Geometry](../../src/runtime/components.h) now exposes one box, sphere, capsule, cone, triangle prism or octahedron per entity. |
| Short trails and breakup | Make motion direction and hit direction visible at a controllable density. Match depth and the existing HDR path. | [Arena renderer](../../src/render/arena3d.c) already collects particles and spatial actors into the same presentation. |
| Effects quality controls | Reduce bloom and decorative particles while retaining projectile cores and hit confirmation. Visible counts and rejected decorative emissions support diagnosis. | [Environment controls](../arena-development.md#hdr-emission-and-environment-authoring) already expose bloom strength, threshold, radius and exposure. |

Do not build a second generic event bus or an all-purpose geometry graph before these concrete authoring tasks require it. Reuse the existing behavior graph, Surface materials, prototype system and particles.

## Evidence required before calling it a showcase

- Play the authored encounter with ordinary movement and firing. Record a quiet frame, a crowded moving frame, an impact and the strongest overlapping effect. Include the player near the border and a projectile passing under another visual effect.
- At normal play size, identify the player, its aim and incoming threats immediately. Their shapes must stay visible with bloom at zero. Floor reactions may support those reads but cannot be their only cue.
- Stop firing and confirm wakes and impulses decay. Pause, restart and scene reload must not leave stale light or particles behind. Contact events must produce one coherent reaction per actual event.
- Change a reaction color/radius, one effect lifetime and one actor part in the actual editor. Check Undo/Redo, save/reopen and Play. Verify standalone and editor Play use those same assets.
- Keep the existing performance gate from [renderer research](arena-3d-rendering.md#acceptance-and-decision-gates). Record viewport, scene counts, effect settings and frame-time percentiles for a two-minute played sample after warm-up. The proposed first target remains p95 at or below 16.7 ms and p99 at or below 20 ms at 1920×1080 on the named M4 Pro. These are acceptance targets, not current measurements.

A successful first iteration has a recognizable player, a deliberate enemy body, a floor that reacts to actual play, and hit/death effects that explain what happened. It should be possible to tune that result through the editor and publish the same authored scene.

## Follow-up motion reference

The follow-up brief refines the floor toward hollow outlines: small inset triangles at rest, larger nested outlines around activity, and dark interiors. This replaces the earlier filled-panel treatment as the visual target. The three new supplied stills show that distinction clearly.

The [second gameplay reference](https://www.youtube.com/watch?v=RV8g5RcSeMo&t=85s) was inspected in the browser with paused frame stepping from 85.0 through 88.3 seconds. Web text extraction was throttled.

- At 85.0–86.0 seconds, the floor is dark with localized amber nested outlines. At 86.5 seconds a broad patch has larger outlines; at 86.75 seconds a sharply bounded cyan patch appears. These remain recognizable triangles, with dark space inside them.
- By 87.5–88.3 seconds, orange-red motifs cover a broad outer band and an interior patch while the central field stays dark. Arena geometry changes afterward. The clip does not establish that the original trigger is a wave; applying a restrained red travelling pulse to our authored wave event is a design choice.
- The close-up still shows a magenta directional core, separate pale cyan fins and a cyan perimeter effect. The moving frames preserve a small, pointed silhouette. They do not support a precise roll axis or amplitude measurement; our roll should preserve that directional read.
- Fine yellow pickup outlines change orientation and apparent size. Curling yellow arcs converge around the player at 85.5–86.5 seconds. Exact synchronization with the soundtrack was not verified from muted frame samples. Shared-beat pickup scaling and curved collection paths are chosen responses to the user's requested motion.

Validate these changes through actual played movement, mouse firing and collection. The optional native review records ten seconds from the ordinary simulation, including two naturally timed wave pulses, without inserting entities or changing encounter state for the camera.
