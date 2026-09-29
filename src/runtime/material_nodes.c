#include "graph/runtime/flow.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/materials.h"
#include "support/arena.h"

#include <math.h>
#include <string.h>

static int damage_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Damage Material requires a live world");
    }
    pin_value target, point, radius, energy, cause, source;
    int result = flow_context_eval_data(ctx, 1, &target);
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 2, &point);
    }
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 3, &radius);
    }
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 4, &energy);
    }
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 5, &cause);
    }
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 6, &source);
    }
    if (result < 0) {
        return result;
    }
    CF_V2 position;
    if (!point.ptr) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Damage Material needs a position");
    }
    memcpy(&position, point.ptr, sizeof position);
    if (!isfinite(position.x + position.y + radius.f) || radius.f < 0 ||
        !isfinite(energy.f) || energy.f < 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Damage Material needs a finite position and nonnegative radius");
    }
    /* The striking entity's light color and heat shape the impact. */
    material_impact impact = { 0 };
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    ecs_entity striker = ecs_entity_from_u64(source.u64);
    if (world && source.u64 && ecs_entity_is_alive(world, striker)) {
        const GameMaterial *light = GAME_GameMaterial_get_const(world, striker);
        const GameIgniter *igniter = GAME_GameIgniter_get_const(world, striker);
        impact.color = light ? light->emission_color : 0;
        impact.heat = igniter && isfinite(igniter->heat) ? fmaxf(igniter->heat, 0) : 0;
    }
    game_materials_damage(
        materials,
        ecs_entity_from_u64(target.u64),
        (fluid_vec){ position.x, position.y },
        radius.f,
        energy.f,
        cause.i,
        impact
    );
    return flow_context_continue(ctx, 0);
}

static pin_spec DAMAGE_IN[] = {
    { .name = "In", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Radius", .type = PIN_F32 },
    { .name = "Energy", .type = PIN_F32 },
    { .name = "Cause", .type = PIN_I32 },
    /* Optional striking entity; its emission color and igniter heat shape
     * sparks and dust. */
    { .name = "Source", .type = PIN_HANDLE, .type_name = "ecs.entity" },
};
static pin_spec DAMAGE_OUT[] = { { .name = "Out", .type = PIN_FLOW, .kind = PIN_EXEC } };
static const node_access DAMAGE_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
node_desc GAME_NODE_DamageMaterial = {
    .name = "Damage Material",
    .category = "Physics",
    .description = "Queues Energy damage that carves Entity's material within "
                   "Radius of Point and removes matter Entity owns there; "
                   "other "
                   "targets are ignored.",
    .input_count = 7,
    .inputs = DAMAGE_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = damage_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &DAMAGE_ACCESS,
};

static int release_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Release Material requires a live world");
    }
    pin_value target;
    int result = flow_context_eval_data(ctx, 1, &target);
    if (result < 0) {
        return result;
    }
    game_materials_release(materials, ecs_entity_from_u64(target.u64));
    return flow_context_continue(ctx, 0);
}

static const char *fragment_node(const eval_context *ctx, const pin_value *inputs, arena *, pin_value *out)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return "Is Material Fragment requires a live world";
    }
    out[0] = (pin_value){
        .type = PIN_BOOL,
        .b = game_materials_is_fragment(materials, ecs_entity_from_u64(inputs[0].u64))
    };
    return NULL;
}

static const char *pierceable_fragment_node(
    const eval_context *ctx,
    const pin_value *inputs,
    arena *,
    pin_value *out
)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return "Is Pierceable Material Fragment requires a live world";
    }
    out[0] = (pin_value){ .type = PIN_BOOL,
                          .b = game_materials_is_pierceable_fragment(
                              materials,
                              ecs_entity_from_u64(inputs[0].u64)
                          ) };
    return NULL;
}

static pin_spec FRAGMENT_OUT[] = { { .name = "Fragment", .type = PIN_BOOL } };
static const node_access FRAGMENT_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
node_desc GAME_NODE_IsMaterialFragment = {
    .name = "Is Material Fragment",
    .category = "Physics",
    .description = "Whether Entity is a fragment broken off destructible "
                   "material.",
    .input_count = 1,
    .inputs = DAMAGE_IN + 1,
    .output_count = 1,
    .outputs = FRAGMENT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = fragment_node,
    .access = &FRAGMENT_ACCESS,
};

node_desc GAME_NODE_IsPierceableMaterialFragment = {
    .name = "Is Pierceable Material Fragment",
    .category = "Physics",
    .description = "Whether Entity is a material fragment that continuous "
                   "sensors may pass through.",
    .input_count = 1,
    .inputs = DAMAGE_IN + 1,
    .output_count = 1,
    .outputs = FRAGMENT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = pierceable_fragment_node,
    .access = &FRAGMENT_ACCESS,
};

node_desc GAME_NODE_ReleaseMaterial = {
    .name = "Release Material",
    .category = "Physics",
    .description = "Queues Entity's remaining material to break loose as one "
                   "fragment, then destroys Entity.",
    .input_count = 2,
    .inputs = DAMAGE_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = release_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &DAMAGE_ACCESS,
};

static game_materials *node_materials(flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    return entities ? game_entities_materials(entities) : NULL;
}

static game_materials *read_materials(const eval_context *ctx)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    return entities ? game_entities_materials(entities) : NULL;
}

/* Evaluates data inputs 1..count of a flow node into values. */
static int inputs(flow_context *ctx, pin_value *values, int count)
{
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static CF_V2 vector(pin_value value)
{
    CF_V2 v = { 0 };
    if (value.ptr) {
        memcpy(&v, value.ptr, sizeof v);
    }
    return v;
}

static int emit_fluid_node(node *, flow_context *ctx)
{
    game_materials *materials = node_materials(ctx);
    pin_value in[6];
    int result = inputs(ctx, in, 6);
    if (result < 0) {
        return result;
    }
    const material_fluid *fluid = materials ? game_materials_fluid(materials) : NULL;
    if (!fluid) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Emit Fluid requires a material fluid world");
    }
    int substance = game_materials_substance(materials, in[0].str);
    CF_V2 at = vector(in[1]), velocity = vector(in[2]);
    int count = in[3].i;
    float radius = in[4].f, heat = in[5].f;
    if (substance < 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Emit Fluid names an unknown substance");
    }
    if (!isfinite(at.x + at.y + velocity.x + velocity.y + radius + heat) ||
        radius < 0 || count < 0 || count > 4096) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Emit Fluid needs finite values, a nonnegative radius and 0..4096 parcels");
    }
    /* A sunflower spiral spreads the parcels evenly over the disk. */
    for (int i = 0; i < count; i++) {
        float r = radius * sqrtf((i + .5f) / count), angle = i * 2.39996323f;
        fluid_particle p = material_fluid_particle(
            fluid,
            substance,
            (fluid_vec){ at.x + r * cosf(angle), at.y + r * sinf(angle) }
        );
        p.velocity = (fluid_vec){ velocity.x, velocity.y };
        p.temperature += heat;
        if (!game_materials_emit(materials, p)) {
            break;
        }
    }
    return flow_context_continue(ctx, 0);
}

static int heat_area_node(node *, flow_context *ctx)
{
    game_materials *materials = node_materials(ctx);
    if (!materials) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Heat Area requires a live world");
    }
    pin_value in[3];
    int result = inputs(ctx, in, 3);
    if (result < 0) {
        return result;
    }
    CF_V2 at = vector(in[0]);
    if (!isfinite(at.x + at.y + in[1].f + in[2].f) || in[1].f < 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Heat Area needs a finite point, delta and nonnegative radius");
    }
    game_materials_heat_area(
        materials,
        (fluid_vec){ at.x, at.y },
        in[1].f,
        in[2].f
    );
    return flow_context_continue(ctx, 0);
}

static const char *copy_text(arena *storage, const char *text)
{
    char *copy = arena_alloc(storage, strlen(text) + 1);
    strcpy(copy, text);
    return copy;
}

static const char *fluid_at_node(const eval_context *ctx, const pin_value *in, arena *storage, pin_value *out)
{
    game_materials *materials = read_materials(ctx);
    if (!materials) {
        return "Fluid At requires a live world";
    }
    CF_V2 at = vector(in[0]);
    if (!isfinite(at.x + at.y + in[1].f) || in[1].f < 0) {
        return "Fluid At needs a finite point and nonnegative radius";
    }
    const material_fluid *fluid = game_materials_fluid(materials);
    fluid_sample sample = fluid ? material_fluid_sample(
                                      fluid,
                                      (fluid_vec){ at.x, at.y },
                                      in[1].f
                                  )
                                : (fluid_sample){ .substance = -1 };
    const char *name = sample.substance >= 0
                         ? game_materials_substance_name(materials, sample.substance)
                         : "";
    out[0] = (pin_value){ .type = PIN_STR, .str = copy_text(storage, name) };
    out[1] = (pin_value){ .type = PIN_F32, .f = sample.density };
    out[2] = (pin_value){ .type = PIN_F32, .f = sample.temperature };
    out[3] = (pin_value){ .type = PIN_BOOL, .b = sample.conductive };
    return NULL;
}

static const char *material_at_node(const eval_context *ctx, const pin_value *in, arena *, pin_value *out)
{
    game_materials *materials = read_materials(ctx);
    if (!materials) {
        return "Material At requires a live world";
    }
    CF_V2 at = vector(in[0]);
    if (!isfinite(at.x + at.y)) {
        return "Material At needs a finite point";
    }
    material_hit hit = game_materials_at(materials, (fluid_vec){ at.x, at.y });
    out[0] = (pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(hit.entity) };
    out[1] = (pin_value){ .type = PIN_BOOL, .b = hit.solid };
    out[2] = (pin_value){ .type = PIN_F32, .f = hit.hardness };
    return NULL;
}

static pin_spec EMIT_IN[] = {
    { .name = "In", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Substance", .type = PIN_NAME },
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Velocity", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Count", .type = PIN_I32 },
    { .name = "Radius", .type = PIN_F32 },
    { .name = "Temperature", .type = PIN_F32 },
};
static pin_spec HEAT_IN[] = {
    { .name = "In", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Radius", .type = PIN_F32 },
    { .name = "Delta", .type = PIN_F32 },
};
static pin_spec FLUID_AT_IN[] = {
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Radius", .type = PIN_F32 },
};
static pin_spec FLUID_AT_OUT[] = {
    { .name = "Substance", .type = PIN_STR },
    { .name = "Density", .type = PIN_F32 },
    { .name = "Temperature", .type = PIN_F32 },
    { .name = "Conductive", .type = PIN_BOOL },
};
static pin_spec MATERIAL_AT_OUT[] = {
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Solid", .type = PIN_BOOL },
    { .name = "Hardness", .type = PIN_F32 },
};
static const node_access MATTER_WRITE = { .flags = NODE_EFFECT_STRUCTURAL };
static const node_access MATTER_READ = { .flags = NODE_EFFECT_WORLD_READ };

node_desc GAME_NODE_EmitFluid = {
    .name = "Emit Fluid",
    .category = "Physics",
    .keywords = "matter substance liquid gas spawn pour spray particles",
    .description = "Adds Count parcels of Substance spread within Radius of "
                   "Position, "
                   "moving at Velocity, with Temperature added to the "
                   "substance's own.",
    .input_count = 7,
    .inputs = EMIT_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = emit_fluid_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &MATTER_WRITE,
};

node_desc GAME_NODE_HeatArea = {
    .name = "Heat Area",
    .category = "Physics",
    .keywords = "matter temperature fire ignite incendiary cryo freeze chill "
                "melt",
    .description = "Adds Delta to the temperature of matter within Radius of "
                   "Point, "
                   "melting solids whose melting point Delta reaches.",
    .input_count = 4,
    .inputs = HEAT_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = heat_area_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &MATTER_WRITE,
};

node_desc GAME_NODE_FluidAt = {
    .name = "Fluid At",
    .category = "Physics",
    .keywords = "matter substance liquid gas query sample density temperature "
                "conductive",
    .description = "Reports the substance with the most mass within Radius of "
                   "Point "
                   "(empty when none), its mass per area, mean temperature and "
                   "whether any of it conducts.",
    .input_count = 2,
    .inputs = FLUID_AT_IN,
    .output_count = 4,
    .outputs = FLUID_AT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = fluid_at_node,
    .access = &MATTER_READ,
};

node_desc GAME_NODE_MaterialAt = {
    .name = "Material At",
    .category = "Physics",
    .keywords = "matter solid terrain rock query hardness wall",
    .description = "Reports the entity whose solid material covers Point, "
                   "whether "
                   "there is one, and its hardness.",
    .input_count = 1,
    .inputs = FLUID_AT_IN,
    .output_count = 3,
    .outputs = MATERIAL_AT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = material_at_node,
    .access = &MATTER_READ,
};

static const char *centroid_node(const eval_context *ctx, const pin_value *in, arena *storage, pin_value *out)
{
    game_materials *materials = read_materials(ctx);
    if (!materials) {
        return "Matter Centroid requires a live world";
    }
    const material_fluid *fluid = game_materials_fluid(materials);
    int count = 0;
    fluid_vec center = fluid && in[0].u64
                         ? material_fluid_centroid(fluid, in[0].u64, &count)
                         : (fluid_vec){ 0 };
    CF_V2 *position = arena_alloc(storage, sizeof *position);
    *position = (CF_V2){ center.x, center.y };
    out[0] = (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = position };
    out[1] = (pin_value){ .type = PIN_I32, .i = count };
    return NULL;
}

static int push_matter_node(node *, flow_context *ctx)
{
    game_materials *materials = node_materials(ctx);
    pin_value in[2];
    int result = inputs(ctx, in, 2);
    if (result < 0) {
        return result;
    }
    material_fluid *fluid = materials ? game_materials_fluid_mut(materials) : NULL;
    CF_V2 force = vector(in[1]);
    if (!fluid) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Push Matter requires a material fluid world");
    }
    if (!isfinite(force.x + force.y)) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Push Matter needs a finite force");
    }
    if (in[0].u64) {
        material_fluid_push_owner(fluid, in[0].u64, (fluid_vec){ force.x, force.y });
    }
    return flow_context_continue(ctx, 0);
}

static pin_spec OWNER_IN[] = {
    { .name = "In", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Owner", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Force", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static pin_spec CENTROID_OUT[] = {
    { .name = "Position", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Count", .type = PIN_I32 },
};

node_desc GAME_NODE_MatterCentroid = {
    .name = "Matter Centroid",
    .category = "Physics",
    .keywords = "matter jelly owner body center position particles",
    .description = "Reports the mean position of the matter Owner's fluid "
                   "volume "
                   "created and how many parcels remain.",
    .input_count = 1,
    .inputs = OWNER_IN + 1,
    .output_count = 2,
    .outputs = CENTROID_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = centroid_node,
    .access = &MATTER_READ,
};

node_desc GAME_NODE_PushMatter = {
    .name = "Push Matter",
    .category = "Physics",
    .keywords = "matter jelly owner force move steer impulse",
    .description = "Applies Force to Owner's matter during the next simulation "
                   "step, shared by its mass.",
    .input_count = 3,
    .inputs = OWNER_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = push_matter_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &MATTER_WRITE,
};
