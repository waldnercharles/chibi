#include "graph/runtime/flow.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/materials.h"

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
                   "Radius of Point; non-material targets are ignored.",
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

static const char missing_world[] = "Sampling terrain requires a game world";

static game_materials *materials_of(void *service)
{
    return service ? game_entities_materials(service) : NULL;
}

static const char *sample_field_call(void *service, const pin_value *in, pin_value *out)
{
    game_materials *materials = materials_of(service);
    if (!materials) {
        return missing_world;
    }
    float value;
    char error[128];
    if (!material_field_sample(
            game_materials_fields(materials),
            in[0].str,
            in[1].str,
            (unsigned)in[4].i,
            in[2].f,
            in[3].f,
            &value,
            error,
            sizeof error
        )) {
        return "Sample Field needs a compiled field, a parameter object of its "
               "parameters, and a point where it is finite";
    }
    out[0].f = value;
    return NULL;
}
static const char *sample_field_read(const eval_context *ctx, const pin_value *in, arena *, pin_value *out)
{
    out[0] = (pin_value){ .type = PIN_F32, .type_name = "float" };
    return sample_field_call(eval_context_service(ctx, "game.entities"), in, out);
}

static const char *terrain_distance_call(void *service, const pin_value *in, pin_value *out)
{
    game_materials *materials = materials_of(service);
    if (!materials) {
        return missing_world;
    }
    if (!in[1].ptr) {
        return "Terrain Distance needs a point";
    }
    const CF_V2 *point = in[1].ptr;
    game_terrain_sample sample;
    const char *error = game_materials_terrain_at(
        materials,
        ecs_entity_from_u64(in[0].u64),
        (fluid_vec){ point->x, point->y },
        &sample
    );
    if (error) {
        return error;
    }
    out[0].f = sample.distance;
    out[1].b = sample.solid;
    return NULL;
}
static const char *terrain_distance_read(
    const eval_context *ctx,
    const pin_value *in,
    arena *,
    pin_value *out
)
{
    out[0] = (pin_value){ .type = PIN_F32, .type_name = "float" };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool" };
    return terrain_distance_call(eval_context_service(ctx, "game.entities"), in, out);
}

static const node_access SAMPLE_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static pin_spec SAMPLE_FIELD_IN[] = {
    { .name = "Field", .type = PIN_NAME, .type_name = "name" },
    { .name = "Parameters", .type = PIN_STR, .type_name = "const char *" },
    { .name = "X", .type = PIN_F32, .type_name = "float" },
    { .name = "Y", .type = PIN_F32, .type_name = "float" },
    { .name = "Seed", .type = PIN_I32, .type_name = "int" },
};
static pin_spec VALUE_OUT[] = { { .name = "Value", .type = PIN_F32, .type_name = "float" } };
node_desc GAME_NODE_SampleField = {
    .name = "Sample Field",
    .category = "Terrain",
    .keywords = "field procedural sample evaluate distance terrain noise seed",
    .description = "Evaluates procedural Field at (X, Y) with Seed and the "
                   "Parameters JSON object's overrides, as a terrain bakes it.",
    .input_count = 5,
    .inputs = SAMPLE_FIELD_IN,
    .output_count = 1,
    .outputs = VALUE_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = sample_field_read,
    .host_call = &(const node_host_call){ .service = "game.entities",
                                          .invoke = sample_field_call,
                                          .missing_service = missing_world },
    .access = &SAMPLE_ACCESS,
};

static pin_spec TERRAIN_DISTANCE_IN[] = {
    { .name = "Terrain", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static pin_spec TERRAIN_DISTANCE_OUT[] = {
    { .name = "Distance", .type = PIN_F32, .type_name = "float" },
    { .name = "Solid", .type = PIN_BOOL, .type_name = "bool" },
};
node_desc GAME_NODE_TerrainDistance = {
    .name = "Terrain Distance",
    .category = "Terrain",
    .keywords = "terrain rock distance solid inside ground wall sdf probe",
    .description = "Returns the signed distance from Point to Terrain's rock, "
                   "negative inside, from its current cells where loaded and "
                   "its field elsewhere.",
    .input_count = 2,
    .inputs = TERRAIN_DISTANCE_IN,
    .output_count = 2,
    .outputs = TERRAIN_DISTANCE_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = terrain_distance_read,
    .host_call = &(const node_host_call){ .service = "game.entities",
                                          .invoke = terrain_distance_call,
                                          .missing_service = missing_world },
    .access = &SAMPLE_ACCESS,
};
