#include "scene_history.h"

#include "../core/array.h"
#include "../game/generated.h"

#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct
{
    ecs_entity entity;
    uint64_t group;
    GameTransform before;
    GameTransform after;
    scene_field_edit *fields;
    scene_actor_edit *actor;
    scene_behavior_edit *behaviors;
    scene_component_edit *component;
} scene_edit;

struct scene_history
{
    scene_edit *undo;
    scene_edit *redo;
    uint64_t group;
    uint64_t next_group;
};

scene_history *scene_history_new()
{
    return xcalloc(1, sizeof(scene_history));
}

static void free_names(const char *const *names, int count)
{
    for (int i = 0; i < count; i++) {
        free((void *)names[i]);
    }
    free((void *)names);
}

static void clear_edits(scene_edit **edits)
{
    for (int i = 0; i < acount(*edits); i++) {
        scene_field_edit *fields = (*edits)[i].fields;
        for (int j = 0; j < acount(fields); j++) {
            free((void *)fields[j].before.type_name);
            free((void *)fields[j].after.type_name);
        }
        afree(fields);
        scene_actor_edit *actor = (*edits)[i].actor;
        if (actor) {
            free((void *)actor->name);
            free((void *)actor->prototype_name);
            free((void *)actor->base_json);
            free((void *)actor->state_json);
            free(actor);
        }
        scene_component_edit *component = (*edits)[i].component;
        if (component) {
            free((void *)component->state_json);
            free(component);
        }
        scene_behavior_edit *behaviors = (*edits)[i].behaviors;
        if (behaviors) {
            free_names(behaviors->before, behaviors->before_count);
            free_names(behaviors->after, behaviors->after_count);
            free(behaviors);
        }
    }
    aclear(*edits);
}

void scene_history_free(scene_history *history)
{
    if (!history) {
        return;
    }
    clear_edits(&history->undo);
    clear_edits(&history->redo);
    afree(history->undo);
    afree(history->redo);
    free(history);
}

void scene_history_clear(scene_history *history)
{
    assert(history);
    clear_edits(&history->undo);
    clear_edits(&history->redo);
    history->group = 0;
}

void scene_history_begin_group(scene_history *history)
{
    assert(history && !history->group && history->next_group != UINT64_MAX);
    history->group = ++history->next_group;
}

void scene_history_end_group(scene_history *history)
{
    assert(history && history->group);
    history->group = 0;
}

uint64_t scene_history_group_id(const scene_history *history, bool redo)
{
    assert(history);
    const scene_edit *edits = redo ? history->redo : history->undo;
    return acount(edits) ? alast(edits).group : 0;
}

bool scene_history_push(scene_history *history, ecs_entity entity, GameTransform before, GameTransform after)
{
    assert(history && entity.slot);
    assert(isfinite(before.x) && isfinite(before.y));
    assert(isfinite(after.x) && isfinite(after.y));
    if (before.x == after.x && before.y == after.y) {
        return false;
    }
    apush(
        history->undo,
        ((scene_edit){ .entity = entity, .group = history->group, .before = before, .after = after })
    );
    clear_edits(&history->redo);
    return true;
}

static bool scalar(pin_value value)
{
    if (value.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    switch (value.type) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_I64:
        case PIN_U64:
        case PIN_ENUM:
        case PIN_HANDLE: return true;
        case PIN_F32: return isfinite(value.f);
        case PIN_F64: return isfinite(value.d);
        default: return false;
    }
}

static bool same_type(pin_value a, pin_value b)
{
    if (a.type != b.type) {
        return false;
    }
    if (a.type != PIN_ENUM && a.type != PIN_HANDLE) {
        return true;
    }
    return a.type_name && b.type_name && strcmp(a.type_name, b.type_name) == 0;
}

static bool same_value(pin_value a, pin_value b)
{
    switch (a.type) {
        case PIN_BOOL: return a.b == b.b;
        case PIN_I8: return a.i8 == b.i8;
        case PIN_U8: return a.u8 == b.u8;
        case PIN_I16: return a.i16 == b.i16;
        case PIN_U16: return a.u16 == b.u16;
        case PIN_I32: return a.i == b.i;
        case PIN_U32: return a.u == b.u;
        case PIN_I64: return a.i64 == b.i64;
        case PIN_U64:
        case PIN_HANDLE: return a.u64 == b.u64;
        case PIN_F32: return a.f == b.f;
        case PIN_F64: return a.d == b.d;
        case PIN_ENUM: return a.e == b.e;
        default: return false;
    }
}

bool scene_history_push_fields(
    scene_history *history,
    ecs_entity entity,
    const scene_field_edit *fields,
    int count
)
{
    assert(history && entity.slot && count >= 0 && (!count || fields));
    bool changed = false;
    for (int i = 0; i < count; i++) {
        if (fields[i].field < 0 || !scalar(fields[i].before) ||
            !scalar(fields[i].after) ||
            !same_type(fields[i].before, fields[i].after)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (fields[i].component == fields[j].component &&
                fields[i].field == fields[j].field) {
                return false;
            }
        }
        changed |= !same_value(fields[i].before, fields[i].after);
    }
    if (!changed) {
        return false;
    }
    scene_edit edit = { .entity = entity };
    for (int i = 0; i < count; i++) {
        scene_field_edit field = fields[i];
        field.before.type_name = field.before.type_name
                                   ? xstrdup(field.before.type_name)
                                   : NULL;
        field.after.type_name = field.after.type_name ? xstrdup(field.after.type_name)
                                                      : NULL;
        apush(edit.fields, field);
    }
    edit.group = history->group;
    apush(history->undo, edit);
    clear_edits(&history->redo);
    return true;
}

bool scene_history_push_field(
    scene_history *history,
    ecs_entity entity,
    ecs_component_id component,
    int field,
    pin_value before,
    pin_value after
)
{
    scene_field_edit edit = { component, field, before, after };
    return scene_history_push_fields(history, entity, &edit, 1);
}

static bool valid_fields(ecs_world *world, const scene_edit *edit)
{
    for (int i = 0; i < acount(edit->fields); i++) {
        const scene_field_edit *field = &edit->fields[i];
        const ecs_component_desc *desc = ecs_world_component(world, field->component);
        if (!desc || !desc->type || field->field >= desc->type->field_count ||
            !ecs_component_has(world, edit->entity, field->component)) {
            return false;
        }
        const type_field *type = &desc->type->fields[field->field];
        pin_value expected = { .type = type->type, .type_name = type->type_name };
        if (!same_type(expected, field->before) || !same_type(expected, field->after)) {
            return false;
        }
    }
    return true;
}

bool scene_history_push_actor(scene_history *history, ecs_entity entity, const scene_actor_edit *actor)
{
    assert(history && entity.slot && actor && actor->index >= 0);
    assert(actor->name && actor->prototype_name && actor->base_json && actor->state_json);
    scene_actor_edit *copy = xmalloc(sizeof *copy);
    *copy = *actor;
    copy->name = xstrdup(actor->name);
    copy->prototype_name = xstrdup(actor->prototype_name);
    copy->base_json = xstrdup(actor->base_json);
    copy->state_json = xstrdup(actor->state_json);
    apush(history->undo, ((scene_edit){ .entity = entity, .group = history->group, .actor = copy }));
    clear_edits(&history->redo);
    return true;
}

const scene_actor_edit *scene_history_peek_actor(
    const scene_history *history,
    bool redo,
    ecs_entity *entity,
    bool *present
)
{
    assert(history);
    const scene_edit *source = redo ? history->redo : history->undo;
    if (!acount(source) || !alast(source).actor) {
        return NULL;
    }
    const scene_edit *edit = &alast(source);
    if (entity) {
        *entity = edit->entity;
    }
    if (present) {
        *present = edit->actor->created ? redo : !redo;
    }
    return edit->actor;
}

static void rebind_actor(scene_edit *edits, ecs_entity old, ecs_entity replacement)
{
    for (int i = 0; i < acount(edits); i++) {
        if (ecs_entity_to_u64(edits[i].entity) == ecs_entity_to_u64(old)) {
            edits[i].entity = replacement;
        }
    }
}

void scene_history_complete_actor(scene_history *history, bool redo, ecs_entity replacement)
{
    assert(history);
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    assert(acount(*source) && alast(*source).actor);
    bool present = alast(*source).actor->created ? redo : !redo;
    assert(present == (replacement.slot != 0));
    if (present) {
        ecs_entity old = alast(*source).entity;
        rebind_actor(history->undo, old, replacement);
        rebind_actor(history->redo, old, replacement);
    }
    scene_edit edit = alast(*source);
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
}

static bool valid_names(const char *const *names, int count)
{
    if (count < 0 || (count && !names)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!names[i] || !names[i][0]) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                return false;
            }
        }
    }
    return true;
}

static const char **copy_names(const char *const *names, int count)
{
    if (!count) {
        return NULL;
    }
    const char **copy = xmalloc((size_t)count * sizeof *copy);
    for (int i = 0; i < count; i++) {
        copy[i] = xstrdup(names[i]);
    }
    return copy;
}

bool scene_history_push_behaviors(scene_history *history, ecs_entity entity, const scene_behavior_edit *edit)
{
    assert(history && entity.slot && edit);
    if (!valid_names(edit->before, edit->before_count) ||
        !valid_names(edit->after, edit->after_count)) {
        return false;
    }
    bool changed = edit->before_count != edit->after_count;
    for (int i = 0; !changed && i < edit->before_count; i++) {
        changed = strcmp(edit->before[i], edit->after[i]) != 0;
    }
    if (!changed) {
        return false;
    }
    scene_behavior_edit *copy = xmalloc(sizeof *copy);
    *copy = *edit;
    copy->before = copy_names(edit->before, edit->before_count);
    copy->after = copy_names(edit->after, edit->after_count);
    apush(history->undo, ((scene_edit){ .entity = entity, .group = history->group, .behaviors = copy }));
    clear_edits(&history->redo);
    return true;
}

const scene_behavior_edit *scene_history_peek_behaviors(
    const scene_history *history,
    bool redo,
    ecs_entity *entity
)
{
    assert(history);
    const scene_edit *source = redo ? history->redo : history->undo;
    if (!acount(source) || !alast(source).behaviors) {
        return NULL;
    }
    if (entity) {
        *entity = alast(source).entity;
    }
    return alast(source).behaviors;
}

void scene_history_complete_behaviors(scene_history *history, bool redo)
{
    assert(history);
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    assert(acount(*source) && alast(*source).behaviors);
    scene_edit edit = alast(*source);
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
}

bool scene_history_push_component(scene_history *history, ecs_entity entity, const scene_component_edit *edit)
{
    assert(history && entity.slot && edit);
    if (!edit->component || !edit->state_json || !edit->state_json[0]) {
        return false;
    }
    scene_component_edit *copy = xmalloc(sizeof *copy);
    *copy = *edit;
    copy->state_json = xstrdup(edit->state_json);
    apush(history->undo, ((scene_edit){ .entity = entity, .group = history->group, .component = copy }));
    clear_edits(&history->redo);
    return true;
}

const scene_component_edit *scene_history_peek_component(
    const scene_history *history,
    bool redo,
    ecs_entity *entity,
    bool *present
)
{
    assert(history);
    const scene_edit *source = redo ? history->redo : history->undo;
    if (!acount(source) || !alast(source).component) {
        return NULL;
    }
    const scene_edit *edit = &alast(source);
    if (entity) {
        *entity = edit->entity;
    }
    if (present) {
        *present = edit->component->added ? redo : !redo;
    }
    return edit->component;
}

void scene_history_complete_component(scene_history *history, bool redo)
{
    assert(history);
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    assert(acount(*source) && alast(*source).component);
    scene_edit edit = alast(*source);
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
}

bool scene_history_can_undo(const scene_history *history)
{
    assert(history);
    return acount(history->undo) > 0;
}

bool scene_history_can_redo(const scene_history *history)
{
    assert(history);
    return acount(history->redo) > 0;
}

static int apply_edit(scene_history *history, ecs_world *world, ecs_entity *out_entity, bool redo)
{
    assert(history && world);
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    if (!acount(*source)) {
        return 0;
    }
    scene_edit edit = alast(*source);
    if (edit.actor || edit.behaviors || edit.component ||
        !ecs_entity_is_alive(world, edit.entity)) {
        return -1;
    }
    if (edit.fields) {
        if (!valid_fields(world, &edit)) {
            return -1;
        }
        for (int i = 0; i < acount(edit.fields); i++) {
            scene_field_edit *field = &edit.fields[i];
            int result = ecs_component_write_field(
                world,
                edit.entity,
                field->component,
                field->field,
                redo ? field->after : field->before
            );
            assert(result == ECS_OK);
        }
    } else {
        GameTransform *transform = GAME_GameTransform_get(world, edit.entity);
        if (!transform) {
            return -1;
        }
        *transform = redo ? edit.after : edit.before;
    }
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
    if (out_entity) {
        *out_entity = edit.entity;
    }
    return 1;
}

int scene_history_undo(scene_history *history, ecs_world *world, ecs_entity *out_entity)
{
    return apply_edit(history, world, out_entity, false);
}

int scene_history_redo(scene_history *history, ecs_world *world, ecs_entity *out_entity)
{
    return apply_edit(history, world, out_entity, true);
}
