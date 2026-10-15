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
        if (component->desc->id == COMPONENT_Sprite.id ||
            component->desc->id == COMPONENT_Tilemap.id) {
            const char *image = component->desc->id == COMPONENT_Sprite.id
                                  ? ((const Sprite *)component->data)->image
                                  : ((const Tilemap *)component->data)->image;
            shader_image_source source = { .key = (char *)image,
                                           .color_space = SHADER_IMAGE_SRGB,
                                           .filter = SHADER_IMAGE_NEAREST,
                                           .wrap = SHADER_IMAGE_CLAMP };
            shader_error diagnostic = { 0 };
            if (!image || !render_image_variant(&source, &diagnostic)) {
                if (size) {
                    snprintf(error, size, "Sprite atlas: %s", image ? diagnostic.message : "no image");
                }
                return false;
            }
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
        if (component->desc->id == COMPONENT_Shader.id) {
            shader_error diagnostic = { 0 };
            if (!authoring_shader_binding_prepare(component->data, &diagnostic)) {
                if (size) {
                    snprintf(error, size, "%s", diagnostic.message);
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
    if (!missing) {
        prepared_scene = scene_prepare(&spec, error, size);
    }
    for (int i = 0; i < acount(objects->components); i++) {
        afree(objects->components[i]);
    }
    afree(objects->components);
    afree(objects->parents);
    afree(objects->objects);
    afree(objects->links);
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
