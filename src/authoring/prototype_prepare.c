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

/* Loads the variant of a sprite's image or map, sampled texel by texel. */
static bool sprite_variant(const char *key, shader_image_color_space color_space, shader_error *error)
{
    shader_image_source source = { .key = (char *)key,
                                   .color_space = color_space,
                                   .filter = SHADER_IMAGE_NEAREST,
                                   .wrap = SHADER_IMAGE_CLAMP };
    return render_image_variant(&source, error) != 0;
}

/* Loads an optional linear map of normals or heights, which must be laid out
 * like its `width` x `height` image. */
static bool sprite_map(const char *key, int width, int height, shader_error *error)
{
    if (!key || !key[0]) {
        return true;
    }
    if (!sprite_variant(key, SHADER_IMAGE_LINEAR, error)) {
        return false;
    }
    int map_width = 0, map_height = 0;
    render_image_info(key, &map_width, &map_height);
    if (map_width == width && map_height == height) {
        return true;
    }
    snprintf(
        error->message,
        sizeof error->message,
        "%s is %dx%d, not the %dx%d of its image",
        key,
        map_width,
        map_height,
        width,
        height
    );
    return false;
}

/* Prepares a Sprite's or Tilemap's image and maps. */
static bool prepare_sprite(const game_prototype_component *component, char *error, size_t size)
{
    const GameSprite *sprite = component->data;
    const GameTilemap *map = component->data;
    bool is_sprite = component->desc->id == GAME_COMPONENT_GameSprite.id;
    const char *image = is_sprite ? sprite->image : map->image;
    shader_error diagnostic = { 0 };
    int width = 0, height = 0;
    if (image && sprite_variant(image, SHADER_IMAGE_SRGB, &diagnostic) &&
        render_image_info(image, &width, &height) &&
        sprite_map(is_sprite ? sprite->normal_map : map->normal_map, width, height, &diagnostic) &&
        sprite_map(is_sprite ? sprite->height_map : map->height_map, width, height, &diagnostic)) {
        return true;
    }
    if (size) {
        snprintf(error, size, "Sprite atlas: %s", image ? diagnostic.message : "no image");
    }
    return false;
}

bool authoring_prototype_resources(const game_prototype *prototype, char *error, size_t size)
{
    assert(prototype);
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        const char *mesh = authoring_component_mesh(component->desc, component->data);
        if (mesh && !authoring_mesh_prepare(mesh, error, size)) {
            return false;
        }
        if (component->desc->id == GAME_COMPONENT_GameMeshParts.id) {
            const GameMeshParts *parts = component->data;
            if (parts->parts && parts->parts[0] &&
                !authoring_parts_prepare(parts->parts, error, size)) {
                return false;
            }
        }
        if ((component->desc->id == GAME_COMPONENT_GameSprite.id ||
             component->desc->id == GAME_COMPONENT_GameTilemap.id) &&
            !prepare_sprite(component, error, size)) {
            return false;
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
    for (int i = 0; i < acount(prototype->child_prototypes); i++) {
        if (!authoring_prototype_resources(prototype->child_prototypes[i], error, size)) {
            return false;
        }
    }
    return true;
}

static const unsigned char tag = 1;

void authoring_objects_add(
    authoring_objects *objects,
    const game_prototype *prototype,
    const char *name,
    int parent
)
{
    game_scene_component *components = NULL;
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        apush(components, ((game_scene_component){ component->desc, component->data }));
    }
    apush(components, ((game_scene_component){ game_prototype_tag_desc(prototype), &tag }));
    apush(objects->components, components);
    apush(objects->parents, parent);
    apush(
        objects->objects,
        ((game_scene_object){
            .name = name,
            .components = components,
            .component_count = acount(components),
            .behaviors = (const char *const *)prototype->graphs,
            .behavior_count = acount(prototype->graphs),
        })
    );
}

void authoring_objects_add_links(
    authoring_objects *objects,
    int object,
    const game_prototype *named,
    bool outer,
    const game_scene_document *document,
    const int *placed
)
{
    for (int i = 0; i < acount(named->links); i++) {
        const game_prototype_link *link = &named->links[i];
        int component = 0;
        while (component < acount(named->components) &&
               named->components[component].desc->id != link->component->id) {
            component++;
        }
        if (link->outer != outer || component == acount(named->components)) {
            continue;
        }
        int target = game_scene_document_entity_index(document, link->path);
        if (target < 0) {
            objects->missing = link;
            continue;
        }
        apush(
            objects->links,
            ((game_scene_link){ object, component, link->field, placed ? placed[target] : target })
        );
    }
}

void authoring_objects_add_children(authoring_objects *objects, const game_prototype *prototype, int index)
{
    const game_scene_document *children = prototype->children;
    int *added = NULL; /* where each child lands in `objects` */
    for (int i = 0; children && i < acount(children->entities); i++) {
        int parent = children->entities[i].parent;
        const game_prototype *child = prototype->child_prototypes[i];
        apush(added, acount(objects->objects));
        authoring_objects_add(objects, child, child->name, parent < 0 ? index : added[parent]);
        authoring_objects_add_children(objects, child, alast(added));
    }
    authoring_objects_add_links(objects, index, prototype, false, children, added);
    for (int i = 0; children && i < acount(children->entities); i++) {
        authoring_objects_add_links(objects, added[i], prototype->child_prototypes[i], true, children, added);
    }
    afree(added);
}

game_scene *authoring_objects_prepare(
    authoring_objects *objects,
    game_scene_spec spec,
    char *error,
    size_t size
)
{
    spec.objects = objects->objects;
    spec.object_count = acount(objects->objects);
    spec.parents = objects->parents;
    spec.links = objects->links;
    spec.link_count = acount(objects->links);
    const game_prototype_link *missing = objects->missing;
    game_scene *scene = NULL;
    if (missing && size) {
        snprintf(
            error,
            size,
            "%s %s names %s, but no entity has that path",
            missing->component->name,
            missing->component->type->fields[missing->field].name,
            missing->path
        );
    }
    if (!missing) {
        scene = game_scene_prepare(&spec, error, size);
    }
    for (int i = 0; i < acount(objects->components); i++) {
        afree(objects->components[i]);
    }
    afree(objects->components);
    afree(objects->parents);
    afree(objects->objects);
    afree(objects->links);
    return scene;
}

game_scene *authoring_prototype_prepare(const game_prototype *prototype, char *error, size_t size)
{
    assert(prototype);
    if (!authoring_prototype_resources(prototype, error, size)) {
        return NULL;
    }
    authoring_objects objects = { 0 };
    authoring_objects_add(&objects, prototype, prototype->name, -1);
    authoring_objects_add_children(&objects, prototype, 0);
    game_scene_spec spec = { .name = prototype->name,
                             .physics = game_physics_defaults() };
    return authoring_objects_prepare(&objects, spec, error, size);
}
