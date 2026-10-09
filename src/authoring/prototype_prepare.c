#include "authoring/prototype_prepare.h"

#include "authoring/image_prepare.h"
#include "authoring/material_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/parts_prepare.h"
#include "authoring/shader_prepare.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>

bool authoring_prototype_resources(const game_prototype *prototype, char *error, size_t size)
{
    assert(prototype);
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (component->desc->id == GAME_COMPONENT_GameMeshAsset.id) {
            const GameMeshAsset *mesh = component->data;
            if (mesh->source && mesh->source[0] &&
                !authoring_mesh_prepare(mesh->source, error, size)) {
                return false;
            }
        }
        if (component->desc->id == GAME_COMPONENT_GameMeshParts.id) {
            const GameMeshParts *parts = component->data;
            if (parts->parts && parts->parts[0] &&
                !authoring_parts_prepare(parts->parts, error, size)) {
                return false;
            }
        }
        if (component->desc->id == GAME_COMPONENT_GameSprite.id) {
            const GameSprite *sprite = component->data;
            shader_image_source source = { .key = (char *)sprite->image,
                                           .color_space = SHADER_IMAGE_SRGB,
                                           .filter = SHADER_IMAGE_NEAREST,
                                           .wrap = SHADER_IMAGE_CLAMP };
            shader_error diagnostic = { 0 };
            if (!sprite->image || !render_image_variant(&source, &diagnostic)) {
                if (size) {
                    snprintf(
                        error,
                        size,
                        "Sprite: %s",
                        sprite->image ? diagnostic.message : "no image"
                    );
                }
                return false;
            }
        }
        if (component->desc->id == GAME_COMPONENT_GameEnvironment.id) {
            const char *sky = ((const GameEnvironment *)component->data)->sky;
            char diagnostic[256];
            if (sky && sky[0] &&
                !image_prepare_sky(sky, diagnostic, sizeof diagnostic)) {
                if (size) {
                    snprintf(error, size, "Environment sky %s: %s", sky, diagnostic);
                }
                return false;
            }
        }
        if (component->desc->id == GAME_COMPONENT_GameShader.id) {
            shader_error diagnostic = { 0 };
            if (!authoring_shader_binding_prepare(component->data, &diagnostic)) {
                if (size) {
                    snprintf(error, size, "%s", diagnostic.message);
                }
                return false;
            }
        }
    }
    return true;
}

game_scene *authoring_prototype_prepare(const game_prototype *prototype, char *error, size_t size)
{
    assert(prototype);
    if (!authoring_prototype_resources(prototype, error, size)) {
        return NULL;
    }
    game_scene_component *components = NULL;
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        apush(components, ((game_scene_component){ component->desc, component->data }));
    }
    unsigned char tag = 1;
    apush(components, ((game_scene_component){ game_prototype_tag_desc(prototype), &tag }));
    game_scene_object object = {
        .name = prototype->name,
        .components = components,
        .component_count = acount(components),
        .behaviors = (const char *const *)prototype->graphs,
        .behavior_count = acount(prototype->graphs),
    };
    game_scene_spec spec = {
        .name = prototype->name,
        .physics = game_physics_defaults(),
        .objects = &object,
        .object_count = 1,
    };
    game_scene *prepared = game_scene_prepare(&spec, error, size);
    afree(components);
    return prepared;
}
