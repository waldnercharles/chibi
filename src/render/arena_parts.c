#include "render/arena_parts.h"

#include "third_party/yyjson/yyjson.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool vector(yyjson_val *value, CF_V3 *out, bool required)
{
    if (!value) {
        return !required;
    }
    if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 3) {
        return false;
    }
    float *components = &out->x;
    for (int i = 0; i < 3; i++) {
        yyjson_val *number = yyjson_arr_get(value, (size_t)i);
        if (!yyjson_is_num(number)) {
            return false;
        }
        components[i] = (float)yyjson_get_num(number);
        if (!isfinite(components[i])) {
            return false;
        }
    }
    return true;
}

static bool valid(const arena_part *part)
{
    return part->primitive >= GAME_PRIMITIVE_BOX &&
           part->primitive < GAME_PRIMITIVE_COUNT && part->dimensions.x > 0 &&
           part->dimensions.y > 0 && part->dimensions.z > 0 &&
           isfinite(
               part->dimensions.x + part->dimensions.y + part->dimensions.z +
               part->position.x + part->position.y + part->position.z +
               part->rotation.x + part->rotation.y + part->rotation.z
           );
}

bool render_arena_parts_parse(const char *json, arena_parts *parts, char *error, size_t size)
{
    *parts = (arena_parts){ 0 };
    if (size) {
        error[0] = 0;
    }
    if (!json || !*json) {
        return true;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_arr(root) || yyjson_arr_size(root) > ARENA_PART_LIMIT) {
        snprintf(error, size, "Mesh Parts needs an array of at most %d parts.", ARENA_PART_LIMIT);
        yyjson_doc_free(doc);
        return false;
    }
    size_t index, count;
    yyjson_val *item;
    yyjson_arr_foreach(root, index, count, item)
    {
        arena_part *part = &parts->parts[parts->count];
        yyjson_val *primitive = yyjson_obj_get(item, "primitive");
        const char *material = yyjson_get_str(yyjson_obj_get(item, "material"));
        int64_t primitive_value = yyjson_get_sint(primitive);
        part->primitive = yyjson_is_int(primitive) && primitive_value >= 0 &&
                                  primitive_value < GAME_PRIMITIVE_COUNT
                            ? (int)primitive_value
                            : -1;
        if (!yyjson_is_obj(item) ||
            !vector(yyjson_obj_get(item, "size"), &part->dimensions, true) ||
            !vector(yyjson_obj_get(item, "position"), &part->position, false) ||
            !vector(yyjson_obj_get(item, "rotation"), &part->rotation, false) ||
            !valid(part) || (material && strlen(material) >= sizeof part->material) ||
            (yyjson_obj_get(item, "material") && !material)) {
            snprintf(error, size, "Mesh part %zu needs a valid primitive, positive size and finite transforms.", index + 1);
            yyjson_doc_free(doc);
            *parts = (arena_parts){ 0 };
            return false;
        }
        snprintf(part->material, sizeof part->material, "%s", material ? material : "");
        parts->count++;
    }
    yyjson_doc_free(doc);
    return true;
}

static void write_vector(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, CF_V3 value)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_real(doc, array, value.x);
    yyjson_mut_arr_add_real(doc, array, value.y);
    yyjson_mut_arr_add_real(doc, array, value.z);
    yyjson_mut_obj_add_val(doc, object, key, array);
}

char *render_arena_parts_write(const arena_parts *parts)
{
    if (parts->count < 0 || parts->count > ARENA_PART_LIMIT) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (int i = 0; i < parts->count; i++) {
        const arena_part *part = &parts->parts[i];
        if (!valid(part) || !memchr(part->material, 0, sizeof part->material)) {
            yyjson_mut_doc_free(doc);
            return NULL;
        }
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_int(doc, object, "primitive", part->primitive);
        write_vector(doc, object, "size", part->dimensions);
        write_vector(doc, object, "position", part->position);
        write_vector(doc, object, "rotation", part->rotation);
        if (part->material[0]) {
            yyjson_mut_obj_add_strcpy(doc, object, "material", part->material);
        }
        yyjson_mut_arr_append(root, object);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}
