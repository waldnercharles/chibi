#include "runtime/behaviors/tilemap_definition.h"

#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/tilemap.h"
#include "runtime/transform_propagation.h"

#include <assert.h>
#include <math.h>

static const char *query(void *service, const pin_value *inputs, pin_value *out, bool box)
{
    entities *entities = service;
    ecs_entity entity = ecs_entity_from_u64(inputs[0].u64);
    const char *error = NULL;
    const tilemap_grid *grid = entities_tilemap(entities, entity, &error);
    if (!grid) {
        return error;
    }
    ecs_world *world = entities_world(entities);
    const Transform *position = Transform_get_const(world, entity);
    if (!position) {
        return "Tilemap query requires a Transform";
    }
    GlobalTransform2D transform = transform_world_2d(world, entity);
    float radians = transform.radians;
    if (!isfinite(transform.position.x) || !isfinite(transform.position.y) ||
        !isfinite(radians)) {
        return "Tilemap query requires a finite Transform and Orientation";
    }
    CF_V2 origin = transform.position;
    assert(inputs[1].ptr);
    CF_V2 point = *(const CF_V2 *)inputs[1].ptr;
    int value;
    if (box) {
        assert(inputs[2].ptr);
        CF_V2 maximum = *(const CF_V2 *)inputs[2].ptr;
        value = tilemap_grid_overlap_count(grid, origin, radians, (CF_Aabb){ point, maximum });
    } else {
        value = tilemap_grid_point(grid, origin, radians, point);
    }
    *out = (pin_value){ .type = PIN_I32, .i = value };
    return NULL;
}

static const char *frame_at(void *service, const pin_value *inputs, pin_value *out)
{
    return query(service, inputs, out, false);
}

static const char *occupied_in_box(void *service, const pin_value *inputs, pin_value *out)
{
    return query(service, inputs, out, true);
}

static const native_parameter_definition FRAME_IN[] = { TILEMAP_FRAME_INPUTS(NATIVE_PORT) };
static const native_parameter_definition BOX_IN[] = { TILEMAP_BOX_INPUTS(NATIVE_PORT) };
static const native_parameter_definition FRAME_OUT[] = {
    TILEMAP_OUTPUT(NATIVE_PORT, "Frame")
};
static const native_parameter_definition BOX_OUT[] = { TILEMAP_OUTPUT(NATIVE_PORT, "Count") };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = TILEMAP_FRAME_NAME,
      .source_name = "tilemap_frame_at",
      .category = "Tilemap",
      .description = "Returns the frame at a world point, or -1 for an empty "
                     "or outside cell.",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ,
      .inputs = FRAME_IN,
      .input_count = NATIVE_COUNT(FRAME_IN),
      .outputs = FRAME_OUT,
      .output_count = NATIVE_COUNT(FRAME_OUT) },
    { .canonical_name = TILEMAP_BOX_NAME,
      .source_name = "tilemap_occupied_in_box",
      .category = "Tilemap",
      .description = "Counts occupied cells with positive-area overlap with a "
                     "world rectangle.",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .completion_output = -1,
      .effects = SCRIPT_EFFECT_READ,
      .inputs = BOX_IN,
      .input_count = NATIVE_COUNT(BOX_IN),
      .outputs = BOX_OUT,
      .output_count = NATIVE_COUNT(BOX_OUT) },
};
static const node_access ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const node_host_call FRAME_CALL = { .service = "game.entities",
                                           .invoke = frame_at };
static const node_host_call BOX_CALL = { .service = "game.entities",
                                         .invoke = occupied_in_box };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = TILEMAP_FRAME_NAME,
      .implementation = { .call = &FRAME_CALL, .access = &ACCESS } },
    { .canonical_name = TILEMAP_BOX_NAME,
      .implementation = { .call = &BOX_CALL, .access = &ACCESS } },
};
const native_definition_pack TILEMAP_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
