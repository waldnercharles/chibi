#include "graph/nodes/set_map_nodes.h"

#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

#define SET_INPUT                                                              \
    { "Set",                                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_DATA,                                                                \
      0,                                                                       \
      PIN_CONTAINER_SET,                                                       \
      { .type = PIN_FLOW, .container = PIN_CONTAINER_SET },                    \
      0,                                                                       \
      PIN_FLAG_WILDCARD,                                                       \
      1 }
#define SET_ITEM                                                               \
    { "Item", PIN_FLOW, NULL, PIN_DATA, 0, PIN_CONTAINER_SINGLE, { .type = PIN_FLOW }, 0, PIN_FLAG_WILDCARD, 1 }
#define SET_OUTPUT                                                             \
    { "Set",                                                                   \
      PIN_FLOW,                                                                \
      NULL,                                                                    \
      PIN_DATA,                                                                \
      0,                                                                       \
      PIN_CONTAINER_SET,                                                       \
      { .type = PIN_FLOW, .container = PIN_CONTAINER_SET },                    \
      0,                                                                       \
      PIN_FLAG_WILDCARD,                                                       \
      1 }
static pin_spec set_length_output = { "Length", PIN_I32, "int", PIN_DATA };
static pin_spec set_bool_output = { "Return Value", PIN_BOOL, "bool", PIN_DATA };

static const pin_array *set_value(const pin_value *v)
{
    return v && v->container == PIN_CONTAINER_SET ? v->ptr : NULL;
}

static pin_value set_make(const pin_value *inputs, int count, arena *a)
{
    pin_array source = {
        .element_type = count ? inputs[0].type : PIN_FLOW,
        .type_name = count ? inputs[0].type_name : NULL,
        .count = count,
        .capacity = count,
        .items = (pin_value *)inputs,
    };
    pin_array *copy = pin_array_clone_to_arena(a, &source);
    return (pin_value){ .type = source.element_type,
                        .type_name = source.type_name,
                        .container = PIN_CONTAINER_SET,
                        .ptr = copy };
}

static pin_value set_contains(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *set = set_value(&inputs[0]);
    if (!set) {
        return (pin_value){ .type = PIN_BOOL };
    }
    for (int i = 0; i < set->count; i++) {
        if (pin_value_equal(&set->items[i], &inputs[1])) {
            return (pin_value){ .type = PIN_BOOL, .b = true };
        }
    }
    return (pin_value){ .type = PIN_BOOL, .b = false };
}

static pin_value set_length(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *set = set_value(&inputs[0]);
    return (pin_value){ .type = PIN_I32, .i = set ? set->count : 0 };
}

static pin_value set_add(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *set = set_value(&inputs[0]);
    if (!set) {
        return (pin_value){ .type = PIN_FLOW, .container = PIN_CONTAINER_SET };
    }
    pin_array *copy = pin_array_clone_to_arena(a, set);
    int exists = 0;
    for (int i = 0; i < copy->count; i++) {
        exists |= pin_value_equal(&copy->items[i], &inputs[1]);
    }
    if (!exists && copy->count < copy->capacity) {
        copy->items[copy->count++] = inputs[1];
    }
    return (pin_value){ .type = copy->element_type,
                        .type_name = copy->type_name,
                        .container = PIN_CONTAINER_SET,
                        .ptr = copy };
}

static pin_spec set_make_inputs[] = { SET_ITEM };
static pin_spec set_make_outputs[] = { SET_OUTPUT };
static pin_spec set_only_inputs[] = { SET_INPUT };
static pin_spec set_item_inputs[] = { SET_INPUT, SET_ITEM };

node_desc NODE_MakeSet = {
    .name = "Make Set",
    .input_count = 1,
    .output_count = 1,
    .inputs = set_make_inputs,
    .outputs = set_make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_make,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set make collection",
};
node_desc NODE_SetAdd = {
    .name = "Set Add",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_item_inputs,
    .outputs = set_make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_add,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set add unique",
};
node_desc NODE_SetContains = {
    .name = "Set Contains",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_item_inputs,
    .outputs = &set_bool_output,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_contains,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set contains equality",
};
node_desc NODE_SetLength = {
    .name = "Set Length",
    .input_count = 1,
    .output_count = 1,
    .inputs = set_only_inputs,
    .outputs = &set_length_output,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_length,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set length",
};

node_desc *const SET_MAP_NODES[] = { &NODE_MakeSet,
                                     &NODE_SetAdd,
                                     &NODE_SetContains,
                                     &NODE_SetLength };
const int SET_MAP_NODE_COUNT = sizeof SET_MAP_NODES / sizeof SET_MAP_NODES[0];

const registry_pack CORE_SET_MAP_PACK = {
    .name = "core.set_map",
    .nodes = SET_MAP_NODES,
    .node_count = sizeof SET_MAP_NODES / sizeof SET_MAP_NODES[0],
};
