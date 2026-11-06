#include "runtime/scenes/prepared.h"

#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    SCENE_ITEM_MAX = 1024,
    SCENE_COMPONENT_MAX = 4096
};
static const uint64_t magic = UINT64_C(0x31454e4543534843); /* CHSCENE1 */
static bool error_text(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static void *allocate(execution_wire *wire, int count, size_t element)
{
    if (wire->error) {
        return NULL;
    }
    if (count < 0 ||
        (size_t)count > (EXECUTION_WIRE_MAX_BYTES - wire->owned_bytes) / element) {
        wire->error = "Prepared scene owned storage limit exceeded";
        return NULL;
    }
    size_t bytes = (size_t)count * element;
    void *data = execution_wire_allocate(wire, bytes);
    if (data) {
        memset(data, 0, bytes);
    }
    return data;
}
static bool same_name(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static void header(execution_wire *wire)
{
    uint64_t identity = magic, version = SCENE_PREPARED_VERSION;
    execution_wire_u64(wire, &identity);
    execution_wire_u64(wire, &version);
    if (!wire->error && (identity != magic || version != SCENE_PREPARED_VERSION)) {
        wire->error = "Prepared scene magic or version is unsupported";
    }
}
static void real(execution_wire *wire, float *f)
{
    pin_value value = { .type = PIN_F32, .f = *f };
    execution_wire_value(wire, &value);
    if (wire->error) {
        return;
    }
    if (value.type != PIN_F32 || value.container != PIN_CONTAINER_SINGLE ||
        value.type_name) {
        wire->error = "Prepared scene physics scalar type differs";
        return;
    }
    *f = value.f;
}
static void settings(execution_wire *wire, physics_settings *p)
{
    execution_wire_int(wire, &p->workers);
    execution_wire_int(wire, &p->substeps);
    execution_wire_bool(wire, &p->sleep);
    execution_wire_bool(wire, &p->continuous);
    execution_wire_bool(wire, &p->force_drive);
    real(wire, &p->contact_hertz);
    real(wire, &p->contact_damping);
    if (!wire->error && !physics_settings_valid(*p)) {
        wire->error = "Prepared scene physics settings are invalid";
    }
}
static void count(execution_wire *wire, int *value)
{
    execution_wire_count(wire, value, 1);
    if (wire->error) {
        return;
    }
    if (*value > SCENE_ITEM_MAX) {
        wire->error = "Prepared scene object/item limit exceeded";
        return;
    }
}
static const ecs_component_desc *resolve(execution_wire *wire, ecs_component_catalog catalog, ecs_component_id id)
{
    const ecs_component_desc *found = NULL;
    for (int i = 0; i < catalog.component_count; i++) {
        const ecs_component_desc *desc = catalog.components[i];
        if (desc && desc->id == id) {
            if (found) {
                wire->error = "Prepared scene component identity is ambiguous";
                return NULL;
            }
            found = desc;
        }
    }
    if (!found) {
        return NULL;
    }
    if (!id || !found->type || !found->name || !found->element_size ||
        found->type->size != found->element_size || found->derived_from) {
        wire->error = "Prepared scene component is unavailable or invalid";
        return NULL;
    }
    return found;
}
static void component(
    execution_wire *wire,
    scene_component *row,
    ecs_component_catalog catalog,
    char *diagnostic,
    size_t diagnostic_size
)
{
    const ecs_component_desc *desc = row->desc;
    uint64_t id = desc ? desc->id : 0;
    uint64_t layout = desc ? ecs_layout_hash(desc->type) : 0;
    size_t bytes = desc ? desc->element_size : 0;
    const char *name = desc ? desc->name : NULL, *key = desc ? desc->key : NULL;
    int index = desc ? desc->index : 0;
    execution_wire_u64(wire, &id);
    execution_wire_u64(wire, &layout);
    execution_wire_size(wire, &bytes);
    execution_wire_string(wire, &name);
    execution_wire_string(wire, &key);
    execution_wire_int(wire, &index);
    if (wire->error) {
        return;
    }
    if (wire->reading) {
        desc = resolve(wire, catalog, id);
        if (!desc && !wire->error && key && *key && name &&
            !strncmp(name, "PrototypeTag:", 13) && !strcmp(name + 13, key) &&
            id == ecs_component_marker_id(key) && bytes == TYPE_MARKER.size &&
            layout == type_layout_hash(&TYPE_MARKER) && index == ECS_INDEX_BITSET) {
            ecs_component_desc *marker = allocate(wire, 1, sizeof *marker);
            if (marker) {
                *marker = (ecs_component_desc){ .id = id,
                                                .key = key,
                                                .name = name,
                                                .element_size = bytes,
                                                .type = &TYPE_MARKER,
                                                .layout_hash = layout,
                                                .index = ECS_INDEX_BITSET };
                desc = marker;
            }
        }
        if (!desc) {
            if (!wire->error) {
                snprintf(
                    diagnostic,
                    diagnostic_size,
                    "Prepared component %.96s key %.96s id %016llx is "
                    "unavailable "
                    "or has an invalid marker schema",
                    name ? name : "(unnamed)",
                    key ? key : "(none)",
                    (unsigned long long)id
                );
                wire->error = diagnostic;
            }
            return;
        }
    }
    if (!desc || !desc->type || !desc->type->name || !layout ||
        layout != ecs_layout_hash(desc->type) || bytes != desc->element_size ||
        bytes != desc->type->size || !same_name(name, desc->name) ||
        !same_name(key, desc->key) || index < ECS_INDEX_BITSET ||
        index > ECS_INDEX_DENSE || index != (int)desc->index ||
        (desc->layout_hash && layout != desc->layout_hash)) {
        wire->error = "Prepared scene component identity or layout differs";
        return;
    }
    const type_desc *linked = type_catalog_find(desc->type->name);
    if (!linked || linked->size != bytes || type_layout_hash(linked) != layout) {
        snprintf(
            diagnostic,
            diagnostic_size,
            "Prepared component %.96s type %.96s differs from host value "
            "catalog "
            "(row size %zu layout %016llx; host size %zu layout %016llx)",
            desc->name ? desc->name : "(unnamed)",
            desc->type->name,
            bytes,
            (unsigned long long)layout,
            linked ? linked->size : 0,
            (unsigned long long)(linked ? type_layout_hash(linked) : 0)
        );
        wire->error = diagnostic;
        return;
    }
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = desc->type->name,
                        .ptr = (void *)row->data };
    execution_wire_value(wire, &value);
    if (!wire->error &&
        (value.type != PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE ||
         !same_name(value.type_name, desc->type->name) || !value.ptr)) {
        wire->error = "Prepared scene component value differs from declared "
                      "row";
    }
    if (!wire->error && wire->reading) {
        *row = (scene_component){ desc, value.ptr };
    }
}
static void object_link(execution_wire *wire, scene_link *value)
{
    execution_wire_int(wire, &value->object);
    execution_wire_int(wire, &value->component);
    execution_wire_int(wire, &value->field);
    execution_wire_int(wire, &value->target);
}
static void relationship(execution_wire *wire, ecs_relationship_desc *value)
{
    execution_wire_u64(wire, &value->id);
    execution_wire_string(wire, &value->key);
    execution_wire_string(wire, &value->name);
    uint64_t traits = value->traits;
    int cleanup = value->on_delete_target;
    execution_wire_u64(wire, &traits);
    execution_wire_int(wire, &cleanup);
    if (traits > (ECS_REL_EXCLUSIVE | ECS_REL_ACYCLIC | ECS_REL_TRAVERSABLE) ||
        cleanup < ECS_CLEANUP_REMOVE || cleanup > ECS_CLEANUP_PANIC) {
        wire->error = "Prepared relationship traits or cleanup policy are "
                      "invalid";
    }
    value->traits = (unsigned)traits;
    value->on_delete_target = (ecs_cleanup)cleanup;
}
static void pair(execution_wire *wire, scene_pair *value)
{
    execution_wire_int(wire, &value->object);
    execution_wire_int(wire, &value->relationship);
    execution_wire_int(wire, &value->target);
}
static void write_scene(execution_wire *wire, const scene *scene, char *diagnostic, size_t diagnostic_size)
{
    const char *name = scene_name(scene), *ui = scene_ui(scene);
    bool perspective = scene_perspective(scene);
    physics_settings physics = scene_physics(scene);
    execution_wire_string(wire, &name);
    execution_wire_string(wire, &ui);
    execution_wire_bool(wire, &perspective);
    settings(wire, &physics);
    int objects = scene_object_count(scene);
    count(wire, &objects);
    for (int i = 0; !wire->error && i < objects; i++) {
        const char *object_name = scene_object_name(scene, i);
        execution_wire_string(wire, &object_name);
        int parent = scene_object_parent(scene, i);
        execution_wire_int(wire, &parent);
        int components = scene_object_component_count(scene, i);
        count(wire, &components);
        for (int j = 0; !wire->error && j < components; j++) {
            scene_component row = scene_object_component_at(scene, i, j);
            component(wire, &row, (ecs_component_catalog){ 0 }, diagnostic, diagnostic_size);
        }
        int behaviors = scene_object_behavior_count(scene, i);
        count(wire, &behaviors);
        for (int j = 0; !wire->error && j < behaviors; j++) {
            const char *behavior = scene_object_behavior(scene, i, j);
            execution_wire_string(wire, &behavior);
        }
    }
    int links = scene_link_count(scene);
    count(wire, &links);
    for (int i = 0; !wire->error && i < links; i++) {
        scene_link one = scene_link_at(scene, i);
        object_link(wire, &one);
    }
    int relationships = scene_relationship_count(scene);
    count(wire, &relationships);
    for (int i = 0; !wire->error && i < relationships; i++) {
        ecs_relationship_desc one = *scene_relationship_at(scene, i);
        relationship(wire, &one);
    }
    int pairs = scene_pair_count(scene);
    count(wire, &pairs);
    for (int i = 0; !wire->error && i < pairs; i++) {
        scene_pair one = scene_pair_at(scene, i);
        pair(wire, &one);
    }
}
bool scene_prepared_write(const scene *scene, unsigned char **data, size_t *length, char *error, size_t size)
{
    assert(scene && data && length && (!size || error));
    *data = NULL;
    *length = 0;
    char diagnostic[512];
    execution_wire wire = { 0 };
    header(&wire);
    write_scene(&wire, scene, diagnostic, sizeof diagnostic);
    if (wire.error) {
        afree(wire.data);
        return error_text(error, size, wire.error);
    }
    *data = xmalloc(wire.size);
    memcpy(*data, wire.data, wire.size);
    *length = wire.size;
    afree(wire.data);
    if (size) {
        error[0] = 0;
    }
    return true;
}
scene *scene_prepared_read_budget(
    const void *data,
    size_t length,
    ecs_component_catalog catalog,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining && (!size || error));
    if (catalog.component_count < 0 || catalog.component_count > SCENE_COMPONENT_MAX ||
        (catalog.component_count && !catalog.components)) {
        error_text(error, size, "Prepared scene component catalog is invalid");
        return NULL;
    }
    size_t budget = *remaining < EXECUTION_WIRE_MAX_BYTES ? *remaining
                                                          : EXECUTION_WIRE_MAX_BYTES;
    size_t allocation_budget = budget;
    arena storage = { 0 };
    char diagnostic[512];
    execution_wire wire = { .data = (unsigned char *)data,
                            .size = length,
                            .reading = true,
                            .storage = &storage,
                            .owned_bytes = EXECUTION_WIRE_MAX_BYTES - budget,
                            .storage_budget = &allocation_budget };
    header(&wire);
    scene_spec spec = { 0 };
    execution_wire_string(&wire, &spec.name);
    execution_wire_string(&wire, &spec.ui);
    execution_wire_bool(&wire, &spec.perspective);
    settings(&wire, &spec.physics);
    count(&wire, &spec.object_count);
    scene_object *objects = allocate(&wire, spec.object_count, sizeof *objects);
    int *parents = allocate(&wire, spec.object_count, sizeof *parents);
    spec.objects = objects;
    spec.parents = parents;
    for (int i = 0; !wire.error && i < spec.object_count; i++) {
        scene_object *object = &objects[i];
        execution_wire_string(&wire, &object->name);
        execution_wire_int(&wire, &parents[i]);
        count(&wire, &object->component_count);
        scene_component *components = allocate(&wire, object->component_count, sizeof *components);
        object->components = components;
        for (int j = 0; !wire.error && j < object->component_count; j++) {
            component(&wire, &components[j], catalog, diagnostic, sizeof diagnostic);
        }
        count(&wire, &object->behavior_count);
        const char **behaviors = allocate(&wire, object->behavior_count, sizeof *behaviors);
        object->behaviors = behaviors;
        for (int j = 0; !wire.error && j < object->behavior_count; j++) {
            execution_wire_string(&wire, &behaviors[j]);
        }
    }
    count(&wire, &spec.link_count);
    scene_link *links = allocate(&wire, spec.link_count, sizeof *links);
    spec.links = links;
    for (int i = 0; !wire.error && i < spec.link_count; i++) {
        object_link(&wire, &links[i]);
    }
    count(&wire, &spec.relationship_count);
    ecs_relationship_desc *relationships = allocate(&wire, spec.relationship_count, sizeof *relationships);
    spec.relationships = relationships;
    for (int i = 0; !wire.error && i < spec.relationship_count; i++) {
        relationship(&wire, &relationships[i]);
    }
    count(&wire, &spec.pair_count);
    scene_pair *pairs = allocate(&wire, spec.pair_count, sizeof *pairs);
    spec.pairs = pairs;
    for (int i = 0; !wire.error && i < spec.pair_count; i++) {
        pair(&wire, &pairs[i]);
    }
    if (!wire.error && wire.offset != wire.size) {
        wire.error = "Prepared scene has trailing input";
    }
    scene *scene = wire.error ? NULL : scene_prepare(&spec, error, size);
    if (wire.error) {
        error_text(error, size, wire.error);
    }
    if (scene) {
        size_t bytes = scene_storage_bytes(scene);
        if (bytes > budget) {
            error_text(error, size, "Prepared scene retained storage budget exceeded");
            scene_free(scene);
            scene = NULL;
        } else {
            *remaining -= bytes;
        }
    }
    arena_free(&storage);
    return scene;
}

scene *scene_prepared_read(
    const void *data,
    size_t length,
    ecs_component_catalog catalog,
    char *error,
    size_t size
)
{
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    return scene_prepared_read_budget(data, length, catalog, &remaining, error, size);
}
