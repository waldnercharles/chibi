#include "assets/scenario_document.h"

#include "graph/model/registry.h"
#include "runtime/components.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENARIO_FRAMES_MAX 108000
#define CONTROL_ENUM "GameControl"

static bool fail(char *error, size_t error_size, const char *message)
{
    snprintf(error, error_size, "%s", message);
    return false;
}

static bool read_vector(yyjson_val *value, float limit, CF_V2 *out)
{
    yyjson_val *x = yyjson_obj_get(value, "x"), *y = yyjson_obj_get(value, "y");
    if (!yyjson_is_obj(value) || yyjson_obj_size(value) != 2 ||
        !yyjson_is_num(x) || !yyjson_is_num(y)) {
        return false;
    }
    double vx = yyjson_get_num(x), vy = yyjson_get_num(y);
    if (!isfinite(vx) || !isfinite(vy) || fabs(vx) > limit || fabs(vy) > limit) {
        return false;
    }
    *out = (CF_V2){ (float)vx, (float)vy };
    return true;
}

static bool read_controls(yyjson_val *value, uint64_t *out)
{
    if (!yyjson_is_arr(value)) {
        return false;
    }
    size_t i, count;
    yyjson_val *label;
    yyjson_arr_foreach(value, i, count, label)
    {
        int control;
        if (!yyjson_is_str(label) ||
            !registry_enum_value(CONTROL_ENUM, yyjson_get_str(label), &control) ||
            control <= GAME_CONTROL_NONE || control >= GAME_CONTROL_COUNT) {
            return false;
        }
        *out |= UINT64_C(1) << control;
    }
    return true;
}

static bool read_flag(yyjson_val *value, bool *out)
{
    *out = yyjson_get_bool(value);
    return yyjson_is_bool(value);
}

static bool read_field(const char *name, yyjson_val *value, input_keyframe *out)
{
    if (!strcmp(name, "frame")) {
        return true;
    }
    if (!strcmp(name, "movement")) {
        return read_vector(value, 1, &out->movement);
    }
    if (!strcmp(name, "aim")) {
        return read_vector(value, FLT_MAX, &out->aim);
    }
    if (!strcmp(name, "aim_point")) {
        return read_flag(value, &out->aim_point);
    }
    if (!strcmp(name, "firing")) {
        return read_flag(value, &out->firing);
    }
    return !strcmp(name, "controls") && read_controls(value, &out->controls);
}

static bool read_key(yyjson_val *value, int frames, input_keyframe *out, char *error, size_t error_size)
{
    if (!yyjson_is_obj(value)) {
        return fail(error, error_size, "each key must be a mapping");
    }
    yyjson_val *frame = yyjson_obj_get(value, "frame");
    if (!yyjson_is_uint(frame) || yyjson_get_uint(frame) >= (uint64_t)frames) {
        return fail(error, error_size, "each key needs a frame below the scenario's frames");
    }
    *out = (input_keyframe){ .step = (int)yyjson_get_uint(frame) };
    size_t i, count;
    yyjson_val *name, *field;
    yyjson_obj_foreach(value, i, count, name, field)
    {
        if (!read_field(yyjson_get_str(name), field, out)) {
            snprintf(
                error,
                error_size,
                "key field %s is unknown or invalid: movement is within -1..1, "
                "aim is finite, "
                "aim_point and firing are booleans, controls are GameControl "
                "labels",
                yyjson_get_str(name)
            );
            return false;
        }
    }
    return true;
}

static bool read_document(yyjson_val *root, scenario_document *out, char *error, size_t error_size)
{
    yyjson_val *scene = yyjson_obj_get(root, "scene");
    yyjson_val *frames = yyjson_obj_get(root, "frames");
    yyjson_val *seed = yyjson_obj_get(root, "seed");
    yyjson_val *keys = yyjson_obj_get(root, "keys");
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 3 + (seed != NULL)) {
        return fail(error, error_size, "a scenario has scene, frames and keys, and may have a seed");
    }
    if (!yyjson_is_str(scene) || !yyjson_get_len(scene)) {
        return fail(error, error_size, "scene must name a scene");
    }
    if (!yyjson_is_uint(frames) || !yyjson_get_uint(frames) ||
        yyjson_get_uint(frames) > SCENARIO_FRAMES_MAX) {
        return fail(error, error_size, "frames must be between 1 and 108000");
    }
    if (seed && (!yyjson_is_uint(seed) || yyjson_get_uint(seed) > UINT32_MAX)) {
        return fail(error, error_size, "seed must be an unsigned 32-bit integer");
    }
    if (!yyjson_is_arr(keys)) {
        return fail(error, error_size, "keys must be a list");
    }
    out->scene = xstrdup(yyjson_get_str(scene));
    out->frames = (int)yyjson_get_uint(frames);
    out->seed = seed ? (unsigned)yyjson_get_uint(seed) : 0;
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(keys, i, count, value)
    {
        input_keyframe key;
        if (!read_key(value, out->frames, &key, error, error_size)) {
            return false;
        }
        int previous = acount(out->keys) - 1;
        if (previous >= 0 && key.step <= out->keys[previous].step) {
            return fail(error, error_size, "key frames must increase");
        }
        if (previous < 0 && key.step > 0) {
            apush(out->keys, ((input_keyframe){ 0 }));
        }
        apush(out->keys, key);
    }
    if (!acount(out->keys)) {
        apush(out->keys, ((input_keyframe){ 0 }));
    }
    return true;
}

scenario_document *scenario_document_load(const char *json, char *error, size_t error_size)
{
    yyjson_doc *doc = asset_text_read(json, error, error_size);
    if (!doc) {
        return NULL;
    }
    scenario_document *document = xcalloc(1, sizeof *document);
    bool ok = read_document(yyjson_doc_get_root(doc), document, error, error_size);
    yyjson_doc_free(doc);
    if (!ok) {
        scenario_document_free(document);
        return NULL;
    }
    return document;
}

static void write_vector(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *name, CF_V2 value)
{
    if (value.x == 0 && value.y == 0) {
        return;
    }
    yyjson_mut_val *vector = yyjson_mut_obj_add_obj(doc, object, name);
    yyjson_mut_obj_add_val(doc, vector, "x", yyjson_mut_float(doc, value.x));
    yyjson_mut_obj_add_val(doc, vector, "y", yyjson_mut_float(doc, value.y));
}

static void write_key(yyjson_mut_doc *doc, yyjson_mut_val *keys, const input_keyframe *key)
{
    yyjson_mut_val *value = yyjson_mut_arr_add_obj(doc, keys);
    yyjson_mut_obj_add_int(doc, value, "frame", key->step);
    write_vector(doc, value, "movement", key->movement);
    write_vector(doc, value, "aim", key->aim);
    if (key->aim_point) {
        yyjson_mut_obj_add_bool(doc, value, "aim_point", true);
    }
    if (key->firing) {
        yyjson_mut_obj_add_bool(doc, value, "firing", true);
    }
    if (!key->controls) {
        return;
    }
    yyjson_mut_val *controls = yyjson_mut_obj_add_arr(doc, value, "controls");
    for (int control = GAME_CONTROL_NONE + 1; control < GAME_CONTROL_COUNT; control++) {
        if (key->controls & (UINT64_C(1) << control)) {
            yyjson_mut_arr_add_str(doc, controls, registry_enum_label(CONTROL_ENUM, control));
        }
    }
}

char *scenario_document_save(const scenario_document *document)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "scene", document->scene);
    yyjson_mut_obj_add_int(doc, root, "frames", document->frames);
    if (document->seed) {
        yyjson_mut_obj_add_uint(doc, root, "seed", document->seed);
    }
    yyjson_mut_val *keys = yyjson_mut_obj_add_arr(doc, root, "keys");
    for (int i = 0; i < acount(document->keys); i++) {
        write_key(doc, keys, &document->keys[i]);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

void scenario_document_free(scenario_document *document)
{
    if (!document) {
        return;
    }
    free(document->scene);
    afree(document->keys);
    free(document);
}
