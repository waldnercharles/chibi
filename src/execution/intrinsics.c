#include "execution/intrinsics.h"

#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/function.h"
#include "execution/scalar.h"
#include "support/arena.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/copy_internal.h"
#include "types/trace.h"
#include "types/union.h"

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
    type_validate_ranges(type, out->ptr);
    return NULL;
}
const node_access EXECUTION_assignment_access = { .flags = NODE_EFFECT_HOST_SINK };
const node_host_call EXECUTION_snapshot_call = { .value = snapshot_value };
const node_host_call EXECUTION_construct_call = { .value = construct_value };
const node_access EXECUTION_value_access = { .flags = NODE_EFFECT_PURE };
static const char *union_make(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 1);
    if (!args[0].u64) {
        return "Union constructor requires a nonzero tag";
    }
    if (args[0].u64 == PIN_UNION_CANCELLED_TAG && count == 1) {
        pin_union_cancelled(out);
        return NULL;
    }
    return pin_union_value(storage, args[0].u64, args + 1, count - 1, out)
             ? "Cannot retain union payload"
             : NULL;
}
static const char *union_test(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 2);
    const pin_union *value = args[0].ptr;
    bool match = false;
    for (int i = 1; value && i < count; i++) {
        match |= value->tag == args[i].u64;
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = match };
    return NULL;
}
static const char *union_get(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count >= 4);
    const pin_union *value = args[0].ptr;
    bool match = value && (!args[1].u64 || value->tag == args[1].u64);
    for (int i = 4; value && i < count; i++) {
        match |= value->tag == args[i].u64;
    }
    if (!match) {
        out->u64 = 0;
        return NULL;
    }
    if (args[3].b) {
        if (out->type != PIN_UNION || out->container != PIN_CONTAINER_SINGLE) {
            return "Union payload differs from the extraction type";
        }
        *out = args[0];
        return NULL;
    }
    int field = args[2].i;
    if (field < 0 || field >= value->count) {
        return "Union payload index exceeds its case layout";
    }
    const pin_value *payload = &value->values[field];
    if (payload->type != out->type || payload->container != out->container ||
        strcmp(payload->type_name ? payload->type_name : "", out->type_name ? out->type_name : "")) {
        return "Union payload differs from the extraction type";
    }
    *out = *payload;
    return NULL;
}
const node_host_call EXECUTION_union_make_call = { .value = union_make };
const node_host_call EXECUTION_union_test_call = { .value = union_test };
const node_host_call EXECUTION_union_get_call = { .value = union_get };
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
    if (reference->kind == LANGUAGE_REFERENCE_STATE) {
        mark(user, reference->owner);
    }
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
    /* State stays with the instance that owns it, which a library task's
     * context does not; keep a copy of the originating context. */
    if (base->kind == LANGUAGE_REFERENCE_STATE) {
        ref->owner = arena_alloc(storage, sizeof *ref->owner);
        *ref->owner = *base->owner;
    }
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
static const type_desc *reference_root_layout(const language_reference *ref)
{
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT) {
        return NULL;
    }
    const ecs_execution_column *column = ecs_execution_find_column(ref->owner->world, ref->component);
    return column && column->layout == ref->layout && column->stride == ref->stride
             ? column->type
             : NULL;
}
static void *reference_path_data(
    const language_reference_path *path,
    pin_value *type,
    void *data,
    const type_desc *root_layout
)
{
    if (!path || !data) {
        return data;
    }
    data = reference_path_data(path->parent, type, data, root_layout);
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
    const type_desc *layout = !path->parent && root_layout ? root_layout
                            : type->type == PIN_STRUCT && type->container == PIN_CONTAINER_SINGLE
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
    return reference_path_data(ref->path, &type, data, reference_root_layout(ref));
}
static pin_value *reference_item(const language_reference *ref, pin_value *root)
{
    const language_reference_path *path = ref->path;
    assert(path && path->element);
    pin_value type = *root;
    void *data = root->type == PIN_STRUCT && root->container == PIN_CONTAINER_SINGLE
                   ? root->ptr
                   : &root->d;
    data = reference_path_data(path->parent, &type, data, reference_root_layout(ref));
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
static int snapshot_reference_root(
    arena *storage,
    pin_value *out,
    const pin_value *source,
    const type_desc *layout
)
{
    if (!layout) {
        return pin_value_snapshot_to_arena(storage, out, source);
    }
    assert(source->type == PIN_STRUCT && source->container == PIN_CONTAINER_SINGLE);
    *out = *source;
    pin_copy_record(PIN_COPY_VALUE_HEADER, 1);
    size_t name_size = strlen(layout->name) + 1;
    char *name = arena_alloc(storage, name_size);
    memcpy(name, layout->name, name_size);
    pin_copy_record(PIN_COPY_NOMINAL, name_size);
    out->type_name = name;
    out->ptr = arena_alloc(storage, layout->size);
    memcpy(out->ptr, source->ptr, layout->size);
    pin_copy_record(PIN_COPY_RECORD, layout->size);
    return pin_struct_snapshot_fields(storage, layout, out->ptr);
}
static const char *read_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    language_reference view,
        *ref = unbox((language_reference *)(uintptr_t)args[0].u64, &view);
    pin_value root;
    void *data = reference_data(ref, &root);
    if (ref->path && ref->path->element) {
        const pin_value *item = reference_item(ref, &root);
        return !item || pin_value_snapshot_to_arena(storage, out, item)
                 ? "Referenced array element is unavailable"
                 : NULL;
    }
    const type_desc *layout = !ref->path ? reference_root_layout(ref) : NULL;
    size_t size = layout ? layout->size : execution_value_size(ref->type);
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
    if (snapshot_reference_root(storage, &retained, out, layout)) {
        return "Cannot retain referenced value";
    }
    *out = retained;
    return NULL;
}
static bool component_scalar_path(const language_reference *ref)
{
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT || !ref->path ||
        ref->type.container != PIN_CONTAINER_SINGLE || ref->type.type == PIN_STRUCT) {
        return false;
    }
    for (const language_reference_path *path = ref->path; path; path = path->parent) {
        if (path->element) {
            return false;
        }
    }
    return true;
}
static const char *write_reference_value(arena *owner_storage, language_reference *ref, const pin_value *args)
{
    pin_value root;
    void *current = reference_data(ref, &root);
    bool element = ref->path && ref->path->element;
    if (!current && (element ? !reference_item(ref, &root)
                             : ref->path || ref->kind == LANGUAGE_REFERENCE_COMPONENT)) {
        return "Referenced storage is unavailable";
    }
    if (!owner_storage) {
        return "Referenced owner has no retained value storage";
    }
    if (component_scalar_path(ref)) {
        size_t offset = (const char *)current - (const char *)root.ptr;
        return !ref->owner->store_component || ref->owner->store_component(
                                                   ref->owner,
                                                   ref->entity,
                                                   ref->component,
                                                   ref->layout,
                                                   offset,
                                                   ref->type,
                                                   args[1]
                                               )
                 ? "Cannot retain referenced component write"
                 : NULL;
    }
    if (ref->kind == LANGUAGE_REFERENCE_COMPONENT && !ref->path) {
        return !ref->owner->store_component || ref->owner->store_component(
                                                   ref->owner,
                                                   ref->entity,
                                                   ref->component,
                                                   ref->layout,
                                                   0,
                                                   root,
                                                   args[1]
                                               )
                 ? "Cannot retain referenced component write"
                 : NULL;
    }
    const type_desc *root_layout = reference_root_layout(ref);
    pin_value value, retained;
    if (pin_value_snapshot_to_arena(owner_storage, &value, &args[1]) ||
        snapshot_reference_root(owner_storage, &retained, &root, root_layout)) {
        return "Cannot retain referenced value";
    }
    if (!ref->path) {
        retained = value;
    } else if (element) {
        pin_value *item = reference_item(ref, &retained);
        if (!item) {
            return "Referenced array element is unavailable";
        }
        *item = value;
    } else {
        pin_value type = retained;
        void *data = retained.type == PIN_STRUCT && retained.container == PIN_CONTAINER_SINGLE
                       ? retained.ptr
                       : &retained.d;
        void *target = reference_path_data(ref->path, &type, data, root_layout);
        size_t size = execution_value_size(ref->type);
        if (!target || !size) {
            return "Referenced path is unavailable";
        }
        const void *source = value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE
                               ? value.ptr
                               : &value.d;
        memcpy(target, source, size);
    }
    if (ref->kind != LANGUAGE_REFERENCE_COMPONENT && retained.type == PIN_STRUCT &&
        retained.container == PIN_CONTAINER_SINGLE) {
        const type_desc *type = type_catalog_find(retained.type_name);
        assert(type);
        type_validate_ranges(type, retained.ptr);
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
    if (ref->kind == LANGUAGE_REFERENCE_VALUE && ref->box &&
        ref->box->owner == &ref->box->detached) {
        execution_box *box = ref->box;
        assert(box->value == &box->kept && box->detached.value_storage == &box->storage);
        /* Logical references keep the box, not an address in its payload.
         * Stage the write without publishing, then retain only the final root:
         * a path write's working snapshot still includes the replaced value. */
        arena working = { 0 }, committed = { 0 };
        pin_value candidate = box->kept, retained;
        language_reference staged = *ref;
        staged.value = &candidate;
        error = write_reference_value(&working, &staged, args);
        if (!error && pin_value_snapshot_to_arena(&committed, &retained, &candidate)) {
            error = "Cannot retain referenced value";
        }
        if (!error) {
            arena previous = box->storage;
            box->kept = retained;
            box->storage = committed;
            arena_free(&previous);
        } else {
            arena_free(&committed);
        }
        arena_free(&working);
    } else if (ref->kind == LANGUAGE_REFERENCE_VALUE) {
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
static const char *reduce_reference(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 3);
    (void)count;
    language_reference *ref = (language_reference *)(uintptr_t)args[0].u64;
    if (!component_scalar_path(ref) || ref->type.type < PIN_I8 ||
        ref->type.type > PIN_F64 || args[1].type != ref->type.type ||
        args[1].container != PIN_CONTAINER_SINGLE || args[2].type != PIN_I32 ||
        args[2].container != PIN_CONTAINER_SINGLE || args[2].i < EXECUTION_REDUCTION_ADD ||
        args[2].i > EXECUTION_REDUCTION_ADD_UNORDERED) {
        return "Reduction requires a numeric component field and matching "
               "value";
    }
    pin_value root;
    const void *target = reference_data(ref, &root);
    if (!target || !root.ptr || !ref->owner->reduce_component) {
        return "Reduction destination is unavailable";
    }
    size_t offset = (const char *)target - (const char *)root.ptr;
    int status = ref->owner->reduce_component(
        ref->owner,
        ref->entity,
        ref->component,
        ref->layout,
        offset,
        ref->type,
        args[1],
        (execution_reduction_op)args[2].i
    );
    *out = (pin_value){ .type = PIN_BOOL, .b = status == 0 };
    return status ? "Cannot collect component reduction" : NULL;
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
const node_host_call EXECUTION_reference_reduce_call = { .value = reduce_reference };
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
static const char *query_source_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 5);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world || !context->world->query_source) {
        return "Query source lookup requires an ECS world";
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .u64 = context->world->query_source(
                            context->world,
                            args[1].u64,
                            args[2].u64,
                            args[3].u64,
                            args[4].i
                        ) };
    return NULL;
}
const node_host_call EXECUTION_query_source_call = { .value = query_source_value };
static const char *query_target_value(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    assert(count == 4);
    (void)count;
    const behavior_function_context *context = value_context(args[0]);
    if (!context->world || !context->world->query_target) {
        return "Relationship iteration requires an ECS world";
    }
    *out = (pin_value){ .type = PIN_HANDLE,
                        .u64 = context->world->query_target(
                            context->world,
                            args[1].u64,
                            args[2].u64,
                            args[3].i
                        ) };
    return NULL;
}
const node_host_call EXECUTION_query_target_call = { .value = query_target_value };
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
        case EXECUTION_INTRINSIC_UNION_MAKE: return &EXECUTION_union_make_call;
        case EXECUTION_INTRINSIC_UNION_TEST: return &EXECUTION_union_test_call;
        case EXECUTION_INTRINSIC_UNION_GET: return &EXECUTION_union_get_call;
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
            return &EXECUTION_reference_make_call;
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            return &EXECUTION_reference_field_call;
        case EXECUTION_INTRINSIC_REFERENCE_READ:
            return &EXECUTION_reference_read_call;
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            return &EXECUTION_reference_write_call;
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
            return &EXECUTION_reference_reduce_call;
        case EXECUTION_INTRINSIC_ACTION_READ:
            return &EXECUTION_action_read_call;
        case EXECUTION_INTRINSIC_ACTION_COMPLETE:
            return &EXECUTION_action_complete_call;
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
            return &EXECUTION_component_has_call;
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
            return &EXECUTION_query_source_call;
        case EXECUTION_INTRINSIC_QUERY_TARGET:
            return &EXECUTION_query_target_call;
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return &EXECUTION_component_membership_call;
        case EXECUTION_INTRINSIC_BREAK_REQUEST:
            return &EXECUTION_break_request_call;
        case EXECUTION_INTRINSIC_BREAK_TAKE: return &EXECUTION_break_take_call;
        case EXECUTION_INTRINSIC_EVENT_READ: return &EXECUTION_event_read_call;
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
            return &EXECUTION_local_release_call;
        default: return NULL;
    }
}
uint64_t execution_intrinsic_id(const node_host_call *call)
{
    if (call == &EXECUTION_query_target_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_QUERY_TARGET;
    }
    if (call == &EXECUTION_union_make_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_MAKE;
    }
    if (call == &EXECUTION_union_test_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_TEST;
    }
    if (call == &EXECUTION_union_get_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_UNION_GET;
    }
    if (call == &EXECUTION_query_source_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_QUERY_SOURCE;
    }
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
    if (call == &EXECUTION_reference_reduce_call) {
        return UINT64_C(0x4348494900000000) | EXECUTION_INTRINSIC_REFERENCE_REDUCE;
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
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
        case EXECUTION_INTRINSIC_QUERY_TARGET:
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
