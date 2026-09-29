#include "editor/documents/scene_history.h"

#include "assets/physics_settings.h"
#include "runtime/generated.h"
#include "support/array.h"

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
    bool physics;
    game_physics_settings physics_before, physics_after;
    char *name_before, *name_after; /* a rename, when name_after is set */
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
            if (fields[j].before.type == PIN_STR || fields[j].before.type == PIN_NAME) {
                free((void *)fields[j].before.str);
                free((void *)fields[j].after.str);
            }
        }
        afree(fields);
        scene_actor_edit *actor = (*edits)[i].actor;
        if (actor) {
            free((void *)actor->name);
            free((void *)actor->prototype_name);
            free((void *)actor->base_json);
            free((void *)actor->state_json);
            free((void *)actor->retained_json);
            free(actor);
        }
        scene_behavior_edit *behaviors = (*edits)[i].behaviors;
        if (behaviors) {
            free_names(behaviors->before, behaviors->before_count);
            free_names(behaviors->after, behaviors->after_count);
            free(behaviors);
        }
        free((*edits)[i].name_before);
        free((*edits)[i].name_after);
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
        case PIN_STR:
        case PIN_NAME:
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
        case PIN_STR:
        case PIN_NAME: return !strcmp(a.str ? a.str : "", b.str ? b.str : "");
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
        if (field.before.type == PIN_STR || field.before.type == PIN_NAME) {
            field.before.str = xstrdup(field.before.str ? field.before.str : "");
            field.after.str = xstrdup(field.after.str ? field.after.str : "");
        }
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
    copy->retained_json = xstrdup(actor->retained_json ? actor->retained_json : "{}");
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

bool scene_history_push_rename(scene_history *history, ecs_entity entity, const char *before, const char *after)
{
    assert(history && entity.slot && before && before[0] && after && after[0]);
    if (!strcmp(before, after)) {
        return false;
    }
    apush(
        history->undo,
        ((scene_edit){ .entity = entity,
                       .group = history->group,
                       .name_before = xstrdup(before),
                       .name_after = xstrdup(after) })
    );
    clear_edits(&history->redo);
    return true;
}

const char *scene_history_peek_rename(const scene_history *history, bool redo, ecs_entity *entity)
{
    assert(history);
    const scene_edit *source = redo ? history->redo : history->undo;
    if (!acount(source) || !alast(source).name_after) {
        return NULL;
    }
    *entity = alast(source).entity;
    return redo ? alast(source).name_after : alast(source).name_before;
}

void scene_history_complete_rename(scene_history *history, bool redo)
{
    assert(history);
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    assert(acount(*source) && alast(*source).name_after);
    scene_edit edit = alast(*source);
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
}

bool scene_history_can_undo(const scene_history *history)
{
    assert(history);
    return acount(history->undo) > 0;
}

bool scene_history_push_physics(scene_history *history, game_physics_settings before, game_physics_settings after)
{
    assert(game_physics_settings_valid(before) && game_physics_settings_valid(after));
    if (game_physics_settings_equal(before, after)) {
        return false;
    }
    apush(
        history->undo,
        ((scene_edit){ .physics = true,
                       .group = history->group,
                       .physics_before = before,
                       .physics_after = after })
    );
    clear_edits(&history->redo);
    return true;
}

const game_physics_settings *scene_history_peek_physics(const scene_history *history, bool redo)
{
    const scene_edit *edits = redo ? history->redo : history->undo;
    if (!acount(edits) || !alast(edits).physics) {
        return NULL;
    }
    return redo ? &alast(edits).physics_after : &alast(edits).physics_before;
}

void scene_history_complete_physics(scene_history *history, bool redo)
{
    scene_edit **source = redo ? &history->redo : &history->undo;
    scene_edit **destination = redo ? &history->undo : &history->redo;
    assert(acount(*source) && alast(*source).physics);
    scene_edit edit = alast(*source);
    asetlen(*source, acount(*source) - 1);
    apush(*destination, edit);
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
    if (edit.actor || edit.behaviors || edit.physics || edit.name_after ||
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

/* History is translated through reflected names so a reordered component or
 * field cannot silently retarget an Undo after native code reload. */
#include "editor/workspace/checkpoint.h"

static const ecs_component_desc *history_component(const char *name)
{
    for (int i = 0; name && i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (!strcmp(name, GAME_REGISTRY_COMPONENTS[i]->name)) {
            return GAME_REGISTRY_COMPONENTS[i];
        }
    }
    return NULL;
}

static yyjson_mut_val *history_value(yyjson_mut_doc *doc, pin_value value)
{
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, object, "type", value.type);
    checkpoint_text(doc, object, "name", value.type_name);
    switch (value.type) {
        case PIN_BOOL:
            yyjson_mut_obj_add_bool(doc, object, "value", value.b);
            break;
        case PIN_STR:
        case PIN_NAME:
            checkpoint_text(doc, object, "value", value.str ? value.str : "");
            break;
        case PIN_I8:
            yyjson_mut_obj_add_int(doc, object, "value", value.i8);
            break;
        case PIN_I16:
            yyjson_mut_obj_add_int(doc, object, "value", value.i16);
            break;
        case PIN_I32:
            yyjson_mut_obj_add_int(doc, object, "value", value.i);
            break;
        case PIN_I64:
            yyjson_mut_obj_add_int(doc, object, "value", value.i64);
            break;
        case PIN_ENUM:
            yyjson_mut_obj_add_int(doc, object, "value", value.e);
            break;
        case PIN_U8:
            yyjson_mut_obj_add_uint(doc, object, "value", value.u8);
            break;
        case PIN_U16:
            yyjson_mut_obj_add_uint(doc, object, "value", value.u16);
            break;
        case PIN_U32:
            yyjson_mut_obj_add_uint(doc, object, "value", value.u);
            break;
        case PIN_U64:
        case PIN_HANDLE:
            yyjson_mut_obj_add_uint(doc, object, "value", value.u64);
            break;
        case PIN_F32:
            yyjson_mut_obj_add_real(doc, object, "value", value.f);
            break;
        case PIN_F64:
            yyjson_mut_obj_add_real(doc, object, "value", value.d);
            break;
        default: assert(false);
    }
    return object;
}

static bool history_read_value(yyjson_val *object, pin_value *out)
{
    yyjson_val *type = yyjson_obj_get(object, "type"),
               *value = yyjson_obj_get(object, "value");
    if (!yyjson_is_int(type)) {
        return false;
    }
    out->type = yyjson_get_int(type);
    out->type_name = checkpoint_copy_text(object, "name");
    if (out->type == PIN_STR || out->type == PIN_NAME) {
        if (!yyjson_is_str(value)) {
            return false;
        }
        out->str = xstrdup(yyjson_get_str(value));
        return true;
    }
    if (out->type == PIN_BOOL) {
        if (!yyjson_is_bool(value)) {
            return false;
        }
        out->b = yyjson_get_bool(value);
        return true;
    }
    if (!yyjson_is_num(value)) {
        return false;
    }
    bool signed_number = yyjson_is_int(value) &&
                         (!yyjson_is_uint(value) || yyjson_get_uint(value) <= INT64_MAX);
    bool unsigned_number = yyjson_is_uint(value);
    int64_t signed_value = yyjson_get_sint(value);
    uint64_t unsigned_value = yyjson_get_uint(value);
    switch (out->type) {
        case PIN_I8:
            if (!signed_number || signed_value < INT8_MIN || signed_value > INT8_MAX) {
                return false;
            }
            out->i8 = signed_value;
            break;
        case PIN_I16:
            if (!signed_number || signed_value < INT16_MIN || signed_value > INT16_MAX) {
                return false;
            }
            out->i16 = signed_value;
            break;
        case PIN_I32:
            if (!signed_number || signed_value < INT32_MIN || signed_value > INT32_MAX) {
                return false;
            }
            out->i = signed_value;
            break;
        case PIN_I64:
            if (!signed_number || signed_value < INT64_MIN || signed_value > INT64_MAX) {
                return false;
            }
            out->i64 = signed_value;
            break;
        case PIN_ENUM:
            if (!signed_number || signed_value < INT32_MIN || signed_value > INT32_MAX) {
                return false;
            }
            out->e = signed_value;
            break;
        case PIN_U8:
            if (!unsigned_number || unsigned_value > UINT8_MAX) {
                return false;
            }
            out->u8 = unsigned_value;
            break;
        case PIN_U16:
            if (!unsigned_number || unsigned_value > UINT16_MAX) {
                return false;
            }
            out->u16 = unsigned_value;
            break;
        case PIN_U32:
            if (!unsigned_number || unsigned_value > UINT32_MAX) {
                return false;
            }
            out->u = unsigned_value;
            break;
        case PIN_U64:
        case PIN_HANDLE:
            if (!unsigned_number) {
                return false;
            }
            out->u64 = unsigned_value;
            break;
        case PIN_F32: out->f = yyjson_get_num(value); break;
        case PIN_F64: out->d = yyjson_get_num(value); break;
        default: return false;
    }
    return scalar(*out);
}

static yyjson_mut_val *history_edits(yyjson_mut_doc *doc, const scene_edit *edits)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(edits); i++) {
        const scene_edit *edit = &edits[i];
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        yyjson_mut_arr_append(array, object);
        yyjson_mut_obj_add_uint(doc, object, "entity", ecs_entity_to_u64(edit->entity));
        yyjson_mut_obj_add_uint(doc, object, "group", edit->group);
        if (edit->physics) {
            yyjson_mut_obj_add_val(
                doc,
                object,
                "physics_before",
                game_physics_settings_write(doc, edit->physics_before)
            );
            yyjson_mut_obj_add_val(
                doc,
                object,
                "physics_after",
                game_physics_settings_write(doc, edit->physics_after)
            );
        }
        double positions[] = { edit->before.x,
                               edit->before.y,
                               edit->after.x,
                               edit->after.y };
        yyjson_mut_obj_add_val(doc, object, "position", yyjson_mut_arr_with_real(doc, positions, 4));
        yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
        for (int j = 0; j < acount(edit->fields); j++) {
            const scene_field_edit *field = &edit->fields[j];
            const ecs_component_desc *component = NULL;
            for (int k = 0; k < GAME_REGISTRY_COMPONENT_COUNT; k++) {
                if (GAME_REGISTRY_COMPONENTS[k]->id == field->component) {
                    component = GAME_REGISTRY_COMPONENTS[k];
                }
            }
            assert(
                component && component->type && field->field < component->type->field_count
            );
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, fields);
            checkpoint_text(doc, item, "component", component->name);
            checkpoint_text(
                doc,
                item,
                "field",
                component->type->fields[field->field].name
            );
            yyjson_mut_obj_add_val(doc, item, "before", history_value(doc, field->before));
            yyjson_mut_obj_add_val(doc, item, "after", history_value(doc, field->after));
        }
        if (edit->actor) {
            const scene_actor_edit *actor = edit->actor;
            yyjson_mut_val *item = yyjson_mut_obj_add_obj(doc, object, "actor");
            if (actor->identity) {
                yyjson_mut_obj_add_uint(doc, item, "identity", actor->identity);
            }
            yyjson_mut_obj_add_int(doc, item, "index", actor->index);
            yyjson_mut_obj_add_bool(doc, item, "created", actor->created);
            checkpoint_text(doc, item, "name", actor->name);
            checkpoint_text(doc, item, "prototype", actor->prototype_name);
            checkpoint_text(doc, item, "base", actor->base_json);
            checkpoint_text(doc, item, "state", actor->state_json);
            checkpoint_text(doc, item, "retained", actor->retained_json);
        }
        if (edit->behaviors) {
            yyjson_mut_val *item = yyjson_mut_obj_add_obj(doc, object, "behaviors");
            yyjson_mut_val *before = yyjson_mut_obj_add_arr(doc, item, "before");
            yyjson_mut_val *after = yyjson_mut_obj_add_arr(doc, item, "after");
            for (int j = 0; j < edit->behaviors->before_count; j++) {
                yyjson_mut_arr_add_strcpy(doc, before, edit->behaviors->before[j]);
            }
            for (int j = 0; j < edit->behaviors->after_count; j++) {
                yyjson_mut_arr_add_strcpy(doc, after, edit->behaviors->after[j]);
            }
        }
        if (edit->name_after) {
            yyjson_mut_val *item = yyjson_mut_obj_add_obj(doc, object, "rename");
            checkpoint_text(doc, item, "before", edit->name_before);
            checkpoint_text(doc, item, "after", edit->name_after);
        }
    }
    return array;
}

char *scene_history_checkpoint(const scene_history *history)
{
    assert(!history->group);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_obj_add_uint(doc, root, "next_group", history->next_group);
    yyjson_mut_obj_add_val(doc, root, "undo", history_edits(doc, history->undo));
    yyjson_mut_obj_add_val(doc, root, "redo", history_edits(doc, history->redo));
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static bool history_read_edits(yyjson_val *array, scene_edit **edits, uint64_t **before, uint64_t **after)
{
    if (!yyjson_is_arr(array)) {
        return false;
    }
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(array, i, count, object)
    {
        if (!yyjson_is_obj(object) ||
            !yyjson_is_uint(yyjson_obj_get(object, "entity"))) {
            return false;
        }
        scene_edit edit = { 0 };
        apush(*edits, edit);
        scene_edit *out = &alast(*edits);
        uint64_t old = yyjson_get_uint(yyjson_obj_get(object, "entity"));
        int mapping = 0;
        while (mapping < acount(*before) && (*before)[mapping] != old) {
            mapping++;
        }
        if (mapping == acount(*before)) {
            apush(*before, old);
            apush(*after, ecs_entity_to_u64((ecs_entity){ .slot = mapping + 1, .generation = UINT32_MAX }));
        }
        out->entity = ecs_entity_from_u64((*after)[mapping]);
        out->group = yyjson_get_uint(yyjson_obj_get(object, "group"));
        yyjson_val *physics = yyjson_obj_get(object, "physics_before");
        if (physics) {
            yyjson_val *next = yyjson_obj_get(object, "physics_after");
            if (!next || !game_physics_settings_read(physics, &out->physics_before) ||
                !game_physics_settings_read(next, &out->physics_after)) {
                return false;
            }
            out->entity = (ecs_entity){ 0 };
            out->physics = true;
        }
        engine_viewport_view position;
        if (!checkpoint_read_camera(yyjson_obj_get(object, "position"), &position)) {
            return false;
        }
        out->before = (GameTransform){ position.center_x, position.center_y };
        out->after = (GameTransform){ position.width, position.height };
        yyjson_val *fields = yyjson_obj_get(object, "fields");
        if (!yyjson_is_arr(fields)) {
            return false;
        }
        size_t j, field_count;
        yyjson_val *item;
        yyjson_arr_foreach(fields, j, field_count, item)
        {
            const ecs_component_desc *component = history_component(
                yyjson_get_str(yyjson_obj_get(item, "component"))
            );
            const char *name = yyjson_get_str(yyjson_obj_get(item, "field"));
            if (!component || !component->type || !name) {
                return false;
            }
            int index = 0;
            while (index < component->type->field_count &&
                   strcmp(component->type->fields[index].name, name)) {
                index++;
            }
            if (index == component->type->field_count) {
                return false;
            }
            scene_field_edit field = { .component = component->id, .field = index };
            apush(out->fields, field);
            scene_field_edit *stored = &alast(out->fields);
            if (!history_read_value(yyjson_obj_get(item, "before"), &stored->before) ||
                !history_read_value(yyjson_obj_get(item, "after"), &stored->after)) {
                return false;
            }
            const type_field *type = &component->type->fields[index];
            pin_value expected = { .type = type->type, .type_name = type->type_name };
            if (!same_type(expected, stored->before) ||
                !same_type(expected, stored->after)) {
                return false;
            }
        }
        item = yyjson_obj_get(object, "actor");
        if (item) {
            out->actor = xcalloc(1, sizeof *out->actor);
            scene_actor_edit *actor = out->actor;
            yyjson_val *identity = yyjson_obj_get(item, "identity");
            if (identity && !yyjson_is_uint(identity)) {
                return false;
            }
            actor->identity = yyjson_get_uint(identity);
            actor->index = yyjson_get_int(yyjson_obj_get(item, "index"));
            actor->created = yyjson_get_bool(yyjson_obj_get(item, "created"));
            actor->name = checkpoint_copy_text(item, "name");
            actor->prototype_name = checkpoint_copy_text(item, "prototype");
            actor->base_json = checkpoint_copy_text(item, "base");
            actor->state_json = checkpoint_copy_text(item, "state");
            actor->retained_json = checkpoint_copy_text(item, "retained");
            if (actor->index < 0 || !actor->name || !actor->prototype_name ||
                !actor->base_json || !actor->state_json) {
                return false;
            }
        }
        item = yyjson_obj_get(object, "behaviors");
        if (item) {
            char **previous = NULL, **next = NULL;
            bool valid = checkpoint_read_strings(yyjson_obj_get(item, "before"), &previous) &&
                         checkpoint_read_strings(yyjson_obj_get(item, "after"), &next);
            out->behaviors = xcalloc(1, sizeof *out->behaviors);
            *out->behaviors = (scene_behavior_edit){
                copy_names((const char *const *)previous, acount(previous)),
                acount(previous),
                copy_names((const char *const *)next, acount(next)),
                acount(next)
            };
            checkpoint_free_strings(previous);
            checkpoint_free_strings(next);
            if (!valid) {
                return false;
            }
        }
        item = yyjson_obj_get(object, "rename");
        if (item) {
            out->name_before = checkpoint_copy_text(item, "before");
            out->name_after = checkpoint_copy_text(item, "after");
            if (!out->name_before || !out->name_after) {
                return false;
            }
        }
    }
    return true;
}

bool scene_history_restore_checkpoint(
    scene_history *history,
    const char *json,
    const uint64_t *old_entities,
    const uint64_t *new_entities,
    int count
)
{
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1) {
        yyjson_doc_free(doc);
        return false;
    }
    scene_history candidate = {
        .next_group = yyjson_get_uint(yyjson_obj_get(root, "next_group"))
    };
    uint64_t *before = NULL, *after = NULL;
    for (int i = 0; i < count; i++) {
        apush(before, old_entities[i]);
        apush(after, new_entities[i]);
    }
    bool valid = history_read_edits(yyjson_obj_get(root, "undo"), &candidate.undo, &before, &after) &&
                 history_read_edits(yyjson_obj_get(root, "redo"), &candidate.redo, &before, &after);
    afree(before);
    afree(after);
    yyjson_doc_free(doc);
    if (valid) {
        scene_history previous = *history;
        *history = candidate;
        candidate = previous;
    }
    clear_edits(&candidate.undo);
    clear_edits(&candidate.redo);
    afree(candidate.undo);
    afree(candidate.redo);
    return valid;
}
