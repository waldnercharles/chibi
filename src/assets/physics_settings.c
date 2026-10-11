#include "assets/physics_settings.h"

#include <assert.h>
#include <string.h>

bool physics_settings_read(av_value *object, physics_settings *out)
{
    physics_settings settings = physics_defaults();
    if (!object) {
        *out = settings;
        return true;
    }
    if (!av_is_obj(object)) {
        return false;
    }
    size_t i, count;
    av_value *key, *value;
    av_obj_foreach(object, i, count, key, value)
    {
        const char *name = av_get_str(key);
        if (!strcmp(name, "workers") || !strcmp(name, "substeps")) {
            if (!av_is_int(value) || av_get_sint(value) < 0 || av_get_uint(value) > 32) {
                return false;
            }
            int *target = !strcmp(name, "workers") ? &settings.workers
                                                   : &settings.substeps;
            *target = (int)av_get_sint(value);
        } else if (!strcmp(name, "sleep") || !strcmp(name, "continuous") || !strcmp(name, "force_drive")) {
            if (!av_is_bool(value)) {
                return false;
            }
            bool *target = !strcmp(name, "sleep")      ? &settings.sleep
                         : !strcmp(name, "continuous") ? &settings.continuous
                                                       : &settings.force_drive;
            *target = av_get_bool(value);
        } else if (!strcmp(name, "contact_hertz") || !strcmp(name, "contact_damping")) {
            if (!av_is_num(value)) {
                return false;
            }
            float *target = !strcmp(name, "contact_hertz")
                              ? &settings.contact_hertz
                              : &settings.contact_damping;
            *target = (float)av_get_num(value);
        } else {
            return false;
        }
    }
    if (!physics_settings_valid(settings)) {
        return false;
    }
    *out = settings;
    return true;
}

av_value *physics_settings_write(av_doc *doc, physics_settings settings)
{
    assert(physics_settings_valid(settings));
    /* Reading starts from the defaults, so only differences are written. */
    physics_settings defaults = physics_defaults();
    av_value *object = av_obj(doc);
    if (settings.workers != defaults.workers) {
        av_obj_add_int(doc, object, "workers", settings.workers);
    }
    if (settings.substeps != defaults.substeps) {
        av_obj_add_int(doc, object, "substeps", settings.substeps);
    }
    if (settings.sleep != defaults.sleep) {
        av_obj_add_bool(doc, object, "sleep", settings.sleep);
    }
    if (settings.continuous != defaults.continuous) {
        av_obj_add_bool(doc, object, "continuous", settings.continuous);
    }
    if (settings.force_drive != defaults.force_drive) {
        av_obj_add_bool(doc, object, "force_drive", settings.force_drive);
    }
    if (settings.contact_hertz != defaults.contact_hertz) {
        av_obj_add_float(doc, object, "contact_hertz", settings.contact_hertz);
    }
    if (settings.contact_damping != defaults.contact_damping) {
        av_obj_add_float(doc, object, "contact_damping", settings.contact_damping);
    }
    return object;
}
