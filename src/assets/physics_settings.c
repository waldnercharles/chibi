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
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, object, "workers", settings.workers);
    yyjson_mut_obj_add_int(doc, object, "substeps", settings.substeps);
    yyjson_mut_obj_add_bool(doc, object, "sleep", settings.sleep);
    yyjson_mut_obj_add_bool(doc, object, "continuous", settings.continuous);
    yyjson_mut_obj_add_bool(doc, object, "force_drive", settings.force_drive);
    yyjson_mut_obj_add_real(doc, object, "contact_hertz", settings.contact_hertz);
    yyjson_mut_obj_add_real(doc, object, "contact_damping", settings.contact_damping);
    return object;
}
