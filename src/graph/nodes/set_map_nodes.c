#include "graph/nodes/set_map_nodes.h"

#include "graph/model/registry.h"
#include "graph/nodes/array_nodes.h"
#include "graph/runtime/value_array.h"
#include "support/arena.h"

#include <string.h>

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

static pin_value set_result(arena *a, const pin_array *source)
{
    pin_array *copy = pin_array_snapshot_to_arena(a, source);
    if (!copy) {
        return (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_SET };
    }
    pin_array_index(a, copy);
    return (pin_value){ .type = source->element_type,
                        .type_name = copy->type_name,
                        .container = PIN_CONTAINER_SET,
                        .ptr = copy };
}
static pin_value set_make(const pin_value *inputs, int count, arena *a)
{
    pin_value *unique = arena_alloc(a, (size_t)count * sizeof *unique);
    int length = pin_set_unique(a, unique, inputs, count);
    pin_array source = {
        .element_type = count ? inputs[0].type : PIN_FLOW,
        .type_name = count ? inputs[0].type_name : NULL,
        .count = length,
        .capacity = length,
        .items = unique,
    };
    return set_result(a, &source);
}

static pin_value set_contains(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    (void)a;
    const pin_array *set = set_value(&inputs[0]);
    return (pin_value){ .type = PIN_BOOL, .b = pin_array_find(set, &inputs[1]) >= 0 };
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
    if (set_contains(inputs, count, a).b) {
        return set_result(a, set);
    }
    int length = set ? set->count : 0;
    pin_value *items = arena_alloc(a, ((size_t)length + 1) * sizeof *items);
    if (length) {
        memcpy(items, set->items, (size_t)length * sizeof *items);
    }
    items[length] = inputs[1];
    pin_array source = { .element_type = set ? set->element_type : inputs[1].type,
                         .type_name = set ? set->type_name : inputs[1].type_name,
                         .count = length + 1,
                         .capacity = length + 1,
                         .items = items };
    return set_result(a, &source);
}

static pin_value set_remove(const pin_value *inputs, int count, arena *a)
{
    (void)count;
    const pin_array *set = set_value(&inputs[0]);
    int removed = pin_array_find(set, &inputs[1]);
    int length = set ? set->count : 0;
    int result_count = length - (removed >= 0);
    pin_value *items = arena_alloc(a, (size_t)result_count * sizeof *items);
    if (result_count && !items) {
        return (pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_SET };
    }
    if (removed < 0 && length) {
        memcpy(items, set->items, (size_t)length * sizeof *items);
    } else if (removed >= 0) {
        if (removed) {
            memcpy(items, set->items, (size_t)removed * sizeof *items);
        }
        if (removed + 1 < length) {
            memcpy(
                items + removed,
                set->items + removed + 1,
                (size_t)(length - removed - 1) * sizeof *items
            );
        }
    }
    pin_array source = {
        .element_type = set ? set->element_type : inputs[0].type,
        .type_name = set ? set->type_name : inputs[0].type_name,
        .count = result_count,
        .capacity = result_count,
        .items = items,
    };
    return set_result(a, &source);
}

#define VALUE_CALL(operation)                                                                                  \
    static const char *operation##_call(arena *storage, const pin_value *inputs, int count, pin_value *result) \
    {                                                                                                          \
        *result = operation(inputs, count, storage);                                                           \
        if (result->type == PIN_FLOW)                                                                          \
            return "data function produced an invalid result";                                                 \
        if (result->type == PIN_STRUCT && !result->ptr)                                                        \
            return "arena allocation failed";                                                                  \
        return NULL;                                                                                           \
    }
VALUE_CALL(set_make)
VALUE_CALL(set_add)
VALUE_CALL(set_remove)
VALUE_CALL(set_contains)
#undef VALUE_CALL
static const node_access set_value_access = { .flags = NODE_EFFECT_PURE };

static pin_spec set_make_inputs[] = { SET_ITEM };
static pin_spec set_make_outputs[] = { SET_OUTPUT };
static pin_spec set_only_inputs[] = { SET_INPUT };
static pin_spec set_item_inputs[] = { SET_INPUT, SET_ITEM };

node_desc NODE_MakeSet = {
    .work = NODE_WORK_SET,
    .name = "Make Set",
    .input_count = 1,
    .output_count = 1,
    .inputs = set_make_inputs,
    .outputs = set_make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_make,
    .host_call = &(const node_host_call){ .value = set_make_call, .variadic = true },
    .access = &set_value_access,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set make collection",
    .description = "Builds a set from the Item inputs, dropping duplicates.",
};
node_desc NODE_SetAdd = {
    .work = NODE_WORK_SET_COPY,
    .name = "Set Add",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_item_inputs,
    .outputs = set_make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_add,
    .host_call = &(const node_host_call){ .value = set_add_call },
    .access = &set_value_access,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set add unique",
    .description = "Returns a copy of Set with Item added, unchanged if an "
                   "equal item is already present.",
};
node_desc NODE_SetRemove = {
    .work = NODE_WORK_SET_COPY,
    .name = "Set Remove",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_item_inputs,
    .outputs = set_make_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_remove,
    .host_call = &(const node_host_call){ .value = set_remove_call },
    .access = &set_value_access,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set remove delete item",
    .description = "Returns a copy of Set with Item removed, unchanged if the "
                   "item is absent.",
};
node_desc NODE_SetContains = {
    .work = NODE_WORK_SET,
    .name = "Set Contains",
    .input_count = 2,
    .output_count = 1,
    .inputs = set_item_inputs,
    .outputs = &set_bool_output,
    .pure_kind = NODE_PURE_CALL,
    .fn = set_contains,
    .host_call = &(const node_host_call){ .value = set_contains_call },
    .access = &set_value_access,
    .lower = behavior_lower_array_search,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Set",
    .keywords = "set contains equality",
    .description = "Whether Set holds an item equal to Item.",
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
    .description = "Returns the number of items in Set.",
};

static node_desc *const SET_MAP_NODES[] = { &NODE_MakeSet,
                                            &NODE_SetAdd,
                                            &NODE_SetRemove,
                                            &NODE_SetContains,
                                            &NODE_SetLength };

const registry_pack CORE_SET_MAP_PACK = {
    .name = "core.set_map",
    .nodes = SET_MAP_NODES,
    .node_count = sizeof SET_MAP_NODES / sizeof SET_MAP_NODES[0],
};
