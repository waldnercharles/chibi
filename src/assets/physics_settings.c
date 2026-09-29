#include "assets/physics_settings.h"

#include <assert.h>
#include <string.h>

bool game_physics_settings_read(yyjson_val *object, game_physics_settings *out)
{
    game_physics_settings settings = game_physics_defaults();
    if (!object) {
        *out = settings;
        return true;
    }
    if (!yyjson_is_obj(object)) {
        return false;
    }
    size_t i, count;
    yyjson_val *key, *value;
    yyjson_obj_foreach(object, i, count, key, value)
    {
        const char *name = yyjson_get_str(key);
        if (!strcmp(name, "workers") || !strcmp(name, "substeps")) {
            if (!yyjson_is_int(value) || yyjson_get_sint(value) < 0 ||
                yyjson_get_uint(value) > 32) {
                return false;
            }
            int *target = !strcmp(name, "workers") ? &settings.workers
                                                   : &settings.substeps;
            *target = (int)yyjson_get_sint(value);
        } else if (!strcmp(name, "sleep") || !strcmp(name, "continuous") || !strcmp(name, "force_drive")) {
            if (!yyjson_is_bool(value)) {
                return false;
            }
            bool *target = !strcmp(name, "sleep")      ? &settings.sleep
                         : !strcmp(name, "continuous") ? &settings.continuous
                                                       : &settings.force_drive;
            *target = yyjson_get_bool(value);
        } else if (!strcmp(name, "gravity")) {
            yyjson_val *x = yyjson_obj_get(value, "x"),
                       *y = yyjson_obj_get(value, "y");
            if (!yyjson_is_obj(value) ||
                yyjson_obj_size(value) != (size_t)(!!x + !!y) ||
                (x && !yyjson_is_num(x)) || (y && !yyjson_is_num(y))) {
                return false;
            }
            settings.gravity_x = x ? (float)yyjson_get_num(x) : 0;
            settings.gravity_y = y ? (float)yyjson_get_num(y) : 0;
        } else if (!strcmp(name, "contact_hertz") || !strcmp(name, "contact_damping")) {
            if (!yyjson_is_num(value)) {
                return false;
            }
            float *target = !strcmp(name, "contact_hertz")
                              ? &settings.contact_hertz
                              : &settings.contact_damping;
            *target = (float)yyjson_get_num(value);
        } else {
            return false;
        }
    }
    if (!game_physics_settings_valid(settings)) {
        return false;
    }
    *out = settings;
    return true;
}

yyjson_mut_val *game_physics_settings_write(yyjson_mut_doc *doc, game_physics_settings settings)
{
    assert(game_physics_settings_valid(settings));
    /* Reading starts from the defaults, so only differences are written. */
    game_physics_settings defaults = game_physics_defaults();
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    if (settings.workers != defaults.workers) {
        yyjson_mut_obj_add_int(doc, object, "workers", settings.workers);
    }
    if (settings.substeps != defaults.substeps) {
        yyjson_mut_obj_add_int(doc, object, "substeps", settings.substeps);
    }
    if (settings.sleep != defaults.sleep) {
        yyjson_mut_obj_add_bool(doc, object, "sleep", settings.sleep);
    }
    if (settings.continuous != defaults.continuous) {
        yyjson_mut_obj_add_bool(doc, object, "continuous", settings.continuous);
    }
    if (settings.force_drive != defaults.force_drive) {
        yyjson_mut_obj_add_bool(doc, object, "force_drive", settings.force_drive);
    }
    if (settings.contact_hertz != defaults.contact_hertz) {
        yyjson_mut_obj_add_float(doc, object, "contact_hertz", settings.contact_hertz);
    }
    if (settings.contact_damping != defaults.contact_damping) {
        yyjson_mut_obj_add_float(doc, object, "contact_damping", settings.contact_damping);
    }
    if (settings.gravity_x != defaults.gravity_x ||
        settings.gravity_y != defaults.gravity_y) {
        yyjson_mut_val *gravity = yyjson_mut_obj_add_obj(doc, object, "gravity");
        yyjson_mut_obj_add_float(doc, gravity, "x", settings.gravity_x);
        yyjson_mut_obj_add_float(doc, gravity, "y", settings.gravity_y);
    }
    return object;
}
