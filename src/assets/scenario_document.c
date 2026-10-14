#include "assets/scenario_document.h"

#include "runtime/components.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENARIO_FRAMES_MAX 108000
#define CONTROL_ENUM "Control"

static bool fail(char *error, size_t error_size, const char *message)
{
    snprintf(error, error_size, "%s", message);
    return false;
}

static bool read_vector(av_value *value, float limit, CF_V2 *out)
{
    av_value *x = av_obj_get(value, "x"), *y = av_obj_get(value, "y");
    if (!av_is_obj(value) || av_obj_size(value) != 2 || !av_is_num(x) ||
        !av_is_num(y)) {
        return false;
    }
    double vx = av_get_num(x), vy = av_get_num(y);
    if (!isfinite(vx) || !isfinite(vy) || fabs(vx) > limit || fabs(vy) > limit) {
        return false;
    }
    *out = (CF_V2){ (float)vx, (float)vy };
    return true;
}

static bool read_controls(av_value *value, uint64_t *out)
{
    if (!av_is_arr(value)) {
        return false;
    }
    size_t i, count;
    av_value *label;
    av_arr_foreach(value, i, count, label)
    {
        int control;
        if (!av_is_str(label) ||
            !type_catalog_enum_value(CONTROL_ENUM, av_get_str(label), &control) ||
            control <= CONTROL_NONE || control >= CONTROL_COUNT) {
            return false;
        }
        *out |= UINT64_C(1) << control;
    }
    return true;
}

static bool read_controllers(av_value *value, input_keyframe *out)
{
    if (!av_is_arr(value) || av_arr_size(value) > INPUT_CONTROLLER_MAX) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(value); i++) {
        av_value *state = av_arr_get(value, i);
        av_value *connected = av_obj_get(state, "connected");
        av_value *buttons = av_obj_get(state, "buttons");
        if (!av_is_obj(state) || av_obj_size(state) != 1 + (buttons != NULL) ||
            !av_is_bool(connected) || (buttons && !av_is_arr(buttons))) {
            return false;
        }
        out->controllers[i].connected = av_get_bool(connected);
        for (size_t j = 0; buttons && j < av_arr_size(buttons); j++) {
            av_value *label = av_arr_get(buttons, j);
            int button;
            if (!out->controllers[i].connected || !av_is_str(label) ||
                !type_catalog_enum_value("PadControl", av_get_str(label), &button) ||
                button <= PAD_NONE || button >= PAD_COUNT) {
                return false;
            }
            out->controllers[i].buttons |= UINT32_C(1) << button;
        }
    }
    return true;
}

static bool read_flag(av_value *value, bool *out)
{
    *out = av_get_bool(value);
    return av_is_bool(value);
}

static bool read_vector3(av_value *value, CF_V3 *out)
{
    av_value *x = av_obj_get(value, "x"), *y = av_obj_get(value, "y"),
             *z = av_obj_get(value, "z");
    if (!av_is_obj(value) || av_obj_size(value) != 3 || !av_is_num(x) ||
        !av_is_num(y) || !av_is_num(z)) {
        return false;
    }
    double vx = av_get_num(x), vy = av_get_num(y), vz = av_get_num(z);
    if (!isfinite(vx) || !isfinite(vy) || !isfinite(vz) || fabs(vx) > FLT_MAX ||
        fabs(vy) > FLT_MAX || fabs(vz) > FLT_MAX) {
        return false;
    }
    *out = (CF_V3){ (float)vx, (float)vy, (float)vz };
    return true;
}

static bool read_pointer(av_value *value, input_pointer_ray *out)
{
    if (!av_is_obj(value) || av_obj_size(value) != 2 ||
        !read_vector3(av_obj_get(value, "origin"), &out->origin) ||
        !read_vector3(av_obj_get(value, "direction"), &out->direction)) {
        return false;
    }
    CF_V3 d = out->direction;
    out->active = d.x || d.y || d.z;
    return out->active;
}

static bool read_field(const char *name, av_value *value, input_keyframe *out)
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
    if (!strcmp(name, "controllers")) {
        return read_controllers(value, out);
    }
    if (!strcmp(name, "pointer")) {
        return read_pointer(value, &out->pointer);
    }
    return !strcmp(name, "controls") && read_controls(value, &out->controls);
}

static bool read_key(av_value *value, int frames, input_keyframe *out, char *error, size_t error_size)
{
    if (!av_is_obj(value)) {
        return fail(error, error_size, "each key must be a mapping");
    }
    av_value *frame = av_obj_get(value, "frame");
    if (!av_is_uint(frame) || av_get_uint(frame) >= (uint64_t)frames) {
        return fail(error, error_size, "each key needs a frame below the scenario's frames");
    }
    *out = (input_keyframe){ .step = (int)av_get_uint(frame) };
    size_t i, count;
    av_value *name, *field;
    av_obj_foreach(value, i, count, name, field)
    {
        if (!read_field(av_get_str(name), field, out)) {
            snprintf(
                error,
                error_size,
                "key field %s is unknown or invalid: movement is within -1..1, "
                "aim is finite, "
                "aim_point and firing are booleans, controls are Control "
                "labels",
                av_get_str(name)
            );
            return false;
        }
    }
    return true;
}

static bool read_document(av_value *root, scenario_document *out, char *error, size_t error_size)
{
    av_value *scene = av_obj_get(root, "scene");
    av_value *frames = av_obj_get(root, "frames");
    av_value *seed = av_obj_get(root, "seed");
    av_value *saved_state = av_obj_get(root, "saved_state");
    av_value *keys = av_obj_get(root, "keys");
    if (!av_is_obj(root) ||
        av_obj_size(root) != 3 + (seed != NULL) + (saved_state != NULL)) {
        return fail(error, error_size, "a scenario has scene, frames and keys, and may have a seed and saved_state");
    }
    if (!av_is_str(scene) || !av_get_len(scene)) {
        return fail(error, error_size, "scene must name a scene");
    }
    if (!av_is_uint(frames) || !av_get_uint(frames) ||
        av_get_uint(frames) > SCENARIO_FRAMES_MAX) {
        return fail(error, error_size, "frames must be between 1 and 108000");
    }
    if (seed && (!av_is_uint(seed) || av_get_uint(seed) > UINT32_MAX)) {
        return fail(error, error_size, "seed must be an unsigned 32-bit integer");
    }
    if (saved_state && !av_is_obj(saved_state)) {
        return fail(error, error_size, "saved_state must be a mapping of committed save values");
    }
    if (!av_is_arr(keys)) {
        return fail(error, error_size, "keys must be a list");
    }
    out->scene = xstrdup(av_get_str(scene));
    out->frames = (int)av_get_uint(frames);
    out->seed = seed ? (unsigned)av_get_uint(seed) : 0;
    out->saved_state = saved_state ? av_value_copy(saved_state) : NULL;
    size_t i, count;
    av_value *value;
    av_arr_foreach(keys, i, count, value)
    {
        input_keyframe key;
        if (!read_key(value, out->frames, &key, error, error_size)) {
            return false;
        }
        int previous = acount(out->keys) - 1;
        if (previous >= 0 && key.step < out->keys[previous].step) {
            return fail(error, error_size, "key frames must not decrease");
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

scenario_document *scenario_document_load(const char *text, char *error, size_t error_size)
{
    av_doc *doc = av_doc_read(text, error, error_size);
    if (!doc) {
        return NULL;
    }
    scenario_document *document = xcalloc(1, sizeof *document);
    bool ok = read_document(av_doc_root(doc), document, error, error_size);
    av_doc_free(doc);
    if (!ok) {
        scenario_document_free(document);
        return NULL;
    }
    return document;
}

static void write_vector(av_doc *doc, av_value *object, const char *name, CF_V2 value)
{
    if (value.x == 0 && value.y == 0) {
        return;
    }
    av_value *vector = av_obj_add_obj(doc, object, name);
    av_obj_add_val(doc, vector, "x", av_float(doc, value.x));
    av_obj_add_val(doc, vector, "y", av_float(doc, value.y));
}

static void write_key(av_doc *doc, av_value *keys, const input_keyframe *key)
{
    av_value *value = av_arr_add_obj(doc, keys);
    av_obj_add_int(doc, value, "frame", key->step);
    write_vector(doc, value, "movement", key->movement);
    write_vector(doc, value, "aim", key->aim);
    if (key->aim_point) {
        av_obj_add_bool(doc, value, "aim_point", true);
    }
    if (key->firing) {
        av_obj_add_bool(doc, value, "firing", true);
    }
    if (key->pointer.active) {
        av_value *pointer = av_obj_add_obj(doc, value, "pointer");
        av_value *origin = av_obj_add_obj(doc, pointer, "origin");
        av_obj_add_val(doc, origin, "x", av_float(doc, key->pointer.origin.x));
        av_obj_add_val(doc, origin, "y", av_float(doc, key->pointer.origin.y));
        av_obj_add_val(doc, origin, "z", av_float(doc, key->pointer.origin.z));
        av_value *direction = av_obj_add_obj(doc, pointer, "direction");
        av_obj_add_val(doc, direction, "x", av_float(doc, key->pointer.direction.x));
        av_obj_add_val(doc, direction, "y", av_float(doc, key->pointer.direction.y));
        av_obj_add_val(doc, direction, "z", av_float(doc, key->pointer.direction.z));
    }
    int last = INPUT_CONTROLLER_MAX;
    while (last && !key->controllers[last - 1].connected) {
        last--;
    }
    if (last) {
        av_value *controllers = av_obj_add_arr(doc, value, "controllers");
        for (int i = 0; i < last; i++) {
            av_value *state = av_arr_add_obj(doc, controllers);
            av_obj_add_bool(doc, state, "connected", key->controllers[i].connected);
            if (!key->controllers[i].buttons) {
                continue;
            }
            av_value *buttons = av_obj_add_arr(doc, state, "buttons");
            for (int button = PAD_NONE + 1; button < PAD_COUNT; button++) {
                if (key->controllers[i].buttons & (UINT32_C(1) << button)) {
                    av_arr_add_str(doc, buttons, type_catalog_enum_label("PadControl", button));
                }
            }
        }
    }
    if (!key->controls) {
        return;
    }
    av_value *controls = av_obj_add_arr(doc, value, "controls");
    for (int control = CONTROL_NONE + 1; control < CONTROL_COUNT; control++) {
        if (key->controls & (UINT64_C(1) << control)) {
            av_arr_add_str(doc, controls, type_catalog_enum_label(CONTROL_ENUM, control));
        }
    }
}

char *scenario_document_save(const scenario_document *document)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_str(doc, root, "scene", document->scene);
    av_obj_add_int(doc, root, "frames", document->frames);
    if (document->seed) {
        av_obj_add_uint(doc, root, "seed", document->seed);
    }
    if (document->saved_state) {
        av_obj_add_val(doc, root, "saved_state", av_clone(doc, document->saved_state));
    }
    av_value *keys = av_obj_add_arr(doc, root, "keys");
    for (int i = 0; i < acount(document->keys); i++) {
        write_key(doc, keys, &document->keys[i]);
    }
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

void scenario_document_free(scenario_document *document)
{
    if (!document) {
        return;
    }
    free(document->scene);
    av_value_free(document->saved_state);
    afree(document->keys);
    free(document);
}
