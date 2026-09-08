#ifndef CBLUEPRINT_CHECKPOINT_H
#define CBLUEPRINT_CHECKPOINT_H

#include "../third_party/yyjson/yyjson.h"
#include "runtime/engine.h"
#include "support/array.h"

#include <math.h>
#include <string.h>

/* Checkpoint JSON is owned across module generations. No process pointers or
 * structure images cross this boundary. */
static inline void checkpoint_text(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const char *text
)
{
    yyjson_mut_obj_add_val(doc, object, key, text ? yyjson_mut_strcpy(doc, text) : yyjson_mut_null(doc));
}

static inline char *checkpoint_copy_text(yyjson_val *object, const char *key)
{
    const char *text = yyjson_get_str(yyjson_obj_get(object, key));
    return text ? xstrdup(text) : NULL;
}

static inline yyjson_mut_val *checkpoint_strings(yyjson_mut_doc *doc, char *const *strings)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(strings); i++) {
        yyjson_mut_arr_add_strcpy(doc, array, strings[i]);
    }
    return array;
}

static inline bool checkpoint_read_strings(yyjson_val *array, char ***strings)
{
    if (!yyjson_is_arr(array)) {
        return false;
    }
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(array, i, count, value)
    {
        if (!yyjson_is_str(value)) {
            return false;
        }
        apush(*strings, xstrdup(yyjson_get_str(value)));
    }
    return true;
}

static inline void checkpoint_free_strings(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

static inline yyjson_mut_val *checkpoint_camera(yyjson_mut_doc *doc, engine_viewport_view view)
{
    double values[] = { view.center_x, view.center_y, view.width, view.height };
    return yyjson_mut_arr_with_real(doc, values, 4);
}

static inline bool checkpoint_read_camera(yyjson_val *array, engine_viewport_view *view)
{
    if (!yyjson_is_arr(array) || yyjson_arr_size(array) != 4) {
        return false;
    }
    float values[4];
    for (int i = 0; i < 4; i++) {
        yyjson_val *value = yyjson_arr_get(array, i);
        if (!yyjson_is_num(value) || !isfinite(yyjson_get_num(value))) {
            return false;
        }
        values[i] = (float)yyjson_get_num(value);
        if (!isfinite(values[i])) {
            return false;
        }
    }
    *view = (engine_viewport_view){ values[0], values[1], values[2], values[3] };
    return true;
}

/* Unmapped old handles remain tombstones, in a generation that cannot name a
 * candidate actor. Distinct deleted actors retain distinct resurrection keys. */
static inline ecs_entity checkpoint_entity(
    uint64_t old,
    const uint64_t *before,
    const uint64_t *after,
    int count
)
{
    for (int i = 0; i < count; i++) {
        if (old == before[i]) {
            return ecs_entity_from_u64(after[i]);
        }
    }
    ecs_entity entity = ecs_entity_from_u64(old);
    if (entity.slot) {
        entity.generation = UINT32_MAX;
    }
    return entity;
}

#endif
