# Graph overhaul

Approved implementation scope: readable behavior graphs, reliable control nodes,
executable extraction, shared authored functions, typed events, and GPU material
and 2D shape graphs on desktop and web. Desktop radiance cascades remain in use;
web radiance and Box2D/Box3D integration are separate future work.

## Acceptance checklist

- [x] Correct control semantics and migrate sample loops.
- [x] Executable extraction, visible sample decisions, compact node presentation.
- [x] Shared synchronous function assets, typed local/entity events.
- [x] Distinct multi-boundary macro expansion.
- [x] Shader graph validation, GLSL generation, CPU shape evaluation, serialization.
- [x] Material/shape editor authoring, reusable functions and instance parameters.
- [x] Desktop and web rendering integration, last-valid preview and diagnostics.
- [x] Gameplay, GPU, browser, native interaction, build and save/reload verification.

## Design references

- [Blueprint best practices](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-best-practices-in-unreal-engine)
- [Blueprint flow control](https://dev.epicgames.com/documentation/en-us/unreal-engine/flow-control-in-unreal-engine)
- [Material functions](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-functions-overview)

Node count is not a readability target. Keep useful engine operations and
recognizable calculations, and expose decisions where the graph benefits from
them. Libraries are project-local, stateless and synchronous. Shader graphs use
GPU-supported pure values and explicit parameters. Shape bounds are authored;
colliders remain separate from visual shapes.

## Authoring

Create assets from the Files navigator's + menu. Behavior Function Libraries
contain synchronous functions. Shader Function Libraries contain pure GPU
functions. Node search includes local functions, macros, qualified library
functions, variable reads/writes, and prototype spawn actions. Dragging a pin into
empty canvas space filters these results by compatible connections. Opening a
library call takes you to its source asset.
Signature edits preserve declaration IDs and existing wires. A connected field
must be disconnected before an incompatible type change or removal.

Event Definitions declare named payload fields. Choose a definition on an event
receiver, Call Event, or Send Event. Call Event queues delivery within the current
behavior instance; Send Event targets an entity. Payload values are copied before
delivery, including across a delay.

Extract Function accepts a connected execution chain with one entry and exit,
including latent work. Macros support multiple execution boundaries and receive
independent graph copies per call during linking. They retain a runtime call
boundary for suspension and debugging. Sequence still suspends its activation
when a branch delays; this is a deliberate difference from Unreal's scheduling.

An unconnected execution output ends its branch; pending Sequence outputs and
loop iterations still run. Return exits the current function or event activation
and discards its pending continuations. Every function requires Return, including
functions without data outputs. Returning from a called function resumes its
caller without cancelling the caller's remaining work.

Movement Input fires each frame while movement is held and once with a zero
vector on release or viewport focus loss. Idle frames do not dispatch Input.

Particle inputs marked Advanced stay hidden until connected or expanded through
the node's context menu. Existing engine operations remain available. Sample
behaviors expose stationary and elapsed-time decisions and use named math where
it makes the operation easier to recognize.

Material and Shape assets use GPU nodes, typed parameters, and a preview with
pause/time controls. Shape half extents bound rendering and selection. Add a
Shader component to an entity or prototype, choose Material/Shape assets, and
edit their named parameters. Set Material Parameter changes an instance value
at runtime. Parameters support scalar and vector values, with 32 slots per graph.
Invalid shader edits keep the last valid rendering and show diagnostics.

Open `content/scenes/ShaderDemo.kdl` and frame the scene to inspect PulseRing,
OutlineGradient, and SmoothUnion. Both materials call `ShaderMath.Pulse` from
`content/shader-libraries/ShaderMath.kdl`. DamageFlashBehavior demonstrates a
runtime parameter write followed by a delayed reset.

The renderer uses material graphs for scene color and lighting channels on
desktop. The web build uses the fragment renderer for material and shape graphs.
Web radiance cascades remain deferred. Visual SDFs do not define physics colliders;
Box2D and Erin Catto's [Box3D](https://github.com/erincatto/box3d) remain future integrations.

## Verification

When this landed (3ee9cebec, 2026-09-08), the Makefile's `test` target passed
with ASan/UBSan, including runtime, authoring, reload, and headless application
checks. The Makefile and its `dev` launch target have since been retired, and
the renderer is archived.

Browser verification opened ShaderDemo, framed its two materials and custom SDF,
edited Radius from 24 to 16, and restored 24 with one Undo. PulseRing opens and
previews correctly at fullscreen size. This found and fixed GLES include lookup,
uniform binding, reflected-value initialization, and 16-bit ImGui index overflow.
ImGui uses 32-bit indices consistently across desktop and web.
