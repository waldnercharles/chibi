#include "authoring/native_prototype.h"

#include "runtime/components.h"
#include "support/array.h"

#include <string.h>

typedef union
{
    Appearance appearance;
    Camera camera;
    VisualTransform transform;
    MeshParts parts;
    HRC2D hrc;
    ParticleEmitter emitter;
    ParticleField3D field;
    Environment environment;
    DirectionalLight light;
    SpotLight spot;
} native_initial;

static bool contains(prototype_component *components, const char *name)
{
    for (int i = 0; i < acount(components); i++) {
        if (!strcmp(components[i].desc->name, name)) {
            return true;
        }
    }
    return false;
}

static void initial_row(const ecs_component_desc *desc, native_initial *storage, prototype_default *out)
{
    *out = (prototype_default){ .component = desc->id };
    if (!strcmp(desc->name, "Appearance")) {
        storage->appearance = (Appearance){ .width = 32,
                                            .height = 32,
                                            .color = 0xffffffffu,
                                            .shape = SHAPE_BOX,
                                            .antialias = 1 };
        out->data = &storage->appearance;
        out->size = sizeof storage->appearance;
    } else if (!strcmp(desc->name, "Camera")) {
        storage->camera = (Camera){ .width = 960, .height = 540 };
        out->data = &storage->camera;
        out->size = sizeof storage->camera;
    } else if (!strcmp(desc->name, "VisualTransform")) {
        storage->transform = (VisualTransform){ .scale = 1 };
        out->data = &storage->transform;
        out->size = sizeof storage->transform;
    } else if (!strcmp(desc->name, "MeshParts")) {
        storage->parts = (MeshParts){ .parts = "(array)-\n" };
        out->data = &storage->parts;
        out->size = sizeof storage->parts;
    } else if (!strcmp(desc->name, "HRC2D")) {
        storage->hrc = hrc2d_defaults();
        out->data = &storage->hrc;
        out->size = sizeof storage->hrc;
    } else if (!strcmp(desc->name, "ParticleEmitter")) {
        storage->emitter = particle_emitter_defaults();
        out->data = &storage->emitter;
        out->size = sizeof storage->emitter;
    } else if (!strcmp(desc->name, "ParticleField3D")) {
        storage->field = (ParticleField3D){ .count = 1024, .additive = true };
        out->data = &storage->field;
        out->size = sizeof storage->field;
    } else if (!strcmp(desc->name, "Environment")) {
        storage->environment = environment_defaults();
        out->data = &storage->environment;
        out->size = sizeof storage->environment;
    } else if (!strcmp(desc->name, "DirectionalLight")) {
        storage->light = directional_light_defaults();
        out->data = &storage->light;
        out->size = sizeof storage->light;
    } else if (!strcmp(desc->name, "SpotLight")) {
        storage->spot = spot_light_defaults();
        out->data = &storage->spot;
        out->size = sizeof storage->spot;
    }
}

int prototype_add_native_component(prototype *prototype, const ecs_component_desc *desc)
{
    if (!prototype || !desc || !desc->name) {
        return -1;
    }
    if (contains(prototype->detached_components, desc->name)) {
        return prototype_add_component(prototype, desc);
    }
    native_initial storage;
    prototype_default initial;
    initial_row(desc, &storage, &initial);
    if (!initial.data) {
        return prototype_add_component(prototype, desc);
    }
    const prototype_registry *original = prototype->registry;
    prototype_registry registry = *original;
    registry.defaults = &initial;
    registry.default_count = 1;
    prototype->registry = &registry;
    int result = prototype_add_component(prototype, desc);
    prototype->registry = original;
    return result;
}

av_value *prototype_native_defaults_value(const prototype_registry *registry, const ecs_component_desc *desc)
{
    prototype *prototype = prototype_new("ComponentDefaults");
    prototype->registry_storage = *registry;
    int result = prototype_add_native_component(prototype, desc);
    av_value *value = result ? NULL : prototype_component_value(prototype, desc->name);
    prototype_free(prototype);
    return value;
}
