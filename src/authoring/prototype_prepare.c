#include "authoring/prototype_prepare.h"

#include "authoring/image_prepare.h"
#include "authoring/material_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/parts_prepare.h"
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
static bool prepare_sprite(const prototype_component *component, char *error, size_t size)
{
    const Sprite *sprite = component->data;
    const Tilemap *map = component->data;
    bool is_sprite = component->desc->id == COMPONENT_Sprite.id;
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

bool authoring_prototype_resources(const prototype *document, char *error, size_t size)
{
    assert(document);
    for (int i = 0; i < acount(document->components); i++) {
        const prototype_component *component = &document->components[i];
        const char *mesh = authoring_component_mesh(component->desc, component->data);
        if (mesh && !authoring_mesh_prepare(mesh, error, size)) {
            return false;
        }
        if (component->desc->id == COMPONENT_MeshParts.id) {
            const MeshParts *parts = component->data;
            if (parts->parts && parts->parts[0] &&
                !authoring_parts_prepare(parts->parts, error, size)) {
                return false;
            }
        }
        if ((component->desc->id == COMPONENT_Sprite.id ||
             component->desc->id == COMPONENT_Tilemap.id) &&
            !prepare_sprite(component, error, size)) {
            return false;
        }
        if (component->desc->id == COMPONENT_Environment.id) {
            const char *sky = ((const Environment *)component->data)->sky;
            char diagnostic[256];
            if (sky && sky[0] &&
                !image_prepare_sky(sky, diagnostic, sizeof diagnostic)) {
                if (size) {
                    snprintf(error, size, "Environment sky %s: %s", sky, diagnostic);
                }
                return false;
            }
        }
    }
    for (int i = 0; i < acount(document->child_prototypes); i++) {
        if (!authoring_prototype_resources(document->child_prototypes[i], error, size)) {
            return false;
        }
    }
    return true;
}

static const unsigned char tag = 1;

void authoring_objects_add(authoring_objects *objects, const prototype *document, const char *name, int parent)
{
    for (int i = 0; document->registry && i < document->registry->relationship_count;
         i++) {
        const ecs_relationship_desc *desc = document->registry->relationships[i];
        int at = 0;
        while (at < acount(objects->relationships) &&
               objects->relationships[at].id != desc->id) {
            at++;
        }
        if (at == acount(objects->relationships)) {
            apush(objects->relationships, *desc);
        } else {
            const ecs_relationship_desc *prior = &objects->relationships[at];
            objects->relationship_conflict |= strcmp(prior->name, desc->name) ||
                                              strcmp(prior->key, desc->key) ||
                                              prior->traits != desc->traits ||
                                              prior->on_delete_target !=
                                                  desc->on_delete_target;
        }
    }
    scene_component *components = NULL;
    for (int i = 0; i < acount(document->components); i++) {
        const prototype_component *component = &document->components[i];
        apush(components, ((scene_component){ component->desc, component->data }));
    }
    apush(components, ((scene_component){ prototype_tag_desc(document), &tag }));
    apush(objects->components, components);
    apush(objects->parents, parent);
    apush(
        objects->objects,
        ((scene_object){
            .name = name,
            .components = components,
            .component_count = acount(components),
            .behaviors = (const char *const *)document->graphs,
            .behavior_count = acount(document->graphs),
        })
    );
}

void authoring_objects_add_links(
    authoring_objects *objects,
    int object,
    const prototype *named,
    bool outer,
    const scene_document *document,
    const int *placed
)
{
    for (int i = 0; i < acount(named->links); i++) {
        const prototype_link *link = &named->links[i];
        int component = 0;
        while (component < acount(named->components) &&
               named->components[component].desc->id != link->component->id) {
            component++;
        }
        if (link->outer != outer || component == acount(named->components)) {
            continue;
        }
        int target = scene_document_entity_index(document, link->path);
        if (target < 0) {
            objects->missing = link;
            continue;
        }
        apush(
            objects->links,
            ((scene_link){ object, component, link->field, placed ? placed[target] : target })
        );
    }
}

void authoring_objects_add_children(authoring_objects *objects, const prototype *document, int index)
{
    const scene_document *children = document->children;
    int *added = NULL; /* where each child lands in `objects` */
    for (int i = 0; children && i < acount(children->entities); i++) {
        int parent = children->entities[i].parent;
        const prototype *child = document->child_prototypes[i];
        apush(added, acount(objects->objects));
        authoring_objects_add(objects, child, child->name, parent < 0 ? index : added[parent]);
        authoring_objects_add_children(objects, child, alast(added));
    }
    authoring_objects_add_links(objects, index, document, false, children, added);
    for (int i = 0; i < acount(document->pairs); i++) {
        const prototype_pair *pair = &document->pairs[i];
        int target = children ? scene_document_entity_index(children, pair->path)
                              : -1;
        if (target < 0) {
            objects->missing_pair = pair;
            continue;
        }
        int relationship = 0;
        while (relationship < acount(objects->relationships) &&
               objects->relationships[relationship].id != pair->relationship->id) {
            relationship++;
        }
        apush(objects->pairs, ((scene_pair){ index, relationship, added[target] }));
    }
    for (int i = 0; children && i < acount(children->entities); i++) {
        authoring_objects_add_links(objects, added[i], document->child_prototypes[i], true, children, added);
    }
    afree(added);
}

scene *authoring_objects_prepare(authoring_objects *objects, scene_spec spec, char *error, size_t size)
{
    spec.objects = objects->objects;
    spec.object_count = acount(objects->objects);
    spec.parents = objects->parents;
    spec.links = objects->links;
    spec.link_count = acount(objects->links);
    spec.relationships = objects->relationships;
    spec.relationship_count = acount(objects->relationships);
    spec.pairs = objects->pairs;
    spec.pair_count = acount(objects->pairs);
    const prototype_link *missing = objects->missing;
    scene *prepared_scene = NULL;
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
    if (size && objects->missing_pair) {
        snprintf(
            error,
            size,
            "Relationship %s names missing child.%s",
            objects->missing_pair->relationship->name,
            objects->missing_pair->path
        );
    }
    if (size && objects->relationship_conflict) {
        snprintf(error, size, "Conflicting authored relationship schemas");
    }
    if (!missing && !objects->missing_pair && !objects->relationship_conflict) {
        prepared_scene = scene_prepare(&spec, error, size);
    }
    for (int i = 0; i < acount(objects->components); i++) {
        afree(objects->components[i]);
    }
    afree(objects->components);
    afree(objects->parents);
    afree(objects->objects);
    afree(objects->links);
    afree(objects->relationships);
    afree(objects->pairs);
    return prepared_scene;
}

scene *authoring_prototype_prepare(const prototype *document, char *error, size_t size)
{
    assert(document);
    if (!authoring_prototype_resources(document, error, size)) {
        return NULL;
    }
    authoring_objects objects = { 0 };
    authoring_objects_add(&objects, document, document->name, -1);
    authoring_objects_add_children(&objects, document, 0);
    scene_spec spec = { .name = document->name, .physics = physics_defaults() };
    return authoring_objects_prepare(&objects, spec, error, size);
}
