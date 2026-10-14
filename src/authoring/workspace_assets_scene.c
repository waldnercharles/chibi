#include "assets/scene_document.h"
#include "authoring/native_prototype.h"
#include "authoring/workspace_assets.h"
#include "ecs/component_catalog.h"
#include "runtime/component_catalog.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static av_value *at_path(av_value *root, const char *path)
{
    if (!*path) {
        return root;
    }
    if (*path != '/') {
        return NULL;
    }
    av_value *value = root;
    const char *p = path + 1;
    while (1) {
        char key[1024];
        int n = 0;
        while (*p && *p != '/') {
            char c = *p++;
            if (c == '~') {
                if (*p != '0' && *p != '1') {
                    return NULL;
                }
                c = *p++ == '0' ? '~' : '/';
            }
            if (n >= (int)sizeof key - 1) {
                return NULL;
            }
            key[n++] = c;
        }
        key[n] = 0;
        if (av_is_obj(value)) {
            value = av_obj_get(value, key);
        } else if (av_is_arr(value)) {
            if (!n || (key[0] == '0' && key[1])) {
                return NULL;
            }
            size_t index = 0;
            for (int i = 0; i < n; i++) {
                if (key[i] < '0' || key[i] > '9' || index > 1000000) {
                    return NULL;
                }
                index = index * 10 + (size_t)(key[i] - '0');
            }
            value = av_arr_get(value, index);
        } else {
            return NULL;
        }
        if (!*p || !value) {
            return value;
        }
        p++;
    }
}
static bool find_entity(
    av_value *array,
    const av_value *wanted,
    const char *parent,
    char **name_path,
    av_value **holder,
    size_t *position,
    int depth
)
{
    if (!av_is_arr(array) || depth > SCENE_DEPTH_MAX) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(array); i++) {
        av_value *item = av_arr_get(array, i);
        const char *name = av_get_str(av_obj_get(item, "name"));
        if (!name) {
            continue;
        }
        size_t size = strlen(parent) + strlen(name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s%s%s", parent, *parent ? "." : "", name);
        if (item == wanted) {
            *name_path = path;
            *holder = array;
            *position = i;
            return true;
        }
        if (find_entity(av_obj_get(item, "children"), wanted, path, name_path, holder, position, depth + 1)) {
            free(path);
            return true;
        }
        free(path);
    }
    return false;
}
static bool below(const char *path, const char *prefix)
{
    size_t n = strlen(prefix);
    return !strncmp(path, prefix, n) && (!path[n] || path[n] == '.');
}
static bool rewrite_fields(
    av_value *fields,
    const type_desc *type,
    const char *from,
    const char *to,
    bool check,
    int depth
)
{
    if (!av_is_obj(fields) || !type) {
        return true;
    }
    if (depth > TYPE_MAX_NESTING) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        av_value *value = av_obj_get(fields, field->name);
        if (field->type == PIN_STRUCT) {
            if (!rewrite_fields(value, type_catalog_find(field->type_name), from, to, check, depth + 1)) {
                return false;
            }
        }
        if (field->type != PIN_HANDLE || !av_is_str(value) ||
            !below(av_get_str(value), from)) {
            continue;
        }
        if (check) {
            return false;
        }
        const char *rest = av_get_str(value) + strlen(from);
        size_t bytes = strlen(to) + strlen(rest) + 1;
        char *path = xmalloc(bytes);
        snprintf(path, bytes, "%s%s", to, rest);
        av_set_str(value, path);
        free(path);
    }
    return true;
}
static bool rewrite_components(
    const prototype_registry *registry,
    av_value *components,
    const char *from,
    const char *to,
    bool check
)
{
    for (size_t i = 0; i < av_obj_size(components); i++) {
        const char *key = av_get_str(av_obj_key_at(components, i));
        const ecs_component_desc *component = ecs_component_catalog_find(
            (ecs_component_catalog){ registry->components, registry->component_count },
            key
        );
        const type_desc *type = component ? component->type : type_catalog_find(key);
        if (!type) {
            return false; /* Unreflected fields might contain an inbound handle. */
        }
        if (!rewrite_fields(av_obj_value_at(components, i), type, from, to, check, 0)) {
            return false;
        }
    }
    return true;
}
static bool rewrite_tree(
    const prototype_registry *registry,
    av_value *array,
    const av_value *skip,
    const char *from,
    const char *to,
    bool check,
    int depth
)
{
    if (depth > SCENE_DEPTH_MAX) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(array); i++) {
        av_value *item = av_arr_get(array, i);
        if (item == skip) {
            continue;
        }
        if (!rewrite_components(registry, av_obj_get(item, "components"), from, to, check) ||
            !rewrite_components(registry, av_obj_get(item, "retained_components"), from, to, check) ||
            !rewrite_tree(registry, av_obj_get(item, "children"), skip, from, to, check, depth + 1)) {
            return false;
        }
    }
    return true;
}
static bool save(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const workspace_document_view *view,
    av_doc *parsed,
    char *error,
    size_t size
)
{
    av_value *root = av_doc_root(parsed);
    if (av_obj_get(root, "entities")) {
        scene_document *scene = scene_document_load_value(root, error, size);
        if (!scene) {
            return false;
        }
        scene_document_free(scene);
    } else {
        char *source = av_doc_write(parsed);
        prototype *asset = prototype_load_authored(source, registry, error, size);
        free(source);
        if (!asset) {
            return false;
        }
        prototype_free(asset);
    }
    char *text = av_doc_write(parsed);
    workspace_document_status status = workspace_document_edit(
        documents,
        document,
        revision,
        0,
        (int)view->bytes,
        text
    );
    free(text);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, size, workspace_document_status_text(status));
    }
    if (size) {
        *error = 0;
    }
    return true;
}
bool workspace_scene_edit_registry(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    workspace_scene_operation operation,
    const char *entity,
    const char *parent,
    const char *before,
    const char *name,
    const char *prototype,
    char *error,
    size_t size
)
{
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    if (!registry) {
        registry = &native;
    }

    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return fail(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    av_doc *parsed = av_doc_read(view.text, error, size);
    if (!parsed) {
        return false;
    }
    av_value *root = av_doc_root(parsed), *roots = av_obj_get(root, "entities");
    bool prototype_root = !roots;
    if (prototype_root) {
        if (!av_is_str(av_obj_get(root, "name"))) {
            av_doc_free(parsed);
            return fail(error, size, "Hierarchy operation requires a scene or prototype document");
        }
        roots = av_obj_get(root, "children");
        if (!roots && operation == WORKSPACE_SCENE_ADD) {
            roots = av_arr(parsed);
            av_obj_set(parsed, root, "children", roots);
        }
    }
    if (!av_is_arr(roots)) {
        av_doc_free(parsed);
        return fail(error, size, "The document has no authored child objects");
    }
    bool ok = false;
    char *from = NULL, *to = NULL, *parent_name = NULL;
    av_value *source = NULL, *source_holder = NULL, *destination = roots;
    size_t source_index = 0, unused = 0;
    if (operation != WORKSPACE_SCENE_ADD) {
        source = at_path(root, entity);
        if (!source ||
            !find_entity(roots, source, "", &from, &source_holder, &source_index, 0)) {
            fail(error, size, "Scene object path does not resolve");
            goto done;
        }
    }
    if (operation == WORKSPACE_SCENE_ADD || operation == WORKSPACE_SCENE_MOVE) {
        if (*parent) {
            av_value *holder = NULL;
            av_value *object = at_path(root, parent);
            if (!object ||
                !find_entity(roots, object, "", &parent_name, &holder, &unused, 0)) {
                fail(error, size, "Scene parent path does not resolve");
                goto done;
            }
            if (from && below(parent_name, from)) {
                fail(error, size, "Scene object cannot move into its own subtree");
                goto done;
            }
            destination = av_obj_get(object, "children");
            if (!destination) {
                destination = av_arr(parsed);
                av_obj_set(parsed, object, "children", destination);
            }
            if (!av_is_arr(destination)) {
                fail(error, size, "Scene parent children must be an array");
                goto done;
            }
        }
    }
    if (operation == WORKSPACE_SCENE_ADD || operation == WORKSPACE_SCENE_RENAME ||
        operation == WORKSPACE_SCENE_DUPLICATE) {
        if (scene_entity_validate_name(name, false, error, size)) {
            goto done;
        }
    }
    if (operation == WORKSPACE_SCENE_ADD) {
        if (!prototype || !*prototype) {
            fail(error, size, "Scene object requires a prototype");
            goto done;
        }
        av_value *added = av_obj(parsed);
        av_obj_add_str(parsed, added, "name", name);
        av_obj_add_str(parsed, added, "prototype", prototype);
        av_arr_append(destination, added);
        ok = save(registry, documents, document, revision, &view, parsed, error, size);
        goto done;
    }
    if (operation == WORKSPACE_SCENE_REMOVE) {
        if ((prototype_root &&
             !rewrite_components(registry, av_obj_get(root, "components"), from, "", true)) ||
            !rewrite_tree(registry, roots, source, from, "", true, 0)) {
            fail(error, size, "Scene object is referenced by a handle binding or unreflected component");
            goto done;
        }
        av_arr_remove(source_holder, source_index);
        ok = save(registry, documents, document, revision, &view, parsed, error, size);
        goto done;
    }
    if (operation == WORKSPACE_SCENE_RENAME || operation == WORKSPACE_SCENE_DUPLICATE) {
        const char *last = strrchr(from, '.');
        size_t prefix = last ? (size_t)(last - from) + 1 : 0;
        to = xmalloc(prefix + strlen(name) + 1);
        memcpy(to, from, prefix);
        strcpy(to + prefix, name);
        if (operation == WORKSPACE_SCENE_RENAME) {
            if ((prototype_root &&
                 !rewrite_components(registry, av_obj_get(root, "components"), from, to, false)) ||
                !rewrite_tree(registry, roots, NULL, from, to, false, 0)) {
                fail(error, size, "Scene binding metadata is unavailable for a component");
                goto done;
            }
            av_obj_set(parsed, source, "name", av_str(parsed, name));
        } else {
            av_value *duplicate = av_clone(parsed, source);
            av_obj_set(parsed, duplicate, "name", av_str(parsed, name));
            av_value *temporary = av_arr(parsed);
            av_arr_append(temporary, duplicate);
            if (!rewrite_tree(registry, temporary, NULL, from, to, false, 0)) {
                fail(error, size, "Scene binding metadata is unavailable for a component");
                goto done;
            }
            av_arr_append(source_holder, duplicate);
        }
        ok = save(registry, documents, document, revision, &view, parsed, error, size);
        goto done;
    }
    if (operation == WORKSPACE_SCENE_MOVE) {
        av_value *before_object = *before ? at_path(root, before) : NULL;
        if (*before && (!before_object || before_object == source)) {
            fail(error, size, "Scene insertion sibling is absent or selected");
            goto done;
        }
        bool before_found = !*before;
        for (size_t i = 0; i < av_arr_size(destination); i++) {
            if (av_arr_get(destination, i) == before_object) {
                before_found = true;
            }
        }
        if (!before_found) {
            fail(error, size, "Scene insertion target must be a sibling of the destination");
            goto done;
        }
        const char *source_name = av_get_str(av_obj_get(source, "name"));
        size_t bytes = (parent_name ? strlen(parent_name) : 0) +
                       strlen(source_name) + 2;
        to = xmalloc(bytes);
        snprintf(to, bytes, "%s%s%s", parent_name ? parent_name : "", parent_name ? "." : "", source_name);
        if ((prototype_root &&
             !rewrite_components(registry, av_obj_get(root, "components"), from, to, false)) ||
            !rewrite_tree(registry, roots, NULL, from, to, false, 0)) {
            fail(error, size, "Scene binding metadata is unavailable for a component");
            goto done;
        }
        av_arr_remove(source_holder, source_index);
        /* Rebuild sibling order without retaining positional indices across removal. */
        av_value *ordered = av_arr(parsed);
        bool inserted = false;
        for (size_t i = 0; i < av_arr_size(destination); i++) {
            av_value *item = av_arr_get(destination, i);
            if (item == before_object) {
                av_arr_append(ordered, source);
                inserted = true;
            }
            av_arr_append(ordered, item);
        }
        if (!inserted) {
            av_arr_append(ordered, source);
        }
        while (av_arr_size(destination)) {
            av_arr_remove(destination, av_arr_size(destination) - 1);
        }
        for (size_t i = 0; i < av_arr_size(ordered); i++) {
            av_arr_append(destination, av_arr_get(ordered, i));
        }
        ok = save(registry, documents, document, revision, &view, parsed, error, size);
        goto done;
    }
    fail(error, size, "Unknown scene operation");
done:
    free(from);
    free(to);
    free(parent_name);
    av_doc_free(parsed);
    return ok;
}

bool workspace_asset_component_registry(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    const char *component,
    bool add,
    char *error,
    size_t size
)
{
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    if (!registry) {
        registry = &native;
    }

    if (strcmp(kind, "scene") && strcmp(kind, "prototype")) {
        return fail(error, size, "Components require a scene or prototype");
    }
    const ecs_component_desc *desc = ecs_component_catalog_find(
        (ecs_component_catalog){ registry->components, registry->component_count },
        component
    );
    if (!desc || desc->derived_from) {
        return fail(error, size, "Component is unknown or computed");
    }
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return fail(error, size, workspace_document_status_text(status));
    }
    if (view.revision != revision) {
        return fail(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    av_doc *parsed = av_doc_read(view.text, error, size);
    if (!parsed) {
        return false;
    }
    av_value *object = at_path(av_doc_root(parsed), entity);
    if (!av_is_obj(object)) {
        av_doc_free(parsed);
        return fail(error, size, "Component owner path does not resolve");
    }
    av_value *components = av_obj_get(object, "components");
    if (!components) {
        components = av_obj(parsed);
        av_obj_set(parsed, object, "components", components);
    }
    if (!av_is_obj(components)) {
        av_doc_free(parsed);
        return fail(error, size, "Components must be an object");
    }
    const char *existing = av_obj_get(components, desc->key) ? desc->key : desc->name;
    if (add) {
        if (av_obj_get(components, existing)) {
            av_doc_free(parsed);
            return fail(error, size, "Component is already authored");
        }
        av_value *defaults = prototype_native_defaults_value(registry, desc);
        av_value *fields = av_obj_get(defaults, desc->key ? desc->key : desc->name);
        if (!fields) {
            fields = av_obj_get(defaults, desc->name);
        }
        if (!av_is_obj(fields)) {
            av_value_free(defaults);
            av_doc_free(parsed);
            return fail(error, size, "Component defaults are unavailable");
        }
        av_obj_set(parsed, components, desc->name, av_clone(parsed, fields));
        av_value_free(defaults);
    } else if (!av_obj_remove_key(components, existing)) {
        av_doc_free(parsed);
        return fail(error, size, "Component has no local override to remove");
    }
    char path[1024];
    if (snprintf(path, sizeof path, "%s/components", entity) >= (int)sizeof path) {
        av_doc_free(parsed);
        return fail(error, size, "Component owner path is too long");
    }
    char *literal = av_value_write(components);
    bool ok = workspace_asset_edit_registry(
        registry,
        documents,
        document,
        revision,
        kind,
        "set",
        path,
        literal,
        error,
        size
    );
    free(literal);
    av_doc_free(parsed);
    return ok;
}

bool workspace_scene_edit(
    workspace_documents *documents,
    int document,
    int revision,
    workspace_scene_operation operation,
    const char *entity,
    const char *parent,
    const char *before,
    const char *name,
    const char *prototype,
    char *error,
    size_t size
)
{
    return workspace_scene_edit_registry(
        NULL,
        documents,
        document,
        revision,
        operation,
        entity,
        parent,
        before,
        name,
        prototype,
        error,
        size
    );
}
bool workspace_asset_component(
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    const char *component,
    bool add,
    char *error,
    size_t size
)
{
    return workspace_asset_component_registry(
        NULL,
        documents,
        document,
        revision,
        kind,
        entity,
        component,
        add,
        error,
        size
    );
}

#define CLIPBOARD_BYTES_MAX (1024u * 1024u)
#define CLIPBOARD_ROOTS_MAX 256
static av_doc *clipboard_document(
    const workspace_documents *documents,
    int document,
    int revision,
    workspace_document_view *view,
    char *error,
    size_t size
)
{
    workspace_document_status status = workspace_document_read(documents, document, view);
    if (status != WORKSPACE_DOCUMENT_OK || view->revision != revision) {
        fail(error, size, workspace_document_status_text(status != WORKSPACE_DOCUMENT_OK ? status : WORKSPACE_DOCUMENT_STALE));
        return NULL;
    }
    if (view->bytes > CLIPBOARD_BYTES_MAX) {
        fail(error, size, "Scene clipboard source exceeds 1MiB");
        return NULL;
    }
    return av_doc_read(view->text, error, size);
}
static av_value *clipboard_roots(av_doc *doc, bool create)
{
    av_value *root = av_doc_root(doc), *roots = av_obj_get(root, "entities");
    if (!roots) {
        roots = av_obj_get(root, "children");
        if (!roots && create && av_is_str(av_obj_get(root, "name"))) {
            roots = av_arr(doc);
            av_obj_set(doc, root, "children", roots);
        }
    }
    return av_is_arr(roots) ? roots : NULL;
}
static bool clipboard_validate(const prototype_registry *registry, av_doc *doc, char *error, size_t size)
{
    if (av_obj_get(av_doc_root(doc), "entities")) {
        scene_document *scene = scene_document_load_value(av_doc_root(doc), error, size);
        if (!scene) {
            return false;
        }
        scene_document_free(scene);
        return true;
    }
    char *text = av_doc_write(doc);
    prototype *asset = prototype_load_authored(text, registry, error, size);
    free(text);
    if (!asset) {
        return false;
    }
    prototype_free(asset);
    return true;
}
bool workspace_scene_clipboard(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *const *entities,
    int count,
    bool cut,
    char **payload,
    char **text,
    char *error,
    size_t size
)
{
    *payload = NULL;
    *text = NULL;
    if (!registry || !entities || count < 1 || count > CLIPBOARD_ROOTS_MAX) {
        return fail(error, size, "Scene clipboard requires one to 256 objects and a reflected registry");
    }
    workspace_document_view view;
    av_doc *source = clipboard_document(documents, document, revision, &view, error, size);
    if (!source) {
        return false;
    }
    av_value *roots = clipboard_roots(source, false);
    av_value *objects[CLIPBOARD_ROOTS_MAX] = { 0 };
    char *names[CLIPBOARD_ROOTS_MAX] = { 0 };
    bool ok = false;
    av_doc *clip = av_doc_new();
    av_doc_set_root(clip, av_obj(clip));
    av_obj_add_str(clip, av_doc_root(clip), "format", "chibi-scene-clipboard-1");
    av_value *items = av_arr(clip);
    av_obj_set(clip, av_doc_root(clip), "objects", items);
    for (int i = 0; i < count; i++) {
        av_value *holder = NULL;
        size_t index = 0;
        if (!entities[i]) {
            fail(error, size, "Clipboard selection contains an empty path");
            goto done;
        }
        objects[i] = at_path(av_doc_root(source), entities[i]);
        if (!objects[i] ||
            !find_entity(roots, objects[i], "", &names[i], &holder, &index, 0)) {
            fail(error, size, "Clipboard selection contains an absent object");
            goto done;
        }
    }
    for (int i = 0; i < count; i++) {
        bool nested = false;
        for (int j = 0; j < count; j++) {
            if (i != j && below(names[i], names[j]) &&
                (strcmp(names[i], names[j]) || j < i)) {
                nested = true;
                break;
            }
        }
        if (nested) {
            continue;
        }
        av_value *entry = av_obj(clip);
        av_obj_add_str(clip, entry, "source", names[i]);
        av_obj_set(clip, entry, "object", av_clone(clip, objects[i]));
        av_arr_append(items, entry);
        if (cut) {
            av_value *holder = NULL;
            size_t index = 0;
            char *path = NULL;
            if (!find_entity(roots, objects[i], "", &path, &holder, &index, 0)) {
                free(path);
                goto done;
            }
            free(path);
            av_arr_remove(holder, index);
        }
    }
    if (cut) {
        for (size_t i = 0; i < av_arr_size(items); i++) {
            const char *from = av_get_str(av_obj_get(av_arr_get(items, i), "source"));
            if (!rewrite_components(registry, av_obj_get(av_doc_root(source), "components"), from, "", true) ||
                !rewrite_tree(registry, roots, NULL, from, "", true, 0)) {
                fail(error, size, "Cut objects are referenced by retained handles or unreflected components");
                goto done;
            }
        }
        if (!clipboard_validate(registry, source, error, size)) {
            goto done;
        }
    }
    *payload = av_doc_write(clip);
    *text = cut ? av_doc_write(source) : xstrdup(view.text);
    ok = strlen(*payload) <= CLIPBOARD_BYTES_MAX && strlen(*text) <= CLIPBOARD_BYTES_MAX;
    if (!ok) {
        free(*payload);
        free(*text);
        *payload = NULL;
        *text = NULL;
        fail(error, size, "Scene clipboard exceeds 1MiB");
    }
done:
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    av_doc_free(clip);
    av_doc_free(source);
    return ok;
}
static int clipboard_compare_names(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}
static size_t clipboard_name_index(const char *const *names, size_t count, const char *name)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (strcmp(names[middle], name) < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}
static bool clipboard_source_path(const char *path, const char *name, char *error, size_t size)
{
    if (!path || strlen(path) > 4096) {
        return fail(error, size, "Clipboard source path is too long");
    }
    const char *part = path;
    int depth = 0;
    while (*part) {
        const char *end = strchr(part, '.');
        size_t count = end ? (size_t)(end - part) : strlen(part);
        char segment[401];
        if (!count || count >= sizeof segment || ++depth > SCENE_DEPTH_MAX + 1) {
            return fail(error, size, "Clipboard source path is invalid");
        }
        memcpy(segment, part, count);
        segment[count] = 0;
        if (scene_entity_validate_name(segment, false, error, size)) {
            return false;
        }
        if (!end) {
            return !strcmp(segment, name) ||
                   fail(error, size, "Clipboard source name does not match its object");
        }
        part = end + 1;
    }
    return fail(error, size, "Clipboard source path is empty");
}
static bool clipboard_rewrite_fields(
    av_value *fields,
    const type_desc *type,
    char *const *from,
    char *const *to,
    size_t count,
    int depth
)
{
    if (!av_is_obj(fields)) {
        return true;
    }
    if (!type || depth > TYPE_MAX_NESTING) {
        return false;
    }
    for (int f = 0; f < type->field_count; f++) {
        const type_field *field = &type->fields[f];
        av_value *value = av_obj_get(fields, field->name);
        if (field->type == PIN_STRUCT &&
            !clipboard_rewrite_fields(value, type_catalog_find(field->type_name), from, to, count, depth + 1)) {
            return false;
        }
        if (field->type != PIN_HANDLE || !av_is_str(value)) {
            continue;
        }
        for (size_t i = 0; i < count; i++) {
            const char *original = av_get_str(value);
            if (!below(original, from[i])) {
                continue;
            }
            const char *rest = original + strlen(from[i]);
            size_t bytes = strlen(to[i]) + strlen(rest) + 1;
            char *replacement = xmalloc(bytes);
            snprintf(replacement, bytes, "%s%s", to[i], rest);
            av_set_str(value, replacement);
            free(replacement);
            break;
        }
    }
    return true;
}
static bool clipboard_rewrite_tree(
    const prototype_registry *registry,
    av_value *items,
    char *const *from,
    char *const *to,
    size_t count,
    int depth
)
{
    if (depth > SCENE_DEPTH_MAX) {
        return false;
    }
    for (size_t i = 0; i < av_arr_size(items); i++) {
        av_value *object = av_arr_get(items, i);
        const char *groups[] = { "components", "retained_components" };
        for (int g = 0; g < 2; g++) {
            av_value *components = av_obj_get(object, groups[g]);
            for (size_t c = 0; c < av_obj_size(components); c++) {
                const char *key = av_get_str(av_obj_key_at(components, c));
                const ecs_component_desc *component = ecs_component_catalog_find(
                    (ecs_component_catalog){ registry->components,
                                             registry->component_count },
                    key
                );
                const type_desc *type = component ? component->type
                                                  : type_catalog_find(key);
                if (!clipboard_rewrite_fields(av_obj_value_at(components, c), type, from, to, count, 0)) {
                    return false;
                }
            }
        }
        if (!clipboard_rewrite_tree(registry, av_obj_get(object, "children"), from, to, count, depth + 1)) {
            return false;
        }
    }
    return true;
}
bool workspace_scene_paste(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *parent,
    const char *payload,
    char **text,
    char ***paths,
    int *count,
    char *error,
    size_t size
)
{
    *text = NULL;
    *paths = NULL;
    *count = 0;
    if (!registry || !parent || !payload || strlen(payload) > CLIPBOARD_BYTES_MAX) {
        return fail(error, size, "Invalid or oversized scene clipboard");
    }
    workspace_document_view view;
    av_doc *source = clipboard_document(documents, document, revision, &view, error, size);
    if (!source) {
        return false;
    }
    av_doc *clip = av_doc_read(payload, error, size);
    av_value *clip_root = clip ? av_doc_root(clip) : NULL;
    const char *format = av_get_str(av_obj_get(clip_root, "format"));
    av_value *items = av_obj_get(clip_root, "objects");
    av_value *roots = clipboard_roots(source, true), *destination = roots;
    char *parent_name = NULL, *from[CLIPBOARD_ROOTS_MAX] = { 0 },
         *to[CLIPBOARD_ROOTS_MAX] = { 0 };
    bool ok = false;
    size_t n = av_arr_size(items);
    if (!format || strcmp(format, "chibi-scene-clipboard-1") || !roots || !n ||
        n > CLIPBOARD_ROOTS_MAX) {
        fail(error, size, "Clipboard does not contain a bounded Chibi object selection");
        goto done;
    }
    if (*parent) {
        av_value *holder = NULL, *object = at_path(av_doc_root(source), parent);
        size_t index = 0;
        if (!object ||
            !find_entity(roots, object, "", &parent_name, &holder, &index, 0)) {
            fail(error, size, "Paste parent is absent");
            goto done;
        }
        destination = av_obj_get(object, "children");
        if (!destination) {
            destination = av_arr(source);
            av_obj_set(source, object, "children", destination);
        }
        if (!av_is_arr(destination)) {
            fail(error, size, "Paste parent children must be an array");
            goto done;
        }
    }
    av_value *added = av_arr(source);
    size_t first = av_arr_size(destination);
    if (first + n > 4096) {
        fail(error, size, "Paste supports at most4096siblings");
        goto done;
    }
    const char *name_index[4096];
    size_t name_count = first;
    for (size_t i = 0; i < first; i++) {
        name_index[i] = av_get_str(av_obj_get(av_arr_get(destination, i), "name"));
        if (!name_index[i]) {
            fail(error, size, "Paste destination contains an unnamed object");
            goto done;
        }
    }
    qsort(name_index, name_count, sizeof *name_index, clipboard_compare_names);
    for (size_t i = 0; i < n; i++) {
        av_value *entry = av_arr_get(items, i), *object = av_obj_get(entry, "object");
        const char *name = av_get_str(av_obj_get(object, "name")),
                   *old = av_get_str(av_obj_get(entry, "source"));
        if (!name || !old) {
            fail(error, size, "Clipboard root requires an object name and source path");
            goto done;
        }
        if (scene_entity_validate_name(name, false, error, size) ||
            !clipboard_source_path(old, name, error, size)) {
            goto done;
        }
        for (size_t j = 0; j < i; j++) {
            if (below(old, from[j]) || below(from[j], old)) {
                fail(error, size, "Clipboard roots overlap");
                goto done;
            }
        }
        char unique[512];
        if (strlen(name) > 400 || strlen(old) > 4096) {
            fail(error, size, "Clipboard object name is too long");
            goto done;
        }
        snprintf(unique, sizeof unique, "%s", name);
        int suffix = 2;
        size_t insertion;
        while ((insertion = clipboard_name_index(name_index, name_count, unique)) < name_count &&
               !strcmp(name_index[insertion], unique)) {
            if (suffix > 4098) {
                fail(error, size, "Paste has too many name collisions");
                goto done;
            }
            snprintf(unique, sizeof unique, "%s%d", name, suffix++);
        }
        av_value *copy = av_clone(source, object);
        av_obj_set(source, copy, "name", av_str(source, unique));
        av_arr_append(destination, copy);
        av_arr_append(added, copy);
        memmove(
            &name_index[insertion + 1],
            &name_index[insertion],
            (name_count - insertion) * sizeof *name_index
        );
        name_index[insertion] = av_get_str(av_obj_get(copy, "name"));
        name_count++;
        from[i] = xstrdup(old);
        size_t bytes = (parent_name ? strlen(parent_name) : 0) + strlen(unique) + 2;
        to[i] = xmalloc(bytes);
        snprintf(to[i], bytes, "%s%s%s", parent_name ? parent_name : "", parent_name ? "." : "", unique);
    }
    if (!clipboard_rewrite_tree(registry, added, from, to, n, 0)) {
        fail(error, size, "Clipboard binding metadata is unavailable");
        goto done;
    }
    if (!clipboard_validate(registry, source, error, size)) {
        goto done;
    }
    *text = av_doc_write(source);
    if (strlen(*text) > CLIPBOARD_BYTES_MAX) {
        free(*text);
        *text = NULL;
        fail(error, size, "Pasted document exceeds 1MiB");
        goto done;
    }
    *paths = xcalloc(n, sizeof **paths);
    const char *root_path = av_obj_get(av_doc_root(source), "entities")
                              ? "/entities"
                              : "/children";
    for (size_t i = 0; i < n; i++) {
        char path[8192];
        snprintf(
            path,
            sizeof path,
            "%s%s/%zu",
            *parent ? parent : root_path,
            *parent ? "/children" : "",
            first + i
        );
        (*paths)[i] = xstrdup(path);
    }
    *count = (int)n;
    ok = true;
done:
    for (int i = 0; i < CLIPBOARD_ROOTS_MAX; i++) {
        free(from[i]);
        free(to[i]);
    }
    free(parent_name);
    av_doc_free(clip);
    av_doc_free(source);
    return ok;
}

bool workspace_scene_place_candidate(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *parent,
    const char *prototype_name,
    const char *name,
    float x,
    float y,
    float z,
    const char *source,
    char **text,
    char ***paths,
    int *count,
    char *error,
    size_t size
)
{
    *text = NULL;
    *paths = NULL;
    *count = 0;
    if (!registry || !parent || !prototype_name || !name || !isfinite(x) ||
        !isfinite(y) || !isfinite(z) ||
        scene_entity_validate_name(name, false, error, size)) {
        return fail(error, size, "Placement requires a valid name and finite parent-local position");
    }
    workspace_document_view original;
    av_doc *current = clipboard_document(documents, document, revision, &original, error, size);
    if (!current) {
        return false;
    }
    av_doc_free(current);
    const char *base_source = registry->include
                                ? registry->include(prototype_name, registry->include_user)
                                : NULL;
    if (!base_source) {
        return fail(error, size, "Placement prototype is unavailable in this project");
    }
    prototype *base = prototype_load(base_source, registry, error, size);
    if (!base) {
        return false;
    }
    av_value *inherited = prototype_component_value(base, "Transform3D");
    bool has_transform = av_obj_size(inherited) > 0;
    av_value_free(inherited);
    prototype_free(base);
    av_doc *clip = av_doc_new();
    av_value *root = av_obj(clip);
    av_doc_set_root(clip, root);
    av_obj_add_str(clip, root, "format", "chibi-scene-clipboard-1");
    av_value *items = av_obj_add_arr(clip, root, "objects"),
             *entry = av_arr_add_obj(clip, items);
    av_obj_add_str(clip, entry, "source", name);
    av_value *object = av_obj_add_obj(clip, entry, "object");
    av_obj_add_str(clip, object, "name", name);
    av_obj_add_str(clip, object, "prototype", prototype_name);
    av_value *components = av_obj_add_obj(clip, object, "components"), *transform;
    if (has_transform) {
        transform = av_obj_add_obj(clip, components, "Transform3D");
    } else {
        const ecs_component_desc *descriptor = prototype_registry_find(registry, "Transform3D");
        av_value *defaults = descriptor
                               ? prototype_native_defaults_value(registry, descriptor)
                               : NULL;
        av_value *fields = descriptor && defaults
                             ? av_obj_get(
                                   defaults,
                                   descriptor->key ? descriptor->key
                                                   : descriptor->name
                               )
                             : NULL;
        if (!fields && descriptor) {
            fields = av_obj_get(defaults, descriptor->name);
        }
        if (!av_is_obj(fields)) {
            av_value_free(defaults);
            av_doc_free(clip);
            return fail(error, size, "Transform3D creation defaults are unavailable");
        }
        transform = av_clone(clip, fields);
        av_value_free(defaults);
        av_obj_set(clip, components, "Transform3D", transform);
        av_value *scale = av_obj(clip);
        av_obj_set(clip, transform, "scale", scale);
        av_obj_add_float(clip, scale, "x", 1);
        av_obj_add_float(clip, scale, "y", 1);
        av_obj_add_float(clip, scale, "z", 1);
    }
    av_value *position = av_obj(clip);
    av_obj_add_float(clip, position, "x", x);
    av_obj_add_float(clip, position, "y", y);
    av_obj_add_float(clip, position, "z", z);
    av_obj_set(clip, transform, "position", position);
    char *payload = av_doc_write(clip);
    av_doc_free(clip);
    const char *working = source && *source ? source : original.text;
    workspace_documents *draft = workspace_documents_new(NULL, NULL);
    int handle = 0;
    bool ok = strlen(working) <= CLIPBOARD_BYTES_MAX && draft &&
              workspace_document_memory(draft, working, &handle) == WORKSPACE_DOCUMENT_OK;
    if (ok) {
        ok = workspace_scene_paste(registry, draft, handle, 1, parent, payload, text, paths, count, error, size);
    } else {
        fail(error, size, "Placement draft is unavailable or exceeds1MiB");
    }
    free(payload);
    workspace_documents_free(draft);
    return ok;
}
