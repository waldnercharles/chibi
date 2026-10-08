#include "execution/intrinsics.h"

#include "ecs/execution.h"
#include "execution/function.h"
#include "execution/scalar.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/trace.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Managed values escape host-site scratch only through invocation-owned snapshots.
 * These primitives use the same arena/value-copy ABI as compiled native calls. */
static const char *snapshot_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    return pin_value_snapshot_to_arena(storage, out, args)
             ? "Cannot retain language value"
             : NULL;
}
static const char *construct_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 1);
    *out = args[0];
    if (out->container != PIN_CONTAINER_SINGLE) {
        pin_array *array = pin_array_snapshot_allocate(storage, out->type, out->type_name, count - 1);
        for (int i = 1; i < count; i++) {
            if (pin_value_snapshot_to_arena(storage, &array->items[i - 1], &args[i])) {
                return "Cannot retain array element";
            }
        }
        out->ptr = array;
        return NULL;
    }
    const type_desc *type = type_catalog_find(out->type_name);
    if (!type || out->type != PIN_STRUCT) {
        return "Unknown language constructor layout";
    }
    if (args[0].ptr) {
        if (pin_value_snapshot_to_arena(storage, out, &args[0])) {
            return "Cannot retain struct base";
        }
    } else {
        out->ptr = arena_alloc(storage, type->size);
        memset(out->ptr, 0, type->size);
        pin_arena_trace_struct(storage, out->ptr, type);
    }
    for (int i = 1; i + 1 < count; i += 2) {
        size_t offset = (size_t)args[i].u64;
        pin_value value;
        if (pin_value_snapshot_to_arena(storage, &value, &args[i + 1])) {
            return "Cannot retain struct field";
        }
        size_t size = value.container != PIN_CONTAINER_SINGLE ? sizeof(void *)
                    : value.type == PIN_STRUCT
                        ? type_catalog_find(value.type_name)->size
                        : behavior_scalar_size(value.type);
        if (offset > type->size || size > type->size - offset) {
            return "Struct field exceeds declared layout";
        }
        if (value.type == PIN_STRUCT &&
            value.container == PIN_CONTAINER_SINGLE && !value.ptr) {
            return "Struct field has no payload";
        }
        const void *data = value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE
                             ? value.ptr
                             : (const void *)&value.d;
        memcpy((char *)out->ptr + offset, data, size);
    }
    return NULL;
}
const node_access EXECUTION_assignment_access = { .flags = NODE_EFFECT_HOST_SINK };
const node_host_call EXECUTION_snapshot_call = { .value = snapshot_value };
const node_host_call EXECUTION_construct_call = { .value = construct_value };
const node_access EXECUTION_value_access = { .flags = NODE_EFFECT_PURE };
typedef struct language_reference_path
{
    const struct language_reference_path *parent;
    pin_value type;
    size_t offset;
    int index;
    bool element;
} language_reference_path;
typedef struct
{
    language_reference_kind kind;
    behavior_function_context *owner;
    pin_value *value;
    pin_value root, type;
    uint64_t entity, component, layout;
    size_t stride;
    const language_reference_path *path;
    int variable;
    execution_box *box;
} language_reference;
static void reference_trace(const void *value, const void *shape, arena_marker mark, void *user)
{
    (void)shape;
    const language_reference *reference = value;
    pin_value_trace(&reference->root, mark, user);
    pin_value_trace(&reference->type, mark, user);
    mark(user, reference->path);
    if (reference->kind == LANGUAGE_REFERENCE_VALUE && !reference->box) {
        mark(user, reference->value);
        pin_value_trace(reference->value, mark, user);
    }
}
static void reference_path_trace(const void *value, const void *shape, arena_marker mark, void *user)
{
    (void)shape;
    const language_reference_path *path = value;
    mark(user, path->parent);
    pin_value_trace(&path->type, mark, user);
}
static behavior_function_context *value_context(pin_value value)
{
    return (behavior_function_context *)(uintptr_t)behavior_scalar_bits(value);
}
static const char *make_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 9);
    (void)count;
    if (args[2].i < LANGUAGE_REFERENCE_VALUE || args[2].i > LANGUAGE_REFERENCE_COMPONENT) {
        return "Invalid reference storage kind";
    }
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = (language_reference){ .owner = value_context(args[0]),
                                 .type = args[1],
                                 .root = args[1],
                                 .kind = args[2].i,
                                 .variable = args[3].i,
                                 .value = (pin_value *)(uintptr_t)args[4].u64,
                                 .entity = args[5].u64,
                                 .component = args[6].u64,
                                 .layout = args[7].u64,
                                 .stride = args[8].u64 };
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
pin_value execution_reference_value(
    arena *storage,
    behavior_function_context *owner,
    pin_value *value,
    pin_value type
)
{
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = (language_reference){ .kind = LANGUAGE_REFERENCE_VALUE,
                                 .owner = owner,
                                 .value = value,
                                 .root = type,
                                 .type = type,
                                 .variable = -1 };
    arena_set_trace(storage, ref, reference_trace, NULL);
    return (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
}
static const language_reference_path *capture_path(arena *storage, const language_reference_path *path)
{
    if (!path) {
        return NULL;
    }
    language_reference_path *copy = arena_alloc(storage, sizeof *copy);
    *copy = *path;
    copy->parent = capture_path(storage, path->parent);
    arena_set_trace(storage, copy, reference_path_trace, NULL);
    return copy;
}
bool execution_reference_local(
    pin_value value,
    behavior_function_context **owner,
    pin_value **slot,
    execution_box **box
)
{
    const language_reference *ref = (const language_reference *)(uintptr_t)value.u64;
    *owner = ref->owner;
    *slot = ref->value;
    *box = ref->box;
    return ref->kind == LANGUAGE_REFERENCE_VALUE;
}
void execution_reference_capture(
    arena *storage,
    behavior_function_context *owner,
    pin_value *value,
    execution_box *box
)
{
    const language_reference *base = (const language_reference *)(uintptr_t)
                                         value->u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = *base;
    ref->owner = owner;
    ref->path = capture_path(storage, base->path);
    if (box) {
        ref->box = box;
        ref->value = NULL;
    }
    arena_set_trace(storage, ref, reference_trace, NULL);
    value->u64 = (uintptr_t)ref;
}
/* A boxed reference reaches the box's current frame and slot. */
static language_reference *unbox(language_reference *ref, language_reference *view)
{
    if (!ref->box) {
        return ref;
    }
    *view = *ref;
    view->owner = ref->box->owner;
    view->value = ref->box->value;
    return view;
}
static const char *field_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 4);
    (void)count;
    language_reference *base = (language_reference *)(uintptr_t)args[0].u64;
    language_reference *ref = arena_alloc(storage, sizeof *ref);
    *ref = *base;
    ref->type = args[1];
    language_reference_path *path = arena_alloc(storage, sizeof *path);
    *path = (language_reference_path){ .parent = base->path,
                                       .type = args[1],
                                       .offset = args[2].u64,
                                       .index = args[2].i,
                                       .element = args[3].b };
    ref->path = path;
    arena_set_trace(storage, path, reference_path_trace, NULL);
    arena_set_trace(storage, ref, reference_trace, NULL);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)ref };
    return NULL;
}
static pin_value reference_root(language_reference *ref)
{
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        pin_value root = ref->root;
        root.ptr = NULL;
        const ecs_execution_view *world = ref->owner->world;
        if (!world) {
            return root;
        }
        uint32_t row = ecs_execution_entity(world, ref->entity);
        const ecs_execution_column *column = ecs_execution_component(world, row, ref->component);
        if (column && column->layout == ref->layout && column->stride == ref->stride) {
            root.ptr = (char *)column->data + (size_t)row * column->stride;
        }
        return root;
    }
    return ref->kind == LANGUAGE_REFERENCE_STATE
             ? ref->owner->variables[ref->variable]
             : *ref->value;
}
static void *reference_path_data(const language_reference_path *path, pin_value *type, void *data)
{
    if (!path || !data) {
        return data;
    }
    data = reference_path_data(path->parent, type, data);
    if (!data) {
        return NULL;
    }
    if (path->element) {
        if (type->container != PIN_CONTAINER_ARRAY &&
            type->container != PIN_CONTAINER_MAP) {
            return NULL;
        }
        pin_array *array;
        memcpy(&array, data, sizeof array);
        if (!array || path->index < 0 || path->index >= array->count) {
            return NULL;
        }
        pin_value *item = &array->items[path->index];
        *type = *item;
        return item->type == PIN_STRUCT && item->container == PIN_CONTAINER_SINGLE
                 ? item->ptr
                 : &item->d;
    }
    const type_desc *layout = type->type == PIN_STRUCT && type->container == PIN_CONTAINER_SINGLE
                                ? type_catalog_find(type->type_name)
                                : NULL;
    if (!layout || path->offset >= layout->size) {
        return NULL;
    }
    *type = path->type;
    return (char *)data + path->offset;
}
static void *reference_data(language_reference *ref, pin_value *root)
{
    *root = reference_root(ref);
    pin_value type = *root;
    void *data = root->type == PIN_STRUCT && root->container == PIN_CONTAINER_SINGLE
                   ? root->ptr
                   : &root->d;
    return reference_path_data(ref->path, &type, data);
}
static pin_value *reference_item(const language_reference_path *path, pin_value *root)
{
    assert(path && path->element);
    pin_value type = *root;
    void *data = root->type == PIN_STRUCT && root->container == PIN_CONTAINER_SINGLE
                   ? root->ptr
                   : &root->d;
    data = reference_path_data(path->parent, &type, data);
    if (!data || (type.container != PIN_CONTAINER_ARRAY &&
                  type.container != PIN_CONTAINER_MAP)) {
        return NULL;
    }
    pin_array *array;
    memcpy(&array, data, sizeof array);
    return array && path->index >= 0 && path->index < array->count
             ? &array->items[path->index]
             : NULL;
}
size_t execution_value_size(pin_value type)
{
    if (type.container != PIN_CONTAINER_SINGLE) {
        return sizeof(void *);
    }
    if (type.type == PIN_STRUCT) {
        const type_desc *layout = type_catalog_find(type.type_name);
        return layout ? layout->size : 0;
    }
    return behavior_scalar_size(type.type);
}
static const char *read_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    language_reference view,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    if (ref->kind == LANGUAGE_REFERENCE_VALUE && ref->owner->initialize_local &&
        ref->owner->initialize_local(ref->owner, ref->value, false) < 0) {
        return "Cannot initialize referenced local";
    }
    pin_value root;
    void *data = reference_data(ref, &root);
    if (ref->path && ref->path->element) {
        const pin_value *item = reference_item(ref->path, &root);
        return !item || pin_value_snapshot_to_arena(storage, out, item)
                 ? "Referenced array element is unavailable"
                 : NULL;
    }
    size_t size = execution_value_size(ref->type);
    if ((!data && (ref->path || ref->kind == LANGUAGE_REFERENCE_COMPONENT)) || !size) {
        return "Referenced storage is unavailable";
    }
    *out = ref->type;
    if (out->type == PIN_STRUCT && out->container == PIN_CONTAINER_SINGLE) {
        out->ptr = data;
    } else {
        memcpy(&out->d, data, size);
    }
    pin_value retained;
    if (pin_value_snapshot_to_arena(storage, &retained, out)) {
        return "Cannot retain referenced value";
    }
    *out = retained;
    return NULL;
}
static const char *write_reference_value(arena *owner_storage, language_reference *ref, const pin_value *args)
{
    if (ref->path && ref->kind == LANGUAGE_REFERENCE_VALUE &&
        ref->owner->initialize_local &&
        ref->owner->initialize_local(ref->owner, ref->value, false) < 0) {
        return "Cannot initialize referenced local";
    }
    pin_value root;
    void *current = reference_data(ref, &root);
    bool element = ref->path && ref->path->element;
    if (!current && (element ? !reference_item(ref->path, &root)
                             : ref->path || ref->kind == LANGUAGE_REFERENCE_COMPONENT)) {
        return "Referenced storage is unavailable";
    }
    if (!owner_storage) {
        return "Referenced owner has no retained value storage";
    }
    pin_value value, retained;
    if (pin_value_snapshot_to_arena(owner_storage, &value, &args[1]) ||
        pin_value_snapshot_to_arena(owner_storage, &retained, &root)) {
        return "Cannot retain referenced value";
    }
    if (!ref->path) {
        retained = value;
    } else if (element) {
        pin_value *item = reference_item(ref->path, &retained);
        if (!item) {
            return "Referenced array element is unavailable";
        }
        *item = value;
    } else {
        pin_value type = retained;
        void *data = retained.type == PIN_STRUCT && retained.container == PIN_CONTAINER_SINGLE
                       ? retained.ptr
                       : &retained.d;
        void *target = reference_path_data(ref->path, &type, data);
        size_t size = execution_value_size(ref->type);
        if (!target || !size) {
            return "Referenced path is unavailable";
        }
        const void *source = value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE
                               ? value.ptr
                               : &value.d;
        memcpy(target, source, size);
    }
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT) {
        if (!ref->owner->store_component ||
            ref->owner->store_component(ref->owner, ref->entity, ref->component, ref->layout, 0, root, retained)) {
            return "Cannot retain referenced component write";
        }
    } else if (ref->kind == LANGUAGE_REFERENCE_STATE) {
        if (!ref->owner->store_value) {
            return "Referenced state owner has no persistent storage";
        }
        ref->owner->store_value(ref->owner->value_owner, ref->variable, &retained);
    } else {
        *ref->value = retained;
        if (ref->owner->initialize_local &&
            ref->owner->initialize_local(ref->owner, ref->value, true) < 0) {
            return "Cannot retain local initialization state";
        }
    }
    return NULL;
}
static const char *write_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    language_reference view,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    const char *error;
    if (ref->kind == LANGUAGE_REFERENCE_VALUE) {
        error = write_reference_value(ref->owner->value_storage, ref, args);
    } else {
        /* Scalars never allocate, so the arena gets a block only on demand. */
        arena temporary = { 0 };
        error = write_reference_value(&temporary, ref, args);
        arena_free(&temporary);
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = !error };
    return error;
}
/* A declaration starts a new local, so tasks keep the old one in its box. */
static const char *release_local(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 1);
    (void)count;
    const language_reference *ref = (const language_reference *)(uintptr_t)args[0]
                                        .u64;
    ref->owner->release_local(ref->owner, ref->value);
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
const node_host_call EXECUTION_reference_make_call = { .value = make_reference };
const node_host_call EXECUTION_local_release_call = { .value = release_local };
const node_host_call EXECUTION_reference_field_call = { .value = field_reference };
const node_host_call EXECUTION_reference_read_call = { .value = read_reference };
const node_host_call EXECUTION_reference_write_call = { .value = write_reference };
static const char *read_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    const pin_value
        *value = context->action_result
                   ? context->action_result(context, args[1].u64, args[2].i)
                   : NULL;
    return !value || pin_value_snapshot_to_arena(storage, out, value)
             ? "Cannot read retained action result"
             : NULL;
}
static const char *complete_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 2);
    behavior_function_context *context = value_context(args[0]);
    int pending = context->pending_commands;
    if (!context->complete_action ||
        context->complete_action(context, args[1].u64, args + 2, count - 2) < 0) {
        return "Cannot dispatch action completion";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = context->pending_commands > pending };
    return NULL;
}
const node_host_call EXECUTION_action_read_call = { .value = read_action_value };
const node_host_call EXECUTION_action_complete_call = { .value = complete_action_value };

static const char *component_has_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world) {
        return "Component lookup requires an ECS world";
    }
    uint32_t row = ecs_execution_entity(context->world, args[1].u64);
    *out = (pin_value){
        .type = PIN_BOOL,
        .b = ecs_execution_component(context->world, row, args[2].u64) != NULL
    };
    return NULL;
}
static const char *component_membership_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 4);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool changed = false;
    if (!context->component_membership ||
        context->component_membership(context, args[1].u64, args[2].u64, args[3].b, &changed) <
            0) {
        return "Cannot change component membership";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = changed };
    return NULL;
}
const node_host_call EXECUTION_component_has_call = { .value = component_has_value };
const node_host_call EXECUTION_component_membership_call = { .value = component_membership_value };
const node_access EXECUTION_component_read_access = { .flags = NODE_EFFECT_WORLD_READ };
const node_access EXECUTION_component_membership_access = {
    .flags = NODE_EFFECT_STRUCTURAL | NODE_EFFECT_HOST_SINK | NODE_EFFECT_ENTITY_SCOPED,
    .target_input = 2
};
static const char *break_request_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    if (!context->request_action_break ||
        context->request_action_break(context, args[1].u64) < 0) {
        return "Cannot request action loop break";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
static const char *break_take_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 2);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool requested = false;
    if (!context->take_action_break ||
        context->take_action_break(context, args[1].u64, &requested) < 0) {
        return "Cannot consume action loop break";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = requested };
    return NULL;
}
const node_host_call EXECUTION_break_request_call = { .value = break_request_value };
const node_host_call EXECUTION_break_take_call = { .value = break_take_value };
static const char *event_read_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    const pin_value
        *value = context->event_argument
                   ? context->event_argument(context, args[1].u64, args[2].i)
                   : NULL;
    if (!value || pin_value_snapshot_to_arena(storage, out, value)) {
        return "Cannot read originating event argument";
    }
    return NULL;
}
const node_host_call EXECUTION_event_read_call = { .value = event_read_value };
static const char *local_group_store_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 2);
    language_reference view,
        *reference = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    if (!reference || reference->kind != LANGUAGE_REFERENCE_VALUE || reference->path ||
        !reference->owner->store_local_group || !reference->owner->value_storage) {
        return "Local group replacement requires an owned value bank";
    }
    behavior_function_context *owner = reference->owner;
    int fields = count - 1;
    if (owner->store_local_group(owner, reference->value, fields, false)) {
        return "Local group replacement differs from its declaration";
    }
    pin_value *values = xcalloc((size_t)fields, sizeof *values);
    for (int i = 0; i < fields; i++) {
        const pin_value *type = &reference->value[i], *value = &args[i + 1];
        if (type->type != value->type || type->container != value->container ||
            ((type->type_name || value->type_name) &&
             (!type->type_name || !value->type_name ||
              strcmp(type->type_name, value->type_name)))) {
            free(values);
            return "Local group replacement field differs from its declared "
                   "type";
        }
        if (pin_value_snapshot_to_arena(owner->value_storage, &values[i], value)) {
            free(values);
            return "Cannot retain local group replacement";
        }
    }
    int status = owner->store_local_group(owner, reference->value, fields, true);
    if (!status) {
        memcpy(reference->value, values, (size_t)fields * sizeof *values);
    }
    free(values);
    if (status) {
        return "Cannot complete local group replacement";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
const node_host_call EXECUTION_local_group_store_call = { .value = local_group_store_value };
static const char *state_demand_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    int demand;
    if (!context->state_inputs ||
        context->state_inputs(context, args[1].u64, args[2].i, &demand) < 0) {
        return "Invalid action state input demand";
    }
    *out = (pin_value){ .type = PIN_I32, .i = demand };
    return NULL;
}
static const char *state_transition_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 3 && count <= 6);
    behavior_function_context *context = value_context(args[0]);
    int selected;
    if (!context->state_transition ||
        context->state_transition(context, args[1].u64, args[2].i, args + 3, count - 3, &selected) <
            0) {
        return "Invalid action state transition";
    }
    *out = (pin_value){ .type = PIN_I32, .i = selected };
    return NULL;
}
static const char *retrigger_action_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    behavior_function_context *context = value_context(args[0]);
    bool found;
    if (!context->retrigger_action ||
        context->retrigger_action(context, args[1].u64, args[2].d, &found) < 0) {
        return "Invalid retriggerable action timer";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = found };
    return NULL;
}
const node_host_call EXECUTION_state_demand_call = { .value = state_demand_value };
const node_host_call EXECUTION_state_transition_call = { .value = state_transition_value };
const node_host_call EXECUTION_retrigger_action_call = { .value = retrigger_action_value };
const node_host_call *execution_intrinsic_call(execution_intrinsic id)
{
    switch (id) {
        case EXECUTION_INTRINSIC_STATE_DEMAND:
            return &EXECUTION_state_demand_call;
        case EXECUTION_INTRINSIC_STATE_TRANSITION:
            return &EXECUTION_state_transition_call;
        case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
            return &EXECUTION_retrigger_action_call;
        case EXECUTION_INTRINSIC_SNAPSHOT: return &EXECUTION_snapshot_call;
        case EXECUTION_INTRINSIC_CONSTRUCT: return &EXECUTION_construct_call;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
            return &EXECUTION_reference_make_call;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            return &EXECUTION_reference_field_call;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
            return &EXECUTION_reference_read_call;
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            return &EXECUTION_reference_write_call;
        case EXECUTION_INTRINSIC_ACTION_READ:
            return &EXECUTION_action_read_call;
        case EXECUTION_INTRINSIC_ACTION_COMPLETE:
            return &EXECUTION_action_complete_call;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
            return &EXECUTION_component_has_call;
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return &EXECUTION_component_membership_call;
        case EXECUTION_INTRINSIC_BREAK_REQUEST:
            return &EXECUTION_break_request_call;
        case EXECUTION_INTRINSIC_BREAK_TAKE: return &EXECUTION_break_take_call;
        case EXECUTION_INTRINSIC_EVENT_READ: return &EXECUTION_event_read_call;
        case EXECUTION_INTRINSIC_LOCAL_GROUP_STORE:
            return &EXECUTION_local_group_store_call;
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
            return &EXECUTION_local_release_call;
        default: return NULL;
    }
}
uint64_t execution_intrinsic_id(const node_host_call *call)
{
    if (call == &EXECUTION_state_demand_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_STATE_DEMAND;
    }
    if (call == &EXECUTION_state_transition_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_STATE_TRANSITION;
    }
    if (call == &EXECUTION_retrigger_action_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_RETRIGGER_ACTION;
    }
    if (call == &EXECUTION_snapshot_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_SNAPSHOT;
    }
    if (call == &EXECUTION_construct_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_CONSTRUCT;
    }
    if (call == &EXECUTION_reference_make_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_MAKE;
    }
    if (call == &EXECUTION_reference_field_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_FIELD;
    }
    if (call == &EXECUTION_reference_read_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_READ;
    }
    if (call == &EXECUTION_reference_write_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_WRITE;
    }
    if (call == &EXECUTION_action_read_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ACTION_READ;
    }
    if (call == &EXECUTION_action_complete_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_ACTION_COMPLETE;
    }
    if (call == &EXECUTION_component_has_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_COMPONENT_HAS;
    }
    if (call == &EXECUTION_component_membership_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP;
    }
    if (call == &EXECUTION_break_request_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_BREAK_REQUEST;
    }
    if (call == &EXECUTION_break_take_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_BREAK_TAKE;
    }
    if (call == &EXECUTION_event_read_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_EVENT_READ;
    }
    if (call == &EXECUTION_local_group_store_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_LOCAL_GROUP_STORE;
    }
    if (call == &EXECUTION_local_release_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_LOCAL_RELEASE;
    }
    return 0;
}
const node_host_call *execution_intrinsic_resolve(uint64_t id)
{
    if ((id & UINT64_C(0xffffffffffffff00)) != UINT64_C(0x4348494900000000)) {
        return NULL;
    }
    return execution_intrinsic_call((execution_intrinsic)(id & 255));
}
const node_access *execution_intrinsic_access(uint64_t id)
{
    if (!execution_intrinsic_resolve(id)) {
        return NULL;
    }
    switch ((execution_intrinsic)(id & 255)) {
        case EXECUTION_INTRINSIC_SNAPSHOT:
        case EXECUTION_INTRINSIC_CONSTRUCT:
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            return &EXECUTION_value_access;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
            return &EXECUTION_component_read_access;
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return &EXECUTION_component_membership_access;
        default: return &EXECUTION_assignment_access;
    }
}
uint64_t execution_intrinsic_abi(uint64_t id)
{
    return execution_intrinsic_resolve(id) ? 1 : 0;
}
