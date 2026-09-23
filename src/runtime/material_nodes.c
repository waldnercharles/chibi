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
    pin_value target, point, radius, energy, cause;
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
    game_materials_damage(
        materials,
        ecs_entity_from_u64(target.u64),
        (fluid_vec){ position.x, position.y },
        radius.f,
        energy.f,
        cause.i
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
};
static pin_spec DAMAGE_OUT[] = { { .name = "Out", .type = PIN_FLOW, .kind = PIN_EXEC } };
static const node_access DAMAGE_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
node_desc GAME_NODE_DamageMaterial = {
    .name = "Damage Material",
    .category = "Physics",
    .input_count = 6,
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
    .input_count = 2,
    .inputs = DAMAGE_IN,
    .output_count = 1,
    .outputs = DAMAGE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = release_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &DAMAGE_ACCESS,
};
