#include "authoring/parts_prepare.h"

#include "support/asset_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool vector(av_value *value, CF_V3 *out, bool required)
{
    if (!value) {
        return !required;
    }
    if (!av_is_arr(value) || av_arr_size(value) != 3) {
        return false;
    }
    float *components = &out->x;
    for (int i = 0; i < 3; i++) {
        av_value *number = av_arr_get(value, (size_t)i);
        if (!av_is_num(number)) {
            return false;
        }
        components[i] = (float)av_get_num(number);
        if (!isfinite(components[i])) {
            return false;
        }
    }
    return true;
}

static bool valid(const scene3d_part *part)
{
    return part->primitive >= PRIMITIVE_BOX &&
           part->primitive < PRIMITIVE_COUNT && part->dimensions.x > 0 &&
           part->dimensions.y > 0 && part->dimensions.z > 0 &&
           isfinite(
               part->dimensions.x + part->dimensions.y + part->dimensions.z +
               part->position.x + part->position.y + part->position.z +
               part->rotation.x + part->rotation.y + part->rotation.z
           );
}

bool authoring_parts_parse(const char *text, scene3d_parts *parts, char *error, size_t size)
{
    *parts = (scene3d_parts){ 0 };
    if (size) {
        error[0] = 0;
    }
    if (!text || !*text) {
        return true;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = av_doc_root(doc);
    if (!av_is_arr(root) || av_arr_size(root) > SCENE3D_PART_LIMIT) {
        snprintf(error, size, "Mesh Parts needs an array of at most %d parts.", SCENE3D_PART_LIMIT);
        av_doc_free(doc);
        return false;
    }
    size_t index, count;
    av_value *item;
    av_arr_foreach(root, index, count, item)
    {
        scene3d_part *part = &parts->parts[parts->count];
        av_value *primitive = av_obj_get(item, "primitive");
        const char *material = av_get_str(av_obj_get(item, "material"));
        int64_t primitive_value = av_get_sint(primitive);
        part->primitive = av_is_int(primitive) && primitive_value >= 0 &&
                                  primitive_value < PRIMITIVE_COUNT
                            ? (int)primitive_value
                            : -1;
        if (!av_is_obj(item) ||
            !vector(av_obj_get(item, "size"), &part->dimensions, true) ||
            !vector(av_obj_get(item, "position"), &part->position, false) ||
            !vector(av_obj_get(item, "rotation"), &part->rotation, false) ||
            !valid(part) || (material && strlen(material) >= sizeof part->material) ||
            (av_obj_get(item, "material") && !material)) {
            snprintf(error, size, "Mesh part %zu needs a valid primitive, positive size and finite transforms.", index + 1);
            av_doc_free(doc);
            *parts = (scene3d_parts){ 0 };
            return false;
        }
        snprintf(part->material, sizeof part->material, "%s", material ? material : "");
        parts->count++;
    }
    av_doc_free(doc);
    return true;
}

static void write_vector(av_doc *doc, av_value *object, const char *key, CF_V3 value)
{
    av_value *array = av_arr(doc);
    av_arr_append(array, av_real(doc, value.x));
    av_arr_append(array, av_real(doc, value.y));
    av_arr_append(array, av_real(doc, value.z));
    av_obj_add_val(doc, object, key, array);
}

char *authoring_parts_write(const scene3d_parts *parts)
{
    if (parts->count < 0 || parts->count > SCENE3D_PART_LIMIT) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_arr(doc);
    av_doc_set_root(doc, root);
    for (int i = 0; i < parts->count; i++) {
        const scene3d_part *part = &parts->parts[i];
        if (!valid(part) || !memchr(part->material, 0, sizeof part->material)) {
            av_doc_free(doc);
            return NULL;
        }
        av_value *object = av_obj(doc);
        av_obj_add_int(doc, object, "primitive", part->primitive);
        write_vector(doc, object, "size", part->dimensions);
        write_vector(doc, object, "position", part->position);
        write_vector(doc, object, "rotation", part->rotation);
        if (part->material[0]) {
            av_obj_add_strcpy(doc, object, "material", part->material);
        }
        av_arr_append(root, object);
    }
    char *text = av_value_write(av_doc_root(doc));
    av_doc_free(doc);
    return text;
}

bool authoring_parts_prepare(const char *text, char *error, size_t size)
{
    scene3d_parts parts;
    return authoring_parts_parse(text, &parts, error, size) &&
           render_scene3d_parts_publish(text, &parts, error, size);
}
