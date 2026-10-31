# Shader graphs

Shader graphs describe how a point on an object looks. The renderer evaluates
that calculation across the object's pixels. They use the same graph editor as
behaviors, but have data wires rather than execution flow: there are no events,
delays, spawning, or mutable gameplay variables in a shader.

The three asset folders have different jobs:

| Folder | What an asset defines | Example |
| --- | --- | --- |
| `materials/` | Surface color, emission, roughness, metallic, normal and opacity | `PulseRing` varies emission with time; `OutlineGradient` varies color across the surface. |
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

An entity's **Shader** component chooses a Material and an optional 2D Shape.
In a 2D scene, **Appearance** supplies the built-in outline when no Shape is set.
In a 3D scene, **3D Geometry** supplies the solid: box, sphere, capsule, cone or
triangle prism or octahedron. Materials shade both paths. A 2D Shape graph does not turn into a 3D solid.
Without a material graph, ordinary material/appearance colors remain available.

To explore the example, open `scenes/ShaderDemo.kdl`, then open
`materials/PulseRing.kdl`. Its preview shows the material in isolation. Open the
`ShaderMath.Pulse` call to inspect the shared calculation. Open
`shapes/SmoothUnion.kdl` to inspect geometry instead of color. Select an actor
in the scene to change its Shader parameter values; the material defaults stay
shared while the selected instance's override is independent.

Parameters are named scalar, Boolean, vector or color inputs. Select a Parameter or
Constant to change its type. Vectors have inline numeric controls; Color uses a
floating point picker that preserves values above one for emission. Choosing the
current type keeps its value. Type changes preserve the common components. The
compiler supports up to 32 parameters per graph and assigns their internal slots.
A behavior can use **Set Material Parameter** to change an instance at runtime;
`DamageFlashBehavior` sets `Flash` on damage and resets it after a delay.

Every material ends in **Surface Output**:

| Input | Meaning |
| --- | --- |
| Base Color | The surface's RGB color. |
| Emission | RGB light emitted by the surface; values can exceed one. |
| Roughness | Zero gives a sharp highlight; one gives a broad highlight. |
| Metallic | Blends between a dielectric and a conductor. |
| Normal | World-space shading normal; zero uses the solid's geometry normal. |
| Opacity | Zero is invisible; one is opaque. Alpha Mode decides how it applies. |
| Unlit | Shows Base Color and Emission without the lighting response. |
| Height | World units of relief above the surface. 2D solids cast its shadows. |
| Occlusion | The share of ambient and sky light that reaches the surface, from zero to one (the default). 3D shading only; direct lights ignore it. |
| Alpha Mode | A setting. **Blend** (the default) mixes the surface over what lies behind by Opacity. **Opaque** ignores Opacity. **Mask** draws the surface whole where Opacity reaches Alpha Cutoff and not at all below it. |
| Alpha Cutoff | The Opacity at which Mask keeps a pixel; 0.5 by default. |

The modes follow glTF's `alphaMode`. In 3D, Blend surfaces with an Opacity
below one sort back to front, skip the depth buffer and cast no shadows; Opaque
and Mask surfaces draw with the solids and cast shadows. A masked surface casts
the shadow of its whole shape until shadow passes run materials. In 2D the
modes change only Opacity.

Material previews use a sphere with diameter 100 and the same shading path as
3D actors. The geometry normal and camera are real, so view-dependent effects
and roughness can be inspected. Shape previews retain their 2D distance view.

**UV** supplies normalized primitive coordinates. **Object Position** supplies
coordinates in the authored solid's units, after its dimensions and before actor
rotation/translation. **World Position**, **Normal** and **View Direction** are
3D vectors. **Position** supplies local 2D coordinates (the local XZ plane on a
3D solid); **Time** supplies simulation seconds. In 2D, coordinates map into
XZ with zero elevation, and Normal/View Direction point along positive Y.

## Designer effects

These operations replace common arithmetic chains. Mapping uses degrees; the
ordinary Sin/Cos nodes still take radians. Position inputs with an unedited,
unconnected default use the coordinate source listed below. Connecting a wire or
editing an explicit vector overrides that default.

| Node | Inputs and result | Coordinate/default behavior |
| --- | --- | --- |
| Mapping | Position 2D, Offset 2D, Rotation, Scale 2D → Position 2D | UV; scale `(1,1)`, zero rotation/offset. Applies scale, rotation, then offset. |
| Mapping3D | Position, Offset, XYZ Rotation and Scale, all 3D → Position 3D | Object Position; scale `(1,1,1)`. Rotates X, then Y, then Z, then adds offset. |
| Fresnel | Normal, View Direction, Power, Base → Factor | Real sample normal/view; Power 5 and Base 0.04. Both vectors normalize before evaluation. |
| Remap | Value, From Range, To Range, Clamp → Value | Both ranges default to `[0,1]`; Clamp is on. Supports scalars/vectors and descending ranges. A zero-width source range returns the target minimum. |
| Gradient | Position 2D, Type, Scale, Offset → Factor | UV; Type is X, Y, Radial or Angular. Scale 1, Offset 0. |
| Fractal Noise | Position, Scale, Detail, Roughness → Factor | Object Position; accepts connected 1D/2D/3D coordinates. Scale 1, Detail 4, Roughness 0.5. |
| Voronoi | Position, Scale, Randomness → Distance and Cell Color | Object Position; supports 2D/3D. Scale 1, Randomness 1. Zero randomness centers a feature in every grid cell. |
| Color Ramp | Factor → Color and Alpha | Factor 0.5; two opaque stops, black at 0 and white at 1. Stops and interpolation are authored settings. |
| Star Field | Direction 3D, Density, Brightness → Color 3D | World Position, the direction looked along in a sky material. One candidate star per cell of a 1024-cell grid on each cube face; Density is stars per square degree (0 to 100). Each star is a Gaussian about half a pixel wide from the direction's screen derivatives, keeping its energy at any pixel size, so Brightness is the brightest star's peak where a pixel spans 0.03 degrees. CPU evaluation uses that pixel size. |
| Surface Influence | Position 3D → Color 3D | World Position; planar activity from scene Surface Influence components. Returns black without a scene signal. |

Surface Influence samples colored activity in the world XZ plane. Connect it to
Emission for a surface that activates near entities carrying the **Surface
Influence** component. Its Color, Strength, Radius and Linger can be edited on
prototypes or animated by behaviors. `ArenaFloor` samples at each triangular
cell center, so a whole panel receives the same response. It combines the signal
with a grid mask to make edges brighter than the fill. This signal is independent
of Point Light, and isolated material previews have no activity sources.

Mapping transforms coordinates, so increasing Scale makes a sampled pattern
smaller. It implements the straightforward point transform described in
[Blender's Mapping documentation](https://docs.blender.org/manual/id/4.5/render/shader_nodes/vector/mapping.html).
It does not expose Blender's inverse Texture or Normal transform modes.

Fresnel computes `Base + (1-Base) * (1-clamp(dot(N,V),0,1))^Power`.
Base clamps to `[0,1]` and Power to `[0,64]`; Power zero returns one. This is a
controllable Schlick-style edge response, useful for rims and masks. It does not
replace the Surface renderer's physical roughness/metallic response.

Gradient X/Y return their respective coordinate. Radial returns twice the
distance from UV `(0.5,0.5)`; Angular maps the angle around that center into
`[0,1]` with a seam on negative X and defines the exact center as zero. Scale and
Offset apply afterward. Results are not clamped, so Mapping and Remap can repeat
or reshape them without losing information.

Fractal Noise adds smooth value-noise octaves at twice the preceding frequency,
weights each by Roughness, and normalizes the result to `[0,1]`. Detail counts
one to eight total octaves, including a weighted fractional final octave.
Roughness clamps to `[0,1]`. The control vocabulary follows the
[Noise Texture documentation](https://docs.blender.org/manual/en/4.0/render/shader_nodes/textures/noise.html),
but this uses our existing deterministic value noise, not Blender's Perlin noise.
Voronoi returns Euclidean distance to the nearest jittered cell feature, plus a
stable RGB cell identifier. It searches enough neighboring cells for the exact
nearest feature, with distance bounds skipping cells that cannot win. It does
not yet provide F2, distance-to-edge, or fractal Voronoi modes.

Noise, Fractal Noise and Voronoi repeat every 4096 input cells. Their shared
32-bit integer hash is identical on CPU/GPU. Floating point interpolation,
rotation, distance and transcendental operations are compared within numerical
tolerance; bit-identical final floats are not promised. A scaled coordinate that
overflows returns zero noise; Voronoi returns zero distance/color. Noise remains
procedural: no image has been loaded or sampled by these nodes.

Color Ramp stores 2–16 ordered stops. Each stop has a position in `[0,1]` and
floating point RGBA color; duplicate positions and nonfinite values are rejected.
Factors outside the first/last stop use that endpoint. RGB and alpha interpolate
independently using Linear, Constant or Smoothstep. Constant changes to the new
stop exactly at its position. HDR colors are retained; Surface clamps opacity
when the Alpha output is connected there. HSV/HSL, B-spline and cardinal
interpolation are not implemented. These controls follow the scalar-to-color and
separate alpha workflow in the
[Color Ramp documentation](https://docs.blender.org/manual/uk/4.5/render/shader_nodes/converter/color_ramp.html)
and its [typed API](https://docs.blender.org/api/4.5/bpy.types.ColorRamp.html).

Stops are an ordinary graph-owned array of reflected `ShaderRampStop` values:
`{ Position: float, Color: ShaderVec4 }`. Interpolation and Gradient Type are
literal settings, so they have no connectable sockets. `shader_node_ramp_get`
and `shader_node_ramp_set` give the Inspector typed access; normal graph
snapshots provide Undo/Redo. Save/load and clipboard copying preserve the nested
RGBA values. The compiler owns a separate stop copy and rejects invalid settings
without discarding the previous valid shader.

## Implementation

The authored JSON is loaded into graph assets. Library calls are resolved against
the project's asset catalog. The shader adapter checks supported nodes and types,
rejects cycles, and converts the graph to a small ordered intermediate
representation. That representation can be evaluated on the CPU or emitted as
GLSL. The renderer compiles the generated GLSL through Cute's shader backend and
supplies each entity's parameters, coordinates and time.

The 3D renderer evaluates Surface graphs per fragment. An instance parameter
texture keeps differently colored copies in the same opaque primitive batch.
Opaque solids write depth; transparent surfaces are sorted from far to near and
depth-tested without writing depth. Intersecting transparent solids still have
the usual limits of object sorting.

Direct lighting uses GGX, Smith visibility and Schlick Fresnel, following
[Filament's standard model](https://google.github.io/filament/main/filament.html#materialsystem/standardmodelsummary).
The current key light and hemisphere ambient establish material response.
Opaque and transparent surfaces now accumulate scene-linear color in an RGBA16F
canvas. Bloom and exposure run after transparency, followed by the Environment's
tone map (AgX or Khronos PBR Neutral; see `docs/rendering-3d.md`). This retains
bright emission for the glow pass and blends transparent materials before tone
mapping. RGB is limited to the finite half-float storage range. Environment
reflections and authored lights/shadows remain roadmap work. Emission and bloom
do not illuminate other 3D objects.

A scene can contain one **Environment** component. Arena3D includes an Environment
actor with Exposure, Bloom Strength, Bloom Threshold and Bloom Radius. Exposure
uses EV stops: +1 doubles linear brightness. Strength zero disables bloom. The
threshold measures the largest scene-linear RGB channel before exposure, keeping
saturated cyan and rose effects useful. Radius is displayed as a percentage of
viewport height. It controls the nominal Gaussian spread and keeps the halo
proportional when the window is resized. Defaults are 0 EV, strength 0.18,
threshold 1 and radius 1.2%. Material previews and scenes without an Environment
use these defaults. These controls use ordinary component history, overrides and
persistence. Duplicate components or invalid values show a Scene diagnostic and
retain the last valid image.

The pass downsamples bright color, filters at adjacent pyramid resolutions and
blends their results as radius changes. It shares the same renderer in editor,
standalone and WebGL. WebGL requires a float-color-buffer extension; unavailable
HDR support produces a diagnostic. This scene Environment follows the familiar
[Godot environment workflow](https://docs.godotengine.org/en/stable/tutorials/3d/environment_and_post_processing.html).
The strength, threshold and radius vocabulary also follows
[Unreal's bloom controls](https://dev.epicgames.com/documentation/unreal-engine/bloom-in-unreal-engine?lang=en-US).
It does not implement those engines' full post-processing feature sets.

On desktop, the 2D renderer translates Surface values into its internal scene,
emission, albedo and absorption buffers for radiance cascades. Designers do not
wire those buffers themselves. Normal still faces the camera along +Y; its X and
Z tilt the surface sideways and up, and Roughness sets gloss. Solids light that
relief from the directions of light received around them, and Height casts short
shadows across it. On web, a fragment shader draws the 2D material and shape graphs;
2D radiance cascades are not implemented there. The 3D Surface path is shared
across native and WebGL. Invalid graph edits preserve valid rendering and expose
diagnostics.

Mesh vertex deformation and the full expression catalog of other engines remain
open work. Supplied materials use Surface Output; the old lighting-buffer output
is removed.

## Arena development

Voidweaver and Arena3D, retired on 2026-10-17, kept their Surface materials in
their own archived repositories' `content/materials`.
The engine keeps focused regression fixtures rather than duplicate game assets. Shared pulse/ring functions and the designer effect nodes
keep each project's example roots focused on their visible effects.

The shared graph vocabulary now includes Vector3, Normalize, Clamp, Saturate,
Step, Smoothstep and Noise. Noise is smooth deterministic value noise with a
Scale input and a scalar result between zero and one. It accepts up to three
coordinate components, including ordinary 2D Position. It is not a claim of
Blender Noise Texture compatibility. Normalize returns zero for zero input;
Clamp sorts its two bounds; Smoothstep supports ascending or descending bounds
and uses a step when both bounds coincide. These operations have CPU/GPU parity
checks, including negative coordinates and extreme finite inputs.

Relevant implementation: `src/graph/shader/` owns validation, CPU evaluation and
GLSL generation; `src/render/shader_catalog.c` owns compiled graph assets and
parameters; `src/render/scene_renderer.c` connects entities to desktop/web
rendering.

## Image Texture

`Shader Image Texture` samples a PNG, JPG or JPEG and exposes separate **Color**
(RGB) and **Alpha** outputs. Connect Color to Surface Base Color or Emission;
connect Alpha to Opacity when the image should control transparency. Selecting an
image alone does not make an opaque material transparent. The Inspector shows
the image dimensions and a Reload action. Files can supply the image key through
the Image selector or a drag into the shader graph.

The selector searches project images and accepts a path directly. Dragging an
image onto the selector replaces its source; dragging onto the shader canvas
creates an Image Texture node. Each action has one Undo step. The control follows
the familiar image, interpolation and extension choices in Blender's
[Image Texture node](https://docs.blender.org/manual/en/5.1/render/shader_nodes/textures/image.html).

| Control | Default | Behavior |
| --- | --- | --- |
| UV | Surface UV | A Vec2 input; unconnected uses the current surface coordinates. |
| Image | Unset | A project/VFS asset key such as `content/textures/Mask.png`. An unset image reports an incomplete graph. |
| Color Space | sRGB | Decodes RGB into linear color before interpolation. Choose Linear for masks, normal data, or already-linear values. Alpha always remains linear. |
| Filter | Linear | Bilinear interpolation between neighboring pixels. Nearest selects one pixel without smoothing. |
| Wrap | Repeat | Repeats beyond the image boundaries. Clamp extends edge pixels. |

Image, Color Space, Filter and Wrap are authored settings with no input sockets.
Graph JSON and clipboard data store the image key and these settings. The image
bytes remain external assets. Different nodes can sample the same image with
different settings.

UV `(0,0)` is the top-left image edge; `(1,1)` is the bottom-right edge. Pixel
centers are `(column + 0.5, row + 0.5) / image_size`. Repeat wraps both neighbors
across a bilinear seam. Clamp repeats edge pixels rather than introducing a
border color. Nonfinite UVs return transparent black. The 2D shape Position
input keeps its existing Y-up local units; Surface UV flips Y for upright images.
Vertical 3D faces, spheres and capsules also use upright Y. Top faces use local
XZ, where positive Z points down in the arena. Box and prism face projections
follow geometry normals, so a sloped prism face does not switch projections
partway across its image.

Unconnected coordinates display their actual default, such as Surface UV or
Object Position, rather than numeric zeroes. The Inspector can switch that input
to Value for an explicit vector, or restore the automatic coordinates. The
compiler and editor share this rule. Already-visible input rows keep their size
when values or connections change; advanced-input visibility remains explicit.

The graph IR owns image keys and settings. CPU evaluation receives a resolver for
borrowed resident RGBA8 pixels, with straight alpha and rows stored top to bottom.
It performs the same addressing, sRGB conversion and filtering as the shader.
Generated GLSL calls one resource function per image; renderer code supplies its
sampler binding. Neither the graph compiler nor CPU sampler opens files or owns
GPU handles.

`src/render/shader_images.c` owns decoded pixels and GPU variants. It uses the
pinned Cute PNG/JPG loaders and texture API, documented in
[`cute_image.h`](https://github.com/RandyGaul/cute_framework/blob/a13f4661f5ba27e8d45027b52f121c1704786425/include/cute_image.h)
and [`cute_graphics.h`](https://github.com/RandyGaul/cute_framework/blob/a13f4661f5ba27e8d45027b52f121c1704786425/include/cute_graphics.h).
Loading and explicit Reload are authoring operations; rendering reads resident
resources. A failed decode or replacement keeps the previous pixels/textures and
reports the file error. Fix the file and Reload to publish its new contents.

Programs bind at most eight distinct image/settings variants. That budget applies
to one material with its shape, or one complete smooth-union metaball group. It
is not a project-wide image limit. Ordinary actors can be split into ordered
batches with different programs. Repeated opaque actors sharing a program and
mesh remain instanced. Native 2D compute uses ordered passes; WebGL 2D and the
3D renderer bind their own contiguous sampler sets.

CPU inspection samples the base image level. GPU bilinear image variants have
mipmaps and anisotropic filtering, and lit 3D materials use coordinate gradients
to select their sampling footprint. A CPU base-level sample therefore does not
reproduce a minified GPU pixel. Images are limited to 2048 pixels per side. The loader checks
dimensions before full decoding and GPU upload, so oversized replacements report
an error and preserve the previous image. The initial image format is RGBA8, so HDR and higher-bit-depth
image data are not preserved. CPU/GPU checks allow 0.003 absolute channel error
for hardware texture interpolation and sRGB decode precision. Tests also cover
image-key ownership, JSON/clipboard round trips, missing resources, independent
settings, and rendered material behavior across multiple image sets.

`EnergyCells` is an editable example with eleven connected nodes. Time and Scroll
Speed drive Mapping; Image Texture feeds a charcoal/cyan Color Ramp; Intensity
controls Emission. Tiling sets repetition. The 128-pixel grayscale source is
`content/textures/EnergyCells.png`. Open `ShaderImageDemo` to see the material on a
box, prism and sphere with independent instance parameters.
