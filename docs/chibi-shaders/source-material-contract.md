# Typed source-material contract

Agreed for source review between shader and typed-assets owners, 2026-10-22.
This is not an implemented or frozen API. Constructor work follows the queued
typed migration checkpoint; shader preparation also needs the compiler's
reviewed facts export and nested-record support. It is within the existing
authoring goal, with no additional human permission step.

## Constructor and descriptor

The typed-assets owner proposes these `game:assets` declarations:

```chibi
pub ShaderUniform :: union {
    Float(name: string, value: float)
    | Float3(name: string, x: float, y: float, z: float)
}

pub SourceMaterial :: (
    name: string, module: string, entry: string,
    uniforms: ShaderUniform[], fields: Field[]
) -> Value
```

The minimal material's ordinary asset Build returns:

```chibi
return data.Encode(data.SourceMaterial(
    "Gradient", "project:shaders/Gradient", "Shade",
    [
        data.ShaderUniform.Float3("tint", float(0.8), float(0.4), float(0.2)),
        data.ShaderUniform.Float("gain", float(0.5)),
        data.ShaderUniform.Float("speed", float(0.125))
    ], []
));
```

`module` is a routed module reference resolved relative to the originating
Build module and its roots. `entry` names an ordinary callable in that module.
Executable shader text is not embedded in a generic graph field.

The proposed immutable input descriptor has `kind = "source_material"`,
`name`, `domain = "material"`, `source = {module, entry}`, and `uniforms`.
Each uniform contains `name`, `type = "float" | "float3"`, and `value` as
Real or a three-Real List. Additional fields are existing material metadata.
The constructor rejects duplicate uniform names, empty required names,
reserved-key collisions and nonfinite defaults. The discriminator is authored
compiler input; it implies no graph version or project-schema bump.

Shader-domain validation resolves uniform names to the entry Parameters
record's field identities. Require exactly one default for every supported
field, with no unknown fields; descriptor order does not determine slots.
`Float` matches float32; `Float3` matches the agreed `game:shaders` Float3
nominal type with float x/y/z fields. Same-layout unrelated records are not
silently substituted. The native IR's existing 32 parameter slots apply;
Float3 consumes one width-three slot. A fixed unlit bool in Surface is a source
constant, not a third uniform kind.

Constructor float values widen losslessly to the asset Real(double) carrier.
Preparation preserves the supplied binary32 value; it does not infer precision
from formatted spelling or silently narrow a generic double. Validate direct
descriptors too: reject wrong numeric variants, nonfinite values and values
that require rounding to float32. Use existing material metadata validation.
Preserve signed zero through widening and roundtrip; narrowing followed by a
numeric equality check alone cannot prove its sign. Do not add parameter types
solely to support this example. The typed-assets owner agreed these domain
constraints; no float32 lowering/transport proof is claimed by that agreement.

The first slice explicitly selects the existing Opaque alpha mode and rejects
Blend/Mask metadata. The authored shader pass applies the existing opacity
lowering once: Opaque forces the opacity output to one. The graph adapter does
this for graph inputs today; direct IR preparation must preserve that contract
without running the adapter again. Broader alpha modes need their existing
lowering rules and separate coverage before they are accepted here.

## One preparation pass

1. The existing asset compiler compiles ordinary Build; the values host runs
   Build/Encode to an owned immutable descriptor. Nothing is published yet.
2. Authored asset finalization dispatches `source_material` to the proposed
   `shader_source.Prepare`, passing the descriptor, originating module, exact
   ordered snapshot/roots, and compiler/catalog/lowering identities. Native
   glue can marshal the invocation but owns no discriminator/schema/domain
   policy. Ordinary Build's values catalog does not gain shader-analysis
   capabilities.
3. The Chibi shader pass resolves the source reference in that frozen snapshot,
   obtains ordinary compiler facts and produces IR, source maps, typed defaults
   and material metadata once as one owned immutable bundle. Required imports
   must be in the same snapshot. Missing source causes a diagnostic or a new
   complete snapshot/retry; do not extend a frozen closure with live file reads.
4. The narrow bridge bounds, validates and owns a copy of the existing
   shader_graph IR. Resource/catalog publication atomically binds the bundle's
   token/provenance to the CPU and GPU program generation. Failed or late
   results publish nothing. Diagnostics retain both descriptor origin and
   attempted shader-source ranges/generation.

SourceMaterial remains input and is never overwritten with derived IR. The
output is the existing shader_graph IR/catalog resource shape. It is not an
authored node-graph document passed through `shader_graph_from_graph` again;
that would lower twice and detach provenance. Graph materials keep their
existing explicit path. A narrow prepared-IR/catalog entry may be needed, with
no new source parser or second semantic pass.

The existing `Shader.parameters` Text-of-asset-source contract and
`set_material_parameter`/`set_shape_parameter` APIs are unchanged by this
constructor. The shader [boundary plan](chibi-boundary.md) owns generation,
inspection, stale-source and resource-lifetime rules for the derived bundle.

## Review and acceptance

Typed assets owns the constructor, descriptor finalization dispatch and values
host. Shader authoring owns Prepare, domain validation, lowering, provenance
and inspection. The compiler owner supplies resolved facts and ordinary nested
record semantics. Review the native IR/catalog seam together before editing
publication code. No constructor, facts exporter or new shader consumer was
implemented to establish this agreement.

C/pico_unit coverage must run the actual constructor and authored preparation,
check the numeric variants and resulting float values, and reject duplicate,
missing, unknown, wrong-type, nonfinite and narrowing defaults. Cover both
signed zeros in the exact float32 roundtrip. Cover origin
roots, absent/imported modules, a changed import and failure/late publication.
Check Opaque selection/opacity lowering and rejection of other alpha modes.
The gradient must prepare as a real material, render through the existing
catalog and provide CPU source inspection from the same generation. A flat
facts probe is an earlier compiler milestone, not that acceptance.
