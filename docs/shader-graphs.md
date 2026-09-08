# Shader graphs

Shader graphs describe how a point on an object looks. The renderer evaluates
that calculation across the object's pixels. They use the same graph editor as
behaviors, but have data wires rather than execution flow: there are no events,
delays, spawning, or mutable gameplay variables in a shader.

The three asset folders have different jobs:

| Folder | What an asset defines | Example |
| --- | --- | --- |
| `materials/` | Surface color and lighting coefficients | `PulseRing` varies emission with time; `OutlineGradient` varies color across the surface. |
| `shapes/` | An object's visible outline as a signed distance function | `SmoothUnion` blends two circles into one silhouette. |
| `shader-libraries/` | Named pure functions shared by materials and shapes | `ShaderMath.Pulse(Time, Speed)` returns a repeating brightness value. |

A library does not render anything by itself, and it is not attached to an
entity. Its functions become calls inside other shader graphs. Editing
`ShaderMath.Pulse` changes both supplied materials that call it. It is the shader
equivalent of a reusable math function; its calculations are included in the
compiled shader. Behavior function libraries in `libraries/` are separate because
the gameplay runtime and GPU support different operations.

A shape outputs one number: distance to its boundary. Negative means inside,
zero is the edge, and positive means outside. Circle and Box calculate simple
distances; Union, Difference, Intersection and Smooth Union combine them. The
renderer uses the result for coverage and antialiasing. CPU evaluation also lets
selection follow the same outline. Shape bounds are authored half extents that
limit drawing and framing; keep them large enough to contain the shape. Visual
shapes are separate from physics colliders.

An entity's **Shader** component chooses a Material and an optional Shape.
Without a shape graph, its **Appearance** component supplies the built-in shape.
Without a material graph, ordinary material/appearance colors remain available.
That is why ShaderDemo can show two custom materials on circles alongside a
custom SmoothUnion shape with a normal color.

To explore the example, open `scenes/ShaderDemo.json`, then open
`materials/PulseRing.json`. Its preview shows the material in isolation. Open the
`ShaderMath.Pulse` call to inspect the shared calculation. Open
`shapes/SmoothUnion.json` to inspect geometry instead of color. Select an actor
in the scene to change its Shader parameter values; the material defaults stay
shared while the selected instance's override is independent.

Parameters are named scalar or vector inputs, with up to 32 slots per graph.
Local Position, World Position and Time are renderer inputs. A behavior can use
**Set Material Parameter** to change an instance at runtime; the supplied
`DamageFlashBehavior` sets `Flash` on damage and resets it after a delay.

## Implementation

The authored JSON is loaded into graph assets. Library calls are resolved against
the project's asset catalog. The shader adapter checks supported nodes and types,
rejects cycles, and converts the graph to a small ordered intermediate
representation. That representation can be evaluated on the CPU or emitted as
GLSL. The renderer compiles the generated GLSL through Cute's shader backend and
supplies each entity's parameters, coordinates and time.

On desktop, the material outputs feed scene color, emission, albedo and absorption
buffers, including separate border coefficients. The existing radiance cascades
consume these buffers for lighting. On web, a fragment shader draws material and
shape graphs; radiance cascades are not implemented there. Invalid edits retain
the last valid rendering and expose diagnostics.

This is a focused 2D procedural shader system. It is not Unreal's full material
system: it does not currently offer texture sampling, mesh vertex deformation,
3D materials, or every Unreal material expression. The supported vocabulary is
scalar/vector arithmetic, position/time inputs, common math, parameters and 2D
signed distance operations. Materials and shapes share that vocabulary, while
their output nodes define their different roles.

Relevant implementation: `src/graph/shader/` owns validation, CPU evaluation and
GLSL generation; `src/render/shader_catalog.c` owns compiled graph assets and
parameters; `src/render/scene_renderer.c` connects entities to desktop/web
rendering.
