#include "authoring/native_prototype.h"

#include "runtime/components.h"
#include "support/array.h"

#include <string.h>

typedef union
{
    GameAppearance appearance;
    GameCamera camera;
    GameVisualTransform transform;
    GameVisualMotion motion;
    GameRhythm rhythm;
    GameMeshParts parts;
    GameHRC2D hrc;
    GameParticleEmitter emitter;
    GameEnvironment environment;
    GameDirectionalLight light;
    GameSpotLight spot;
} native_initial;

static bool contains(game_prototype_component *components, const char *name)
{
    for (int i = 0; i < acount(components); i++) {
        if (!strcmp(components[i].desc->name, name)) {
            return true;
        }
    }
    return false;
}

static int motion_initial(const game_prototype *prototype, GameVisualMotion *motion)
{
    *motion = (GameVisualMotion){ .speed = 1,
                                  .reaction_duration = .32f,
                                  .reaction_age = .32f };
    pin_value value;
    const char *angles[] = { "pitch", "yaw", "roll" };
    float *rotation[] = { &motion->rotation.x,
                          &motion->rotation.y,
                          &motion->rotation.z };
    if (contains(prototype->components, "GameVisualTransform")) {
        for (int i = 0; i < 3; i++) {
            if (game_prototype_read_field(prototype, "GameVisualTransform", angles[i], &value)) {
                return -1;
            }
            *rotation[i] = value.f;
        }
    }
    if (contains(prototype->components, "GameGeometry3D")) {
        if (game_prototype_read_field(prototype, "GameGeometry3D", "elevation", &value)) {
            return -1;
        }
        motion->rest_elevation = value.f;
    }
    return 0;
}

static int initial_row(
    const game_prototype *prototype,
    const ecs_component_desc *desc,
    native_initial *storage,
    game_prototype_default *out
)
{
    *out = (game_prototype_default){ .component = desc->id };
    if (!strcmp(desc->name, "GameAppearance")) {
        storage->appearance = (GameAppearance){ .width = 32,
                                                .height = 32,
                                                .color = 0xffffffffu,
                                                .shape = GAME_SHAPE_BOX,
                                                .antialias = 1 };
        out->data = &storage->appearance;
        out->size = sizeof storage->appearance;
    } else if (!strcmp(desc->name, "GameCamera")) {
        storage->camera = (GameCamera){ .width = 960, .height = 540 };
        out->data = &storage->camera;
        out->size = sizeof storage->camera;
    } else if (!strcmp(desc->name, "GameVisualTransform")) {
        storage->transform = (GameVisualTransform){ .scale = 1 };
        out->data = &storage->transform;
        out->size = sizeof storage->transform;
    } else if (!strcmp(desc->name, "GameVisualMotion")) {
        if (motion_initial(prototype, &storage->motion)) {
            return -1;
        }
        out->data = &storage->motion;
        out->size = sizeof storage->motion;
    } else if (!strcmp(desc->name, "GameRhythm")) {
        storage->rhythm = (GameRhythm){ .bpm = 120, .pulse_width = .18f };
        out->data = &storage->rhythm;
        out->size = sizeof storage->rhythm;
    } else if (!strcmp(desc->name, "GameMeshParts")) {
        storage->parts = (GameMeshParts){ .parts = "(array)-\n" };
        out->data = &storage->parts;
        out->size = sizeof storage->parts;
    } else if (!strcmp(desc->name, "GameHRC2D")) {
        storage->hrc = game_hrc2d_defaults();
        out->data = &storage->hrc;
        out->size = sizeof storage->hrc;
    } else if (!strcmp(desc->name, "GameParticleEmitter")) {
        storage->emitter = game_particle_emitter_defaults();
        out->data = &storage->emitter;
        out->size = sizeof storage->emitter;
    } else if (!strcmp(desc->name, "GameEnvironment")) {
        storage->environment = game_environment_defaults();
        out->data = &storage->environment;
        out->size = sizeof storage->environment;
    } else if (!strcmp(desc->name, "GameDirectionalLight")) {
        storage->light = game_directional_light_defaults();
        out->data = &storage->light;
        out->size = sizeof storage->light;
    } else if (!strcmp(desc->name, "GameSpotLight")) {
        storage->spot = game_spot_light_defaults();
        out->data = &storage->spot;
        out->size = sizeof storage->spot;
    }
    return 0;
}

int game_prototype_add_native_component(game_prototype *prototype, const ecs_component_desc *desc)
{
    if (!prototype || !desc || !desc->name) {
        return -1;
    }
    if (contains(prototype->detached_components, desc->name)) {
        return game_prototype_add_component(prototype, desc);
    }
    native_initial storage;
    game_prototype_default initial;
    if (initial_row(prototype, desc, &storage, &initial)) {
        return -1;
    }
    if (!initial.data) {
        return game_prototype_add_component(prototype, desc);
    }
    const game_prototype_registry *original = prototype->registry;
    game_prototype_registry registry = *original;
    registry.defaults = &initial;
    registry.default_count = 1;
    prototype->registry = &registry;
    int result = game_prototype_add_component(prototype, desc);
    prototype->registry = original;
    return result;
}

av_value *game_prototype_native_defaults_value(
    const game_prototype_registry *registry,
    const ecs_component_desc *desc
)
{
    game_prototype *prototype = game_prototype_new("ComponentDefaults");
    prototype->registry_storage = *registry;
    int result = game_prototype_add_native_component(prototype, desc);
    av_value *value = result
                        ? NULL
                        : game_prototype_component_value(prototype, desc->name);
    game_prototype_free(prototype);
    return value;
}
