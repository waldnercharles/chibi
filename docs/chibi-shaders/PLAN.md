# Shaders and materials in Chibi

This plan moves materials, shapes and shader libraries from KDL graph assets to
ordinary `.chibi` source, and then ports the engine's own GLSL shaders. It is written in the style of the [native LLVM
ledger](../native-llvm-ledger.md): each work package names what it delivers,
what it depends on, and the decision it waits for.

Status, 28 October 2026: the CPU inspection foundation (#663) and a scalar
lowering prototype over supplied compiler facts (#671) are merged, and the
owner has settled every open decision (P1-P4). Nothing compiles a real
`.chibi` material yet, and no thread owns the next package.

## Today

| Area | How it is authored | Where it runs |
| --- | --- | --- |
| Materials, shapes, shader libraries | KDL graph assets in `materials/`, `shapes/` and `shader-libraries/` (13 in this repository, all in test fixtures; Studio's are copies in `tests/held`) | `src/graph/shader/shader_adapter.c` lowers the graph to `shader_graph` IR (`src/runtime/shaders/program.h`). `program.c` validates it, evaluates it on the CPU (previews, picking, baking) and emits a GLSL function that `render/shader_program.c` links into the surface shaders. |
| glTF materials | `src/assets/model_import.c` writes a Standard material graph per glTF material to `content/materials/*.kdl` at import (R6) | As above |
| Engine pipeline shaders | 102 hand-written GLSL files in `src/render/shaders`, 51 of them for HRC (3D PBR, shadows, sky, SSAO, HRC, post) | Cute's runtime shader compiler (`cf_make_shader_from_source`) compiles GLSL 450 for each SDL_GPU backend |
| Behaviors, libraries, systems | `.chibi` source; graphs are a derived view | VM, SLJIT, and LLVM for the native subset |

The `shader_graph` IR is an expression DAG of 1 to 4 float lanes, with 70
operations, 32 parameter slots, 9 surface outputs and at most 1,024 nodes. It
has no loops, branches, matrices or integers. That covers every authored
material and shape today, and none of the engine pipeline shaders.

## Direction

- Source is authoritative. A material is `.chibi` source, and its graph,
  IR, GLSL and source map are derived from the same revision, the way
  behaviors already work.
- There is no second frontend. The stage-one compiler parses, resolves and
  type-checks shader source like any other source. A shader-domain pass,
  written in Chibi, checks the reachable functions and lowers them to shader
  IR. It reports anything a GPU cannot run at its source range rather than
  dropping it.
- Numbers already agree. `number` is `f32` on the CPU and the GPU (D3), so a
  shader float is a `number` with no narrowing rule. `f64` is rejected in the
  shader domain. When integers reach shaders, the emitted code keeps D1's
  meaning (wrapping, `x / 0` is 0), which GLSL leaves undefined, so it guards
  division and conversions.
- The CPU evaluator stays. Previews, picking, baking and source inspection keep
  evaluating the same IR the GPU code is generated from.

## Source form

The language rule is to infer, or reuse a form the language already has,
before adding syntax. Materials and shapes need neither a declaration kind nor
an attribute (P2).

- A material is an ordinary library function whose result is `Surface`. A shape
  is one whose result is `Shape`: a record with the signed `distance` and the
  constant half-extent `bounds` that today's shape assets author.
- Its parameters are the material's parameters. Their defaults are the
  material's defaults, and an entity's Shader component overrides them by
  name, as it does today. Parameter types come from the defaults (S2b).
- Per-sample inputs are context, not parameters. `sample()` returns the current
  `Sample` (position, object position, UV, normal, view direction and time).
  Helpers can call it too. The shader pass rejects it where gameplay code
  reaches it.
- Surface fields keep their current defaults, so a material returns only what
  it changes. Alpha Mode and Alpha Cutoff become Surface fields that must be
  constant, because lowering folds them into Opacity.

```chibi
Materials :: library {
    PulseRing :: (radius = 35.0, width = 3.0, color = vec3(0.05, 0.6, 1)) -> Surface {
        ring := ShaderMath.RingMask(sample().position, radius, width)
        return {emission: color * ring * ShaderMath.Pulse(sample().time, 2.0)}
    }

    SmoothUnion :: (radius = 20.0, blend = 8.0) -> Shape {
        p := sample().position
        a := length(p - vec2(-15, 0)) - radius
        b := length(p - vec2(15, 0)) - radius
        return {distance: smooth_min(a, b, blend), bounds: vec2(60, 30)}
    }
}

ShaderMath :: library {
    Pulse :: (time: number, speed: number) {
        return (sin(time * speed) + 1) * 0.5
    }

    RingMask :: (position: vec2, radius: number, width: number) {
        return saturate(1 - abs(length(position) - radius) / max(width, 0.001))
    }
}
```

`smooth_min` and the `Sample` field names are placeholders until S2 and S4
settle them.

`-> Surface` is the one annotation. Records are nominal, as in Odin and Rust,
and a `{...}` literal takes its type from context, so nothing else says which
record `{emission: ...}` is. The annotation also states the function's role.
Inferring it would need structural record types across the whole language,
which the owner declined. Helpers like `Pulse` return numbers and infer their
results as usual.

`ShaderMath` is an ordinary library (P3). Gameplay code can call `Pulse` too;
the shader pass checks only the functions a material reaches. The separate
`shader-libraries/` kind goes away.

### Compiler, engine and ECS

The shader compiler does not depend on the ECS, and the language has no
concept of a material. The caller names the entry and its contract, as HLSL's
`dxc -E` does: "compile `Materials.PulseRing` against the surface contract".
The renderer, Studio's preview, tests, export and an application without the
ECS module are all callers.

| Layer | Owns |
| --- | --- |
| Language and compiler | The shader pass: which types, natives and effects a GPU runs. No material, shape or lighting concept. |
| Engine shader package, outside the ECS | `Surface`, `Shape`, `Sample`, `sample()` and the lighting contract they feed. |
| ECS | The Shader component stores a function reference and its parameter overrides, and asks the compiler for that function. |

Pipeline shaders (S8) are complete GPU stages, so they carry the generic stage
attributes `@vertex`, `@fragment` and `@compute`, in the general attribute
form the language survey adopted (S32). These are GPU concepts, not engine
ones, and they let tools find entry points without the engine. Materials and
shapes are not stages: the engine's surface fragment entry calls them, as
`surface_material.shd` calls the generated material function today. Once that
entry is in Chibi, it takes the material as a function argument, which the
survey's non-escaping, inlined function values (S23) specialize per material.

### Instances

A material instance is asset data, as in Bevy and Unreal: it names a material
function and overrides some of its parameters. It uses the typed `.chibi`
asset form of #678, with an instance constructor beside `data.Scene` and
`data.Prototype`:

```chibi
package brick
import data "game:assets"

pub Build :: () -> u8[] {
    return data.Encode(data.Material("Brick", "Materials.Standard", [
        data.Field.Named("roughness", data.Value.Real(0.8))
    ]))
}
```

`data.Material` does not exist yet (S6). glTF import writes instances of one
shared standard material this way.

## Shader backend options

| Option | Path | For | Against |
| --- | --- | --- | --- |
| A. Existing IR, GLSL out | Source facts lower to today's `shader_graph`; `program.c` emits GLSL; Cute compiles it | No new backend. The CPU evaluator, previews, baking and web builds keep working. Fits every authored material today. | The IR has no loops, branches, matrices or integers. |
| B. Typed shader IR, GLSL out | A new IR with structured control flow, matrices and integers, still emitting GLSL 450 for Cute's compiler | Grows A to cover pipeline shaders. Same downstream path on every backend, web included. | A second IR to keep in step with the CPU evaluator, or the evaluator grows with it. |
| C. LLVM to SPIR-V | Lower through the native lane's LLVM IR and LLVM's SPIR-V backend | Reuses the LLVM lowering. | LLVM's SPIR-V backend is aimed at compute kernels; graphics stages are still maturing. Studio would need LLVM to preview an edit, while the native lane uses it ahead of time only. Metal, D3D12 and web still need cross-compiling. No CPU preview. |
| D. Direct SPIR-V | An authored emitter writes SPIR-V, as Naga and Slang do, and hands it to SDL_GPU | Removes the GLSL text step and glslang from the runtime; faster compiles. | Large. Needs reflection and cross-compiling for other backends, and web GLES still wants GLSL. |

Recommendation: A for materials now, and B for pipeline shaders, which P1
commits to. Design B's IR for pipeline shaders from the start, so that
materials move onto it rather than A and B living side by side. D only if
measured compile times justify it. C is not recommended.

## Work packages

| ID | Package | Delivers | Depends on | State |
| --- | --- | --- | --- | --- |
| S0 | CPU inspection foundation | `source_debug.[ch]`: source-mapped CPU snapshots of shader IR, with call-site chains and nonfinite flags (#663). Authored provenance policy and a scalar facts-to-IR prototype in `shader_source/` (#671). | none | done |
| S1 | Compiler facts export | The stage-one compiler exports resolved facts for a reachable function closure: types, ranges, expressions, calls and constants ([typed-facts.md](typed-facts.md)). No private records or bytecode decoding. | Compiler owner. Not the LLVM lane. | not started |
| S2 | Vector types | Built-in `f32` `vec2`/`vec3`/`vec4`, `quat` and `mat2`/`mat3`/`mat4`, memberwise arithmetic with broadcasting, swizzles and the common functions, on the CPU and in shaders, as the language survey adopted (S5). Needed before any material reads naturally. | none | not started |
| S2b | Parameter types from defaults | A parameter with a default and no annotation takes its default's type, in stage zero and stage one, so materials write `radius = 35.0`. An untyped `35` would infer `int` from its spelling. | none | not started |
| S3 | Shader-domain pass | Checks reachable functions are pure and use shader-supported types and calls; lowers them to `shader_graph` IR with a source map; ranged diagnostics for everything else. Replaces #671's supplied-facts prototype. | S1, S2 | not started |
| S4 | Material and shape functions | The shader pass compiles `-> Surface` and `-> Shape` functions against the engine shader package's contract, with `sample()` for per-sample inputs, and ordinary libraries replace `shader-libraries/`. In the same package, glTF import writes `.chibi` materials instead of KDL graphs, and a project migration rewrites the 13 KDL graph assets in this repository. Then the KDL graph format and `shader_adapter.c` are removed, and the graph becomes a derived view. | S3, S2b | not started |
| S5 | Studio | Materials open in the code editor with diagnostics and a live preview; the graph view and parameter overrides keep working; CPU watches come from `source_debug`. Shared navigation in SDK authoring modules. | S4 | not started |
| S6 | Texture parameters and instances | Image-typed parameters bound per draw (R4d). Material instances as typed `.chibi` assets (`data.Material`, see [Instances](#instances)). glTF import switches from one generated material function per glTF material to instances of one shared standard material. | S4, R4d, #678's asset form | not started |
| S7 | Control flow, matrices, integers | Option B's IR, designed for pipeline shaders: `if`, bounded loops, matrices, integers with D1 semantics, textures and stage inputs and outputs, on the CPU evaluator and in GLSL. Materials move from `shader_graph` onto it. | S3, S2 | not started |
| S8 | Pipeline shaders in Chibi | `@vertex`, `@fragment` and `@compute` entry points with declared bindings. The surface fragment entry takes the material as an inlined function argument (S23). Port the 102 engine shader files one family at a time (post, sky, shadows, 3D surface, SSAO, GI, HRC), each with GPU image checks against the GLSL it replaces, and delete the GLSL as each family lands. | S7, survey S23 and S32 | not started |
| S9 | Direct SPIR-V | Option D, only if S8's measurements show GLSL compilation is the bottleneck. | S8 | not planned |

### Relation to the LLVM lanes

Shaders do not wait for LLVM. S1 and S3 work on the stage-one compiler, which
the LLVM lane also lowers from. The overlaps are:

- A library function shared by gameplay and a material runs natively on the CPU
  only once the LLVM lane covers it (records in M4, imports in M5). Until then
  it runs on the VM, and in shaders through S3.
- Vector types (S2) are the same built-in types on the CPU and the GPU, so the
  native lane lowers them too, first as records of `number`, which M4 already
  supports.
- Integer semantics in S7 follow D1, which the native lane already pins.

## Decisions

| ID | Question | Decision | State |
| --- | --- | --- | --- |
| P1 | Do the engine's pipeline shaders move to Chibi too, or only authored materials and shapes? | Both: materials first (S1-S6), then the engine's pipeline shaders (S7-S8). (Owner, 27 October 2026.) | decided |
| P2 | How is a material written? | An ordinary function with a `-> Surface` result (`-> Shape` for shapes), parameter types from defaults and inputs through `sample()`. No `:: material` kind, no `@material` or `@shape`, and no structural record literals. The caller names the entry. (Owner, 28 October 2026.) | decided |
| P3 | Do shader libraries stay a separate kind? | No. One library kind; the shader pass checks what a material reaches. (Owner, 27 October 2026.) | decided |
| P4 | Which backend? | A now, then B designed for pipeline shaders; D only if measured. (Owner, 27 October 2026.) | decided |
| D2 (stdlib) | Built-in vector types or library records? | Built-in: the language survey adopted built-in `f32` vector, quaternion and matrix types (S5, 25 October 2026). | decided |

## CPU and GPU equivalence limits

Both backends run the same validated float operations and uniform values, but
results agree only within tolerances. libm, GPU approximations, fused
operations, rounding and denormals differ, so tests choose a tolerance per
operation and never claim bit identity or full-frame equivalence from a single
sample. Snapshots keep nonfinite intermediates and flag them.

CPU textures sample the resident base level with nearest or bilinear filtering;
the GPU uses mip chains, anisotropy and `textureGrad`, so minification cannot
match in general. CPU normal, bump and triplanar detail use explicit gradients
and finite differences, not hardware derivatives, so arbitrary `dFdx`, `dFdy`,
`fwidth` and implicit LOD are rejected by the shader pass until they have a
defined CPU neighborhood. Rasterization, lighting, blending, MSAA, discard and
lane divergence are outside a single material sample.

## Acceptance for each package

Each package adds `@test` functions that `chibi test` runs, or pico_unit cases
in `test/language` or `test/tools`, runs the required groups
(`chibi build test short native projects language`) and reports unexecuted
coverage. S4 and later render a real material and compare CPU samples with GPU
pixels at controlled coordinates, which waits for a renderer: the GPU selectors
are archived with `src/render`. They also prove with a second
material of different content that the path is not specific to the first. S5
verifies source edits, error ranges, undo and redo in Studio.
