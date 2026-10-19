# Clay SDF prototype baseline

This is an implementation checkpoint for the [reviewed prototype plan](https://github.com/waldnercharles/chibi/blob/3efb07d3cd6371816604d15a9386e44c246dbc89/docs/plans/clay-sdf-prototype/PLAN.md).
It records source inspection, native builds and bounded CPU validation, not a
completed rendering or interchange gate. The first deliverable is an authored
material laboratory; the intended vertical slice remains the four-scene
playground and bounded VDB bridge from that plan.

## Source and ownership

Inspected on 19 October 2026:

- Engine main: `07eb2abaa5a097352c472746b2aadf2f97cd5e4e`.
- Main tree: `35bc3bca435f427564de423d22a9b473ecc47701`.
- Measured candidate tree: `be110662e6b3928636e0d23821e9aeedc018f0ac`.
- Plan: `3efb07d3cd6371816604d15a9386e44c246dbc89`.
- Separately owned toolchain cleanup branch:
  `29d6bdaedd2c4be5f99ee843a15a2b3385f49067`, after the prepared-writer change.

The prototype starts from main. It does not import the cleanup branch's
unfinished self-hosting work. PR 407 owns operation-contract integration in
the capability catalog, native definitions, lowering and SDK/test registration;
prototype registration must preserve its changes when the branches integrate.
Studio PR 77 is independently owned and is not a prerequisite of the native
laboratory.

## Authored/native boundary

Use current `.chibi` declarations for components, prototypes, behaviors and
systems. They own presets, input, presentation timing, scene rules and tuning.
Current model/material graphs and scene/input documents remain KDL because
those are their existing executable asset contracts. Runtime shape and material
parameter setters already exist; controls do not justify a new native API.

The initial models use existing primitives, CSG and bounded transforms.
The initial materials use the existing single-lobe dielectric response,
roughness and bump operations. Oiliness is a documented roughness/detail mapping.
No new grammar, raw shader escape hatch, reflection lobe or scattering model is
introduced by this checkpoint. A future Chibi shader frontend retains the plan's
separate design gate.

Reusable renderer work belongs in native code where CPU/GPU integration requires
it. It must not absorb the laboratory's scene composition or behavior. Initial
physics uses explicit existing collision proxies; a rendered field does not
implicitly become a physical collider.

## Confirmed integration boundaries

- The existing preview/planar SDF path can resolve models and material graphs.
  Its lighting is not scene3d's GGX lighting, so its appearance cannot approve the
  final scene3d preset.
- Scene3d currently collects `Geometry3D` primitives and meshes without selecting
  SDF drawing from `SdfSurface3D`. A model may therefore be picked differently
  from the geometry that this path displays.
- CPU model picking already exists in `scene3d_pick.c`. Its residual tolerance,
  coordinate conversion and error/exhaustion handling still need the field
  safety and renderer-parity gates. `model_program_hit` resolves part/material
  attribution at a point; it is not itself an intersection query.
- Model snapshots mutate shader-catalog caches. The renderer bridge must not
  obtain them concurrently inside scene3d's parallel entity collection.
- The renderer bridge must prove mixed opaque depth, camera/inside clipping,
  instance lifetime, material parity, picking and field-shaped directional
  shadows before claiming the scene3d gate.

## Validation status and environment

On 19 October 2026 the approved task-local Clang 19.1.7, matching resource
headers and OpenGL development headers were installed and verified. Pinned
native dependency sources were prepared without changing system packages or
shell profiles. No Emscripten installation or browser execution is covered.

The native build completed for Debug `chibi_ctl`, full `test_tools` and
`test_language`, the unsanitized Release `chibi_ctl`, and the compiler-free
`make tools-build` target. Debug retains assertions, ASan and UBSan. Leak
detection was explicitly disabled by the owner because LeakSanitizer cannot
run under this environment's ptrace; leak coverage is **disabled, not passed**.
The laboratory uses the real renderer-owned prepared-resource host, which
publishes CPU shader/model generations without opening a window in its CPU test.
A desktop-OFF metadata-only host does not validate this contract.

After independent source/workload review and canonicalization, the frozen
candidate above passed these production-supervised checks. The final authored
input fix changes `ClayControls` from the movement-only `on Input` event to the
existing named Form/Fingerprints/Variation/Oiliness/Reset actions. The prior
candidate failed at the first toggle; the current case passes through real input
sampling, without a new grammar or native gameplay change.

| Check | Passed evidence |
| --- | --- |
| `clay-format-check` | Canonical assets; 0.113 s |
| `clay-fields` | 5 cases, 197 assertions; 0.164 s |
| `clay-lab` | 1 case, 9,037 assertions, 26 simulation steps; 0.640 s |
| Compiler-free `make test-tools` | 134 cases, 27,390 assertions; 24.547 s |
| Supporting CPU selectors | `model-graphs`, `model-catalog`, `shader-graphs`, `desktop-dependencies`, `reference-index`, `material-surface-mesh`, `terrain-seams`, `terrain-checkpoint` |

Each check ran under one unchanged 60-second production supervisor deadline;
configuration/build time is separate. The checks consumed the frozen assets
with audited matching native inputs and resources. Builds alone do not imply
execution of the language aggregate or any other unlisted check.

The default Vulkan loader reported no device (instance result `-9`); selecting
the installed Chromium SwiftShader ICD enumerated the software `SwiftShader
Device (Subzero)` (instance result `0`). The separately admitted 320×180
offscreen preview targeted this Vulkan route with one CPU step, one GPU warmup
and one output frame. It failed during initial rendering: UBSan rejected a null argument
to a zero-size `memcmp` in the pinned Cute drawing dependency, causing SIGABRT
after 12.330 seconds under the outer supervisor. No image was produced and
pixels remain unverified. This is a dependency correctness failure, not a
device-availability diagnosis. Remediation is paused pending approval for the
narrow dependency fix. No reference hardware or frame budget is established.

Before publication, apply [CUT62](testing.md#batch-validation-cut62) to the final
candidate: refresh its identity after any change, review affected contracts and
workloads, and validate the final combination. Final exact-head validation is
still pending; the results above identify the previously frozen measured tree.
Recheck main, ownership, explicit holds, approvals and server requirements. The four aggregate driver groups are
not claimed by these focused results; full native/render/Studio suites remain
quarantined, disabled and not passed.

## Remaining gates

G0 and G1 remain open. The bounded numerical corpus and authored CPU preparation
now pass, but baseline image review, production ray-query diagnostics, CPU/GPU
parity and the scene3d integration evidence remain incomplete. These results do
not approve the final material appearance. Renderer implementation follows the
supported field contract.

OpenVDB import/export remains a separate bounded asset-build task. It must retain
the original source, select one supported float32 level-set grid, validate units,
transforms, sign, band and error bounds, and complete an external two-way DCC
roundtrip. No dependency has been added and no VDB compatibility is claimed.
NanoVDB runtime storage, USD, persistent sculpting and simulation remain separately
gated. Software-rendered correctness images cannot establish the proposed
1280-by-720/60-Hz performance target.
