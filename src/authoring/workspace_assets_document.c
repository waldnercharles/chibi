#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/component_presentation.h"
#include "authoring/workspace_assets.h"
#include "authoring/workspace_presentation.h"
#include "graph/document/serialize.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool refuse(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static const asset_schema *schema_for(const char *kind, char *error, size_t size)
{
    const asset_schema *schema = asset_schema_find(kind);
    if (!schema || schema->executable || !schema->field_count) {
        refuse(error, size, "This operation requires a declarative KDL asset schema");
        return NULL;
    }
    return schema;
}
static bool read_document(
    const workspace_documents *documents,
    int document,
    int revision,
    workspace_document_view *view,
    char *error,
    size_t size
)
{
    workspace_document_status status = workspace_document_read(documents, document, view);
    if (status != WORKSPACE_DOCUMENT_OK) {
        return refuse(error, size, workspace_document_status_text(status));
    }
    if (view->revision != revision) {
        return refuse(error, size, workspace_document_status_text(WORKSPACE_DOCUMENT_STALE));
    }
    return true;
}
static char *store(arena *storage, const char *value)
{
    if (!value) {
        value = "";
    }
    char *out = arena_alloc(storage, strlen(value) + 1);
    strcpy(out, value);
    return out;
}
static bool path_join(const char *parent, const char *name, char *out, size_t size)
{
    size_t at = strlen(parent);
    if (at + 2 > size) {
        return false;
    }
    memcpy(out, parent, at);
    out[at++] = '/';
    for (const char *p = name; *p; p++) {
        if (at + 3 > size) {
            return false;
        }
        if (*p == '~' || *p == '/') {
            out[at++] = '~';
            out[at++] = *p == '~' ? '0' : '1';
        } else {
            out[at++] = *p;
        }
    }
    out[at] = 0;
    return true;
}
static const char *value_type(const av_value *value, const asset_schema_field *field)
{
    if (!value) {
        return field ? asset_schema_value_name(field->type) : "any";
    }
    switch (av_get_type(value)) {
        case AV_NULL: return "null";
        case AV_BOOL: return "bool";
        case AV_SINT:
        case AV_UINT: return "integer";
        case AV_REAL: return "number";
        case AV_STRING: return "string";
        case AV_OBJECT: return "object";
        case AV_ARRAY: return "array";
    }
    return "any";
}
static bool inspect_value(
    workspace_asset_inspection *out,
    const av_value *value,
    const char *path,
    const char *name,
    int parent,
    int depth,
    const asset_schema_field *field,
    const asset_schema *schema,
    const type_desc *reflected,
    const type_field *native,
    char *error,
    size_t size
)
{
    if (depth > 32 || acount(out->fields) >= 4096) {
        return refuse(error, size, "Asset inspection exceeds 4096 fields or 32 levels");
    }
    int index = acount(out->fields);
    const char *native_type = !native                  ? NULL
                            : native->type == PIN_BOOL ? "bool"
                            : native->type >= PIN_I8 && native->type <= PIN_U64
                                ? "integer"
                            : native->type == PIN_F32 || native->type == PIN_F64
                                ? "number"
                            : native->type == PIN_STRUCT ? "object"
                                                         : "string";
    workspace_asset_field row = {
        .path = store(&out->storage, path),
        .relative = store(&out->storage, path),
        .common = true,
        .name = store(&out->storage, name),
        .type = store(&out->storage, !value && native ? native_type : value_type(value, field)),
        .text = store(&out->storage, av_is_str(value) ? av_get_str(value) : ""),
        .description = store(
            &out->storage,
            native  ? native->description
            : field ? field->description
                    : ""
        ),
        .reference = store(
            &out->storage,
            native && native->type == PIN_HANDLE ? "entity"
            : field                              ? field->reference_kind
                                                 : ""
        ),
        .type_name = store(&out->storage, native ? native->type_name : ""),
        .unit = store(&out->storage, native ? native->unit : ""),
        .bounded = native && native->max > native->min,
        .min = native ? native->min : 0,
        .max = native ? native->max : 0,
        .color = native && native->color,
        .parent = parent,
        .depth = depth,
        .present = value != NULL,
        .required = field && field->required,
        .boolean = av_is_bool(value) && av_get_bool(value),
        .number = av_is_num(value) ? av_get_num(value) : 0
    };
    row.json = store(&out->storage, "");
    if (value && !av_is_ctn(value)) {
        char *json = av_json_write(value);
        row.json = store(&out->storage, json);
        free(json);
    }
    if (value && !av_is_ctn(value) && !av_is_str(value)) {
        char *literal = av_value_write(value);
        row.text = store(&out->storage, literal);
        free(literal);
    }
    apush(out->fields, row);
    if (field) {
        for (int i = 0; i < field->choice_count; i++) {
            apush(
                out->choices,
                ((workspace_asset_choice){ row.path,
                                           store(&out->storage, field->choices[i]) })
            );
        }
    }
    if (native && native->type == PIN_ENUM) {
        for (int i = 0; i < type_catalog_enum_count(); i++) {
            const enum_desc *e = type_catalog_enum_at(i);
            if (!native->type_name || strcmp(e->name, native->type_name)) {
                continue;
            }
            for (int j = 0; j < e->value_count; j++) {
                apush(
                    out->choices,
                    ((workspace_asset_choice){ row.path,
                                               store(&out->storage, e->values[j].name) })
                );
            }
        }
    }
    const asset_schema *children = field ? field->object : schema;
    if (av_is_obj(value)) {
        for (size_t i = 0; i < av_obj_size(value); i++) {
            const char *key = av_get_str(av_obj_key_at(value, i));
            char child[1024];
            if (!path_join(path, key, child, sizeof child)) {
                return refuse(error, size, "Asset field path exceeds 1023 bytes");
            }
            const asset_schema_field *meta = children
                                               ? asset_schema_field_find(children, key)
                                               : NULL;
            const type_field *native_field = NULL;
            for (int f = 0; reflected && f < reflected->field_count; f++) {
                if (!strcmp(reflected->fields[f].name, key)) {
                    native_field = &reflected->fields[f];
                    break;
                }
            }
            const type_desc *child_type = native_field && native_field->type == PIN_STRUCT
                                            ? type_catalog_find(native_field->type_name)
                                            : NULL;
            if (children && !strcmp(children->kind, "components")) {
                const ecs_component_desc *component = ecs_component_catalog_find(
                    (ecs_component_catalog){ out->registry->components,
                                             out->registry->component_count },
                    key
                );
                child_type = component ? component->type : NULL;
            }
            if (!inspect_value(
                    out,
                    av_obj_value_at(value, i),
                    child,
                    key,
                    index,
                    depth + 1,
                    meta,
                    NULL,
                    child_type,
                    native_field,
                    error,
                    size
                )) {
                return false;
            }
        }
        if (children) {
            for (int i = 0; i < children->field_count; i++) {
                const asset_schema_field *meta = &children->fields[i];
                if (av_obj_get(value, meta->name)) {
                    continue;
                }
                char child[1024];
                if (!path_join(path, meta->name, child, sizeof child)) {
                    return refuse(error, size, "Asset field path exceeds 1023 bytes");
                }
                if (!inspect_value(out, NULL, child, meta->name, index, depth + 1, meta, NULL, NULL, NULL, error, size)) {
                    return false;
                }
            }
        }
    }
    if (av_is_obj(value) && reflected) {
        for (int i = 0; i < reflected->field_count; i++) {
            const type_field *native_field = &reflected->fields[i];
            if (av_obj_get(value, native_field->name)) {
                continue;
            }
            char child[1024];
            if (!path_join(path, native_field->name, child, sizeof child)) {
                return refuse(error, size, "Asset field path exceeds 1023 bytes");
            }
            if (!inspect_value(
                    out,
                    NULL,
                    child,
                    native_field->name,
                    index,
                    depth + 1,
                    NULL,
                    NULL,
                    NULL,
                    native_field,
                    error,
                    size
                )) {
                return false;
            }
        }
    }
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            char name_text[32], child[1024];
            snprintf(name_text, sizeof name_text, "%zu", i);
            if (!path_join(path, name_text, child, sizeof child)) {
                return refuse(error, size, "Asset field path exceeds 1023 bytes");
            }
            if (!inspect_value(
                    out,
                    av_arr_get(value, i),
                    child,
                    name_text,
                    index,
                    depth + 1,
                    NULL,
                    field ? field->element_object : NULL,
                    NULL,
                    NULL,
                    error,
                    size
                )) {
                return false;
            }
        }
    }
    return true;
}
static bool present_fields(
    workspace_asset_inspection *out,
    const workspace_documents *documents,
    char *error,
    size_t size
)
{
    component_presentation presentation = { 0 };
    ecs_component_catalog catalog = { out->registry->components,
                                      out->registry->component_count };
    if (!workspace_presentation_load(documents, catalog, &presentation, error, size)) {
        return false;
    }
    for (int i = 0; i < acount(out->fields); i++) {
        workspace_asset_field *row = &out->fields[i];
        const ecs_component_desc *component = NULL;
        int group = i;
        while (group >= 0) {
            workspace_asset_field *ancestor = &out->fields[group];
            if (ancestor->parent >= 0 &&
                !strcmp(out->fields[ancestor->parent].name, "components")) {
                component = ecs_component_catalog_find(catalog, ancestor->name);
                break;
            }
            group = ancestor->parent;
        }
        row->computed = component && component->derived_from != 0;
        row->component = store(
            &out->storage,
            component ? (component->key ? component->key : component->name) : ""
        );
        row->presentation_category = store(
            &out->storage,
            component ? component_presentation_category(
                            component_presentation_tab(&presentation, component)
                        )
                      : ""
        );
        row->icon = store(
            &out->storage,
            component ? component_presentation_icon_name(
                            component_presentation_icon(&presentation, component)
                        )
                      : ""
        );
        if (component && row->parent == group && component->type) {
            for (int f = 0; f < component->type->field_count; f++) {
                if (strcmp(component->type->fields[f].name, row->name)) {
                    continue;
                }
                field_hint hint = component_presentation_field(&presentation, component, f);
                row->unit = store(&out->storage, hint.unit);
                row->min = hint.min;
                row->max = hint.max;
                row->bounded = hint.max > hint.min;
                break;
            }
        }
    }
    component_presentation_free(&presentation);
    return true;
}
bool workspace_asset_inspect_registry(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    workspace_asset_inspection *out,
    char *error,
    size_t size
)
{
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    *out = (workspace_asset_inspection){ .registry = registry ? registry : &native };
    const asset_schema *schema = schema_for(kind, error, size);
    if (!schema) {
        return false;
    }
    workspace_document_view view;
    if (!read_document(documents, document, revision, &view, error, size)) {
        return false;
    }
    av_doc *parsed = av_doc_read(view.text, error, size);
    if (!parsed) {
        return false;
    }
    bool ok = inspect_value(out, av_doc_root(parsed), "", kind, -1, 0, NULL, schema, NULL, NULL, error, size);
    ok = ok && present_fields(out, documents, error, size);
    av_doc_free(parsed);
    if (!ok) {
        workspace_asset_inspection_free(out);
        return false;
    }
    if (size) {
        *error = 0;
    }
    return true;
}
void workspace_asset_inspection_free(workspace_asset_inspection *out)
{
    afree(out->fields);
    afree(out->choices);
    arena_free(&out->storage);
    *out = (workspace_asset_inspection){ 0 };
}
static bool array_index(const char *name, size_t *index)
{
    if (!*name || (name[0] == '0' && name[1])) {
        return false;
    }
    size_t result = 0;
    for (const char *p = name; *p; p++) {
        if (*p < '0' || *p > '9' || result > (SIZE_MAX - 9) / 10) {
            return false;
        }
        result = result * 10 + (size_t)(*p - '0');
    }
    *index = result;
    return true;
}
static av_value *child_value(av_value *parent, const char *name)
{
    if (av_is_obj(parent)) {
        return av_obj_get(parent, name);
    }
    size_t index;
    return av_is_arr(parent) && array_index(name, &index) ? av_arr_get(parent, index)
                                                          : NULL;
}
static av_value *resolve(av_value *root, const char *path, char *leaf, size_t capacity)
{
    if (!path || *path != '/') {
        return NULL;
    }
    av_value *parent = root;
    const char *p = path + 1;
    for (;;) {
        size_t at = 0;
        while (*p && *p != '/') {
            char c = *p++;
            if (c == '~') {
                if (*p != '0' && *p != '1') {
                    return NULL;
                }
                c = *p++ == '0' ? '~' : '/';
            }
            if (at + 1 >= capacity) {
                return NULL;
            }
            leaf[at++] = c;
        }
        leaf[at] = 0;
        if (!*p) {
            return parent;
        }
        parent = child_value(parent, leaf);
        if (!parent) {
            return NULL;
        }
        p++;
    }
}
static bool validate(
    const prototype_registry *registry,
    const asset_schema *schema,
    const av_value *value,
    const char *source,
    char *error,
    size_t size
)
{
    asset_schema_diagnostic *diagnostics = asset_schema_validate(schema, value, source);
    bool ok = !acount(diagnostics);
    if (!ok) {
        refuse(error, size, diagnostics[0].message);
    }
    afree(diagnostics);
    if (!ok) {
        return false;
    }
    if (!strcmp(schema->kind, "scene")) {
        scene_document *scene = scene_document_load_value((av_value *)value, error, size);
        if (!scene) {
            return false;
        }
        scene_document_free(scene);
        return true;
    }
    if (!strcmp(schema->kind, "prototype")) {
        char *text = asset_text_write(value);
        prototype *prototype = prototype_load_authored(text, registry, error, size);
        free(text);
        if (!prototype) {
            return false;
        }
        prototype_free(prototype);
        return true;
    }
    if (!strcmp(schema->kind, "event") || !strcmp(schema->kind, "material") ||
        !strcmp(schema->kind, "shape") || !strcmp(schema->kind, "field") ||
        !strcmp(schema->kind, "shader-library") || !strcmp(schema->kind, "model")) {
        behavior_asset *asset = behavior_asset_from_value((av_value *)value);
        if (!asset) {
            return refuse(error, size, graph_last_error());
        }
        behavior_asset_free(asset);
    }
    return true;
}
bool workspace_asset_edit_registry(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char *error,
    size_t size
)
{
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    if (!registry) {
        registry = &native;
    }

    const asset_schema *schema = schema_for(kind, error, size);
    if (!schema) {
        return false;
    }
    workspace_document_view view;
    if (!read_document(documents, document, revision, &view, error, size)) {
        return false;
    }
    av_doc *parsed = av_doc_read(view.text, error, size);
    if (!parsed) {
        return false;
    }
    char leaf[1024];
    av_value *parent = resolve(av_doc_root(parsed), path, leaf, sizeof leaf);
    if (!parent) {
        av_doc_free(parsed);
        return refuse(error, size, "Asset path does not resolve to a parent");
    }
    bool string = !strcmp(operation, "set-text"),
         number = !strcmp(operation, "set-number"),
         boolean = !strcmp(operation, "set-bool");
    bool remove = !strcmp(operation, "remove"),
         append = !strcmp(operation, "append"),
         set = !strcmp(operation, "set") || string || number || boolean;
    if (!remove && !append && !set) {
        av_doc_free(parsed);
        return refuse(error, size, "Asset edit operation must be set, remove, append, set-text, set-number or set-bool");
    }
    av_doc *incoming = remove ? NULL
                     : string ? av_doc_new()
                              : av_value_read(literal, error, size);
    if (string) {
        av_doc_set_root(incoming, av_str(incoming, literal));
    }
    if (!remove && !incoming) {
        av_doc_free(parsed);
        return false;
    }
    if ((number && !av_is_num(av_doc_root(incoming))) ||
        (boolean && !av_is_bool(av_doc_root(incoming)))) {
        av_doc_free(incoming);
        av_doc_free(parsed);
        return refuse(error, size, "Typed asset edit value has the wrong scalar type");
    }
    if (set && (!strcmp(kind, "scene") || !strcmp(kind, "prototype")) &&
        !strcmp(leaf, "name") && av_obj_get(parent, "prototype") &&
        av_obj_get(parent, "name")) {
        if (!av_is_str(av_doc_root(incoming))) {
            av_doc_free(incoming);
            av_doc_free(parsed);
            return refuse(error, size, "Scene object names must be text");
        }
        size_t length = strlen(path);
        char *entity = xmalloc(length - 4);
        memcpy(entity, path, length - 5);
        entity[length - 5] = 0;
        bool ok = workspace_scene_edit_registry(
            registry,
            documents,
            document,
            revision,
            WORKSPACE_SCENE_RENAME,
            entity,
            "",
            "",
            av_get_str(av_doc_root(incoming)),
            "",
            error,
            size
        );
        free(entity);
        av_doc_free(incoming);
        av_doc_free(parsed);
        return ok;
    }
    av_value *replacement = incoming ? av_clone(parsed, av_doc_root(incoming)) : NULL;
    bool changed = false;
    size_t index;
    if (append) {
        av_value *array = child_value(parent, leaf);
        changed = av_is_arr(array) && av_arr_append(array, replacement);
    }
    if (set) {
        if (av_is_obj(parent)) {
            changed = av_obj_set(parsed, parent, leaf, replacement);
        } else if (av_is_arr(parent) && array_index(leaf, &index) && index < av_arr_size(parent)) {
            changed = av_arr_replace(parent, index, replacement) != NULL;
        }
    }
    if (remove) {
        if (av_is_obj(parent)) {
            changed = av_obj_remove_key(parent, leaf);
        } else if (av_is_arr(parent) && array_index(leaf, &index) && index < av_arr_size(parent)) {
            changed = av_arr_remove(parent, index) != NULL;
        }
    }
    av_doc_free(incoming);
    if (!changed) {
        av_doc_free(parsed);
        return refuse(error, size, "Asset edit target is absent or has the wrong container type");
    }
    char *candidate = av_doc_write(parsed);
    bool ok = validate(registry, schema, av_doc_root(parsed), candidate, error, size);
    if (ok) {
        workspace_document_status status = workspace_document_edit(
            documents,
            document,
            revision,
            0,
            (int)view.bytes,
            candidate
        );
        if (status != WORKSPACE_DOCUMENT_OK) {
            ok = refuse(error, size, workspace_document_status_text(status));
        }
    }
    free(candidate);
    av_doc_free(parsed);
    if (ok && size) {
        *error = 0;
    }
    return ok;
}

bool workspace_asset_inspect(
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    workspace_asset_inspection *out,
    char *error,
    size_t size
)
{
    return workspace_asset_inspect_registry(NULL, documents, document, revision, kind, out, error, size);
}
bool workspace_asset_prepare_registry(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char **text,
    char *error,
    size_t size
)
{
    if (!text) {
        return refuse(error, size, "Missing asset candidate output");
    }
    *text = NULL;
    workspace_document_view original;
    if (!read_document(documents, document, revision, &original, error, size)) {
        return false;
    }
    workspace_document_status status;
    workspace_documents *stage = workspace_documents_new(NULL, &status);
    int handle = 0;
    status = workspace_document_memory(stage, original.text, &handle);
    bool ok = status == WORKSPACE_DOCUMENT_OK;
    workspace_document_view view;
    if (ok) {
        workspace_document_read(stage, handle, &view);
        ok = workspace_asset_edit_registry(
            registry,
            stage,
            handle,
            view.revision,
            kind,
            operation,
            path,
            literal,
            error,
            size
        );
    } else {
        refuse(error, size, workspace_document_status_text(status));
    }
    if (ok) {
        workspace_document_read(stage, handle, &view);
        *text = xstrdup(view.text);
    }
    workspace_documents_free(stage);
    return ok;
}

bool workspace_asset_edit(
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *operation,
    const char *path,
    const char *literal,
    char *error,
    size_t size
)
{
    return workspace_asset_edit_registry(
        NULL,
        documents,
        document,
        revision,
        kind,
        operation,
        path,
        literal,
        error,
        size
    );
}

static bool scene_settings(
    av_doc *doc,
    av_value *object,
    const workspace_documents *documents,
    char *error,
    size_t size
)
{
    scene_document *scene = scene_document_load_value(object, error, size);
    if (!scene) {
        return false;
    }
    physics_settings settings = scene->physics;
    scene_document_free(scene);
    av_value *physics = av_obj(doc);
    av_obj_add_int(doc, physics, "workers", settings.workers);
    av_obj_add_int(doc, physics, "substeps", settings.substeps);
    av_obj_add_bool(doc, physics, "sleep", settings.sleep);
    av_obj_add_bool(doc, physics, "continuous", settings.continuous);
    av_obj_add_bool(doc, physics, "force_drive", settings.force_drive);
    av_obj_add_float(doc, physics, "contact_hertz", settings.contact_hertz);
    av_obj_add_float(doc, physics, "contact_damping", settings.contact_damping);
    av_obj_set(doc, object, "physics", physics);
    const char *selected_ui = av_get_str(av_obj_get(object, "ui"));
    if (selected_ui && *selected_ui) {
        return true;
    }
    bool exists;
    int handle, revision;
    workspace_document_status
        status = workspace_document_path(documents, "project.kdl", &exists, &handle, &revision);
    if (status == WORKSPACE_DOCUMENT_UNAVAILABLE || (!status && !exists)) {
        return true;
    }
    char *text = NULL;
    if (!status) {
        status = workspace_document_read_path(documents, "project.kdl", &text);
    }
    if (status) {
        return refuse(error, size, workspace_document_status_text(status));
    }
    av_doc *project = av_doc_read(text, error, size);
    free(text);
    if (!project) {
        return false;
    }
    av_value *ui = av_obj_get(av_doc_root(project), "ui");
    bool valid = !ui || av_is_str(ui);
    if (valid && ui) {
        av_obj_set(doc, object, "ui", av_clone(doc, ui));
    }
    av_doc_free(project);
    return valid || refuse(error, size, "Project UI must be a document name");
}
/* Effective object inspection expands includes with the host's current-source
 * resolver. Paths retain the authored object's coordinates; nothing is saved. */
bool workspace_object_inspect(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    workspace_asset_inspection *out,
    char *error,
    size_t size
)
{
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    if (!registry) {
        registry = &native;
    }
    *out = (workspace_asset_inspection){ .registry = registry };
    workspace_document_view view;
    if (!read_document(documents, document, revision, &view, error, size)) {
        return false;
    }
    const asset_schema *schema = schema_for(kind, error, size);
    if (!schema || (strcmp(kind, "scene") && strcmp(kind, "prototype"))) {
        return refuse(error, size, "Object properties require a scene or prototype");
    }
    av_doc *parsed = av_doc_read(view.text, error, size);
    if (!parsed) {
        return false;
    }
    av_value *authored = av_doc_root(parsed);
    if (*entity) {
        char leaf[1024];
        av_value *parent = resolve(authored, entity, leaf, sizeof leaf);
        authored = parent ? child_value(parent, leaf) : NULL;
    }
    if (!av_is_obj(authored)) {
        av_doc_free(parsed);
        return refuse(error, size, "Object path does not resolve");
    }
    prototype *effective = NULL;
    av_value *base_components = NULL;
    if (!strcmp(kind, "prototype") && !*entity) {
        effective = prototype_load(view.text, registry, error, size);
    } else if (*entity) {
        const char *name = av_get_str(av_obj_get(authored, "prototype"));
        const char *text = name && registry->include
                             ? registry->include(name, registry->include_user)
                             : NULL;
        if (!text) {
            av_doc_free(parsed);
            return refuse(error, size, "The object's prototype source is unavailable");
        }
        effective = prototype_load(text, registry, error, size);
        if (effective) {
            base_components = prototype_components_value(effective);
        }
        if (effective && av_obj_get(authored, "components") &&
            prototype_apply_entity_components(effective, av_obj_get(authored, "components"), error, size)) {
            prototype_free(effective);
            effective = NULL;
        }
    }
    if ((!strcmp(kind, "prototype") || *entity) && !effective) {
        av_value_free(base_components);
        av_doc_free(parsed);
        return false;
    }
    av_value *object = av_clone(parsed, authored);
    if (!strcmp(kind, "scene") && !*entity &&
        !scene_settings(parsed, object, documents, error, size)) {
        av_doc_free(parsed);
        return false;
    }
    if (effective) {
        av_value *components = prototype_components_value(effective);
        av_obj_set(parsed, object, "components", av_clone(parsed, components));
        av_value_free(components);
        if (!av_obj_get(authored, "behaviors")) {
            av_value *behaviors = av_arr(parsed);
            for (int i = 0; i < acount(effective->graphs); i++) {
                av_arr_append(behaviors, av_str(parsed, effective->graphs[i]));
            }
            av_obj_set(parsed, object, "behaviors", behaviors);
        }
    }
    /* Child instances are inspected separately when selected. */
    av_obj_remove_key(object, "children");
    const asset_schema *object_schema = schema;
    if (*entity) {
        const asset_schema_field
            *children = asset_schema_field_find(asset_schema_find("scene"), "entities");
        object_schema = children ? children->element_object : NULL;
    }
    bool ok = inspect_value(out, object, entity, kind, -1, 0, NULL, object_schema, NULL, NULL, error, size);
    if (ok) {
        for (int i = 0; i < acount(out->fields); i++) {
            workspace_asset_field *field = &out->fields[i];
            char leaf[1024];
            av_value *parent = resolve(av_doc_root(parsed), field->path, leaf, sizeof leaf);
            field->present = !*field->path || (parent && child_value(parent, leaf));
            field->relative = store(&out->storage, field->path + strlen(entity));
        }
    }
    ok = ok && present_fields(out, documents, error, size);
    for (int i = 0; ok && base_components && i < acount(out->fields); i++) {
        workspace_asset_field *row = &out->fields[i];
        if (!*row->component || row->computed) {
            continue;
        }
        int group = i;
        while (out->fields[group].parent >= 0 &&
               strcmp(out->fields[out->fields[group].parent].name, "components")) {
            group = out->fields[group].parent;
        }
        row->added = av_obj_get(base_components, out->fields[group].name) == NULL;
        if (row->added || i == group) {
            continue;
        }
        const char *relative = row->path + strlen(out->fields[group].path);
        av_value *base = av_obj_get(base_components, out->fields[group].name);
        char leaf[1024];
        av_value *parent = resolve(base, relative, leaf, sizeof leaf);
        av_value *before = parent ? child_value(parent, leaf) : NULL;
        parent = resolve(object, row->relative, leaf, sizeof leaf);
        av_value *after = parent ? child_value(parent, leaf) : NULL;
        row->overridden = before && after && !av_equals(before, after);
    }
    av_value_free(base_components);
    prototype_free(effective);
    av_doc_free(parsed);
    return ok;
}

/* Stage all target changes in an independent memory document, then replace the
 * original once. Refusal leaves its text, revision, transaction and redo intact. */
bool workspace_objects_edit(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    const char *relative,
    const char *operation,
    const char *value,
    char *error,
    size_t size
)
{
    if (count < 1 || count > 256 || !relative || *relative != '/') {
        return refuse(error, size, "Object editing requires1to256targets and a relative field path");
    }
    workspace_document_view original;
    if (!read_document(documents, document, revision, &original, error, size)) {
        return false;
    }
    workspace_document_status status;
    workspace_documents *stage = workspace_documents_new(NULL, &status);
    int handle = 0;
    bool ok = stage && workspace_document_memory(stage, original.text, &handle) ==
                           WORKSPACE_DOCUMENT_OK;
    for (int i = 0; ok && i < count; i++) {
        for (int j = 0; j < i; j++) {
            if (!strcmp(entities[i], entities[j])) {
                ok = refuse(error, size, "Object edit targets must be unique");
                break;
            }
        }
        if (!ok) {
            break;
        }
        workspace_document_view current;
        workspace_document_read(stage, handle, &current);
        char path[1024];
        if (snprintf(path, sizeof path, "%s%s", entities[i], relative) >=
            (int)sizeof path) {
            ok = refuse(error, size, "Object field path is too long");
            break;
        }
        workspace_asset_inspection effective = { 0 };
        ok = workspace_object_inspect(
            registry,
            stage,
            handle,
            current.revision,
            kind,
            entities[i],
            &effective,
            error,
            size
        );
        bool skip = false;
        if (ok && !strcmp(operation, "remove")) {
            for (int f = 0; f < acount(effective.fields); f++) {
                if (!strcmp(effective.fields[f].path, path) &&
                    !effective.fields[f].present) {
                    skip = true;
                    break;
                }
            }
        }
        if (ok && strcmp(operation, "remove")) {
            /* Create sparse inherited object containers; array containers retain
             * their effective shape so an indexed override cannot shift peers. */
            for (int f = 1; ok && f < acount(effective.fields); f++) {
                workspace_asset_field *field = &effective.fields[f];
                size_t n = strlen(field->path);
                if (field->present || strncmp(path, field->path, n) || path[n] != '/') {
                    continue;
                }
                if (strcmp(field->type, "object") && strcmp(field->type, "array")) {
                    continue;
                }
                if (!strcmp(field->type, "array")) {
                    ok = refuse(error, size, "Inherited array edits require an explicit local array override");
                    break;
                }
                workspace_document_read(stage, handle, &current);
                ok = workspace_asset_edit_registry(
                    registry,
                    stage,
                    handle,
                    current.revision,
                    kind,
                    "set",
                    field->path,
                    "",
                    error,
                    size
                );
            }
        }
        workspace_asset_inspection_free(&effective);
        if (ok && !skip) {
            workspace_document_read(stage, handle, &current);
            ok = workspace_asset_edit_registry(
                registry,
                stage,
                handle,
                current.revision,
                kind,
                operation,
                path,
                value,
                error,
                size
            );
        }
    }
    if (ok) {
        workspace_document_view edited;
        workspace_document_read(stage, handle, &edited);
        status = workspace_document_edit(
            documents,
            document,
            revision,
            0,
            (int)original.bytes,
            edited.text
        );
        if (status != WORKSPACE_DOCUMENT_OK) {
            ok = refuse(error, size, workspace_document_status_text(status));
        }
    }
    workspace_documents_free(stage);
    return ok;
}

bool workspace_objects_inspect(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    workspace_asset_inspection *out,
    char *error,
    size_t size
)
{
    *out = (workspace_asset_inspection){ 0 };
    if (count < 1 || count > 256) {
        return refuse(error, size, "Object inspection requires1to256targets");
    }
    if (!workspace_object_inspect(registry, documents, document, revision, kind, entities[0], out, error, size)) {
        return false;
    }
    for (int i = 1; i < count; i++) {
        workspace_asset_inspection next = { 0 };
        if (!workspace_object_inspect(registry, documents, document, revision, kind, entities[i], &next, error, size)) {
            workspace_asset_inspection_free(&next);
            return false;
        }
        for (int f = 0; f < acount(out->fields); f++) {
            workspace_asset_field *field = &out->fields[f], *other = NULL;
            for (int j = 0; j < acount(next.fields); j++) {
                if (!strcmp(field->relative, next.fields[j].relative) &&
                    !strcmp(field->type, next.fields[j].type)) {
                    other = &next.fields[j];
                    break;
                }
            }
            if (!other) {
                field->common = false;
                continue;
            }
            field->present = field->present || other->present;
            field->mixed = field->mixed || strcmp(field->text, other->text) ||
                           field->number != other->number ||
                           field->boolean != other->boolean;
        }
        workspace_asset_inspection_free(&next);
    }
    return true;
}

bool workspace_objects_component(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
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
    const ecs_component_desc *descriptor = prototype_registry_find(registry, component);
    if (!descriptor) {
        return refuse(error, size, "Component is unknown");
    }
    if (count < 1 || count > 256) {
        return refuse(error, size, "Component editing requires1to256targets");
    }
    workspace_document_view original;
    if (!read_document(documents, document, revision, &original, error, size)) {
        return false;
    }
    workspace_document_status status;
    workspace_documents *stage = workspace_documents_new(NULL, &status);
    int handle;
    bool ok = stage && workspace_document_memory(stage, original.text, &handle) ==
                           WORKSPACE_DOCUMENT_OK;
    for (int i = 0; ok && i < count; i++) {
        workspace_document_view current;
        workspace_document_read(stage, handle, &current);
        workspace_asset_inspection effective = { 0 };
        ok = workspace_object_inspect(
            registry,
            stage,
            handle,
            current.revision,
            kind,
            entities[i],
            &effective,
            error,
            size
        );
        bool exists = false, local = false;
        for (int f = 0; ok && f < acount(effective.fields); f++) {
            workspace_asset_field *field = &effective.fields[f];
            if (field->parent >= 0 &&
                !strcmp(effective.fields[field->parent].name, "components") &&
                (!strcmp(field->name, descriptor->name) ||
                 !strcmp(field->name, descriptor->key))) {
                exists = true;
                local = field->present;
                break;
            }
        }
        workspace_asset_inspection_free(&effective);
        if (ok && ((add && !exists) || (!add && local))) {
            ok = workspace_asset_component_registry(
                registry,
                stage,
                handle,
                current.revision,
                kind,
                entities[i],
                component,
                add,
                error,
                size
            );
        }
    }
    if (ok) {
        workspace_document_view edited;
        workspace_document_read(stage, handle, &edited);
        status = !strcmp(original.text, edited.text) ? WORKSPACE_DOCUMENT_OK
                                                     : workspace_document_edit(
                                                           documents,
                                                           document,
                                                           revision,
                                                           0,
                                                           (int)original.bytes,
                                                           edited.text
                                                       );
        if (status != WORKSPACE_DOCUMENT_OK) {
            ok = refuse(error, size, workspace_document_status_text(status));
        }
    }
    workspace_documents_free(stage);
    return ok;
}

bool workspace_objects_candidate(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *const *entities,
    int count,
    const char *relative,
    const char *operation,
    const char *value,
    const char *source,
    char **text,
    char *error,
    size_t size
)
{
    *text = NULL;
    workspace_document_view original;
    if (!read_document(documents, document, revision, &original, error, size)) {
        return false;
    }
    workspace_document_status status;
    workspace_documents *candidate = workspace_documents_new(NULL, &status);
    int handle = 0;
    bool ok = candidate && workspace_document_memory(
                               candidate,
                               source && *source ? source : original.text,
                               &handle
                           ) == WORKSPACE_DOCUMENT_OK;
    if (ok) {
        if (!strcmp(operation, "component-add") ||
            !strcmp(operation, "component-remove")) {
            ok = workspace_objects_component(
                registry,
                candidate,
                handle,
                1,
                kind,
                entities,
                count,
                value,
                !strcmp(operation, "component-add"),
                error,
                size
            );
        } else {
            ok = workspace_objects_edit(
                registry,
                candidate,
                handle,
                1,
                kind,
                entities,
                count,
                relative,
                operation,
                value,
                error,
                size
            );
        }
    }
    if (ok) {
        workspace_document_view view;
        workspace_document_read(candidate, handle, &view);
        *text = xstrdup(view.text);
    } else if (size && !*error) {
        refuse(error, size, "Could not construct the private object candidate");
    }
    workspace_documents_free(candidate);
    return ok;
}

bool workspace_candidate_difference(
    const char *original,
    const char *candidate,
    int *start,
    int *end,
    char **replacement
)
{
    *replacement = NULL;
    if (!original || !candidate) {
        return false;
    }
    size_t before = strlen(original), after = strlen(candidate), prefix = 0,
           suffix = 0;
    if (before > WORKSPACE_DOCUMENT_BYTES_MAX || after > WORKSPACE_DOCUMENT_BYTES_MAX) {
        return false;
    }
    while (prefix < before && prefix < after && original[prefix] == candidate[prefix]) {
        prefix++;
    }
    while (prefix && ((unsigned char)original[prefix] & 0xc0) == 0x80) {
        prefix--;
    }
    while (suffix < before - prefix && suffix < after - prefix &&
           original[before - suffix - 1] == candidate[after - suffix - 1]) {
        suffix++;
    }
    size_t old_end = before - suffix, new_end = after - suffix;
    while (suffix && ((((unsigned char)original[old_end] & 0xc0) == 0x80) ||
                      (((unsigned char)candidate[new_end] & 0xc0) == 0x80))) {
        old_end++;
        new_end++;
        suffix--;
    }
    size_t bytes = new_end - prefix;
    *replacement = xmalloc(bytes + 1);
    memcpy(*replacement, candidate + prefix, bytes);
    (*replacement)[bytes] = 0;
    *start = (int)prefix;
    *end = (int)old_end;
    return true;
}

/* Pure source candidates for promoting instance overrides or preserving a tuned
 * instance as a new prototype. Publication and cross-document history belong to
 * workspace_documents, so inspecting a candidate changes no handles or source. */
bool workspace_prototype_candidate(
    const prototype_registry *registry,
    const workspace_documents *documents,
    int document,
    int revision,
    const char *kind,
    const char *entity,
    int prototype_document,
    int prototype_revision,
    const char *const *paths,
    int count,
    const char *operation,
    const char *name,
    char **object_text,
    char **prototype_text,
    char **new_path,
    char *error,
    size_t size
)
{
    *object_text = NULL;
    *prototype_text = NULL;
    *new_path = NULL;
    bool applying = operation && !strcmp(operation, "apply");
    bool saving = operation && !strcmp(operation, "save-as");
    if ((!applying && !saving) || !entity || !*entity ||
        (strcmp(kind, "scene") && strcmp(kind, "prototype"))) {
        return refuse(error, size, "Prototype actions require a scene or prototype child instance");
    }
    workspace_document_view original, target = { 0 };
    if (!read_document(documents, document, revision, &original, error, size)) {
        return false;
    }
    if (applying &&
        (!paths || count < 1 || count > 256 || document == prototype_document ||
         !read_document(documents, prototype_document, prototype_revision, &target, error, size))) {
        return refuse(error, size, "Apply requires 1 to 256 fields and a distinct exact-revision prototype document");
    }
    av_doc *source = av_doc_read(original.text, error, size), *destination = NULL;
    char leaf[1024];
    av_value *parent = source ? resolve(av_doc_root(source), entity, leaf, sizeof leaf)
                              : NULL;
    av_value *object = parent ? child_value(parent, leaf) : NULL;
    const char *base = av_get_str(av_obj_get(object, "prototype"));
    bool ok = object && av_is_obj(object) && base && *base;
    if (!ok) {
        refuse(error, size, "The selected instance has no prototype identity");
    }
    if (ok && applying) {
        destination = av_doc_read(target.text, error, size);
        const char *declared = destination
                                 ? av_get_str(av_obj_get(av_doc_root(destination), "name"))
                                 : NULL;
        if (!declared || strcmp(base, declared)) {
            ok = refuse(error, size, "The target document is not the instance's declared prototype");
        }
        for (int i = 0; ok && i < count; i++) {
            if (!paths[i] || strncmp(paths[i], "/components/", 12) ||
                strlen(paths[i]) >= 1024) {
                ok = refuse(error, size, "Only local component override paths can be applied");
                break;
            }
            for (int j = 0; j < i; j++) {
                size_t a = strlen(paths[i]), b = strlen(paths[j]);
                if (!strcmp(paths[i], paths[j]) ||
                    (!strncmp(paths[i], paths[j], b) && paths[i][b] == '/') ||
                    (!strncmp(paths[j], paths[i], a) && paths[j][a] == '/')) {
                    ok = refuse(error, size, "Applied override paths must be distinct and non-overlapping");
                    break;
                }
            }
            if (!ok) {
                break;
            }
            char source_leaf[1024];
            av_value *source_parent = resolve(object, paths[i], source_leaf, sizeof source_leaf);
            av_value *value = source_parent ? child_value(source_parent, source_leaf)
                                            : NULL;
            if (!av_is_obj(source_parent) || !value) {
                ok = refuse(error, size, "The selected field has no local override");
                break;
            }
            /* Sparse object parents inherit every untouched field. Indexed array
             * promotion requires the whole authored array, never sparse indices. */
            char prefix[1024];
            snprintf(prefix, sizeof prefix, "%s", paths[i]);
            for (char *slash = strchr(prefix + 1, '/'); ok && slash;
                 slash = strchr(slash + 1, '/')) {
                *slash = 0;
                char key[1024];
                av_value *container = resolve(av_doc_root(destination), prefix, key, sizeof key);
                if (!av_is_obj(container)) {
                    ok = refuse(error, size, "Apply requires object parents; promote a complete array instead");
                } else if (!av_obj_get(container, key)) {
                    av_obj_set(destination, container, key, av_obj(destination));
                } else if (!av_is_obj(av_obj_get(container, key))) {
                    ok = refuse(error, size, "Apply cannot replace a scalar or array parent");
                }
                *slash = '/';
            }
            av_value *destination_parent = ok ? resolve(av_doc_root(destination), paths[i], leaf, sizeof leaf)
                                              : NULL;
            if (ok && !av_is_obj(destination_parent)) {
                ok = refuse(error, size, "Override destination does not resolve");
            }
            if (ok) {
                av_obj_set(destination, destination_parent, leaf, av_clone(destination, value));
                av_obj_remove_key(source_parent, source_leaf);
            }
        }
    }
    if (ok && saving) {
        workspace_asset_template_result template = { 0 };
        ok = workspace_asset_template("prototype", name, &template, error, size);
        if (ok && registry && registry->include &&
            registry->include(name, registry->include_user)) {
            ok = refuse(error, size, "A prototype with this declared name already exists");
        }
        if (ok) {
            destination = av_doc_new();
            av_value *root = av_obj(destination);
            av_doc_set_root(destination, root);
            av_obj_add_strcpy(destination, root, "name", name);
            av_value *includes = av_obj_add_arr(destination, root, "includes");
            av_arr_add_val(includes, av_str(destination, base));
            av_value *components = av_obj_get(object, "components");
            if (components) {
                av_obj_set(destination, root, "components", av_clone(destination, components));
            }
            /* Keep child placement and bindings in the original hierarchy. The
             * new prototype represents this instance's component tuning. */
            av_obj_remove_key(object, "components");
            av_obj_set(source, object, "prototype", av_str(source, name));
            *new_path = xstrdup(template.path);
        }
        workspace_asset_template_free(&template);
    }
    if (ok) {
        const asset_schema *schema = schema_for(kind, error, size);
        ok = schema &&
             validate(registry, schema, av_doc_root(source), original.path, error, size) &&
             validate(
                 registry,
                 asset_schema_find("prototype"),
                 av_doc_root(destination),
                 applying ? target.path : "",
                 error,
                 size
             );
    }
    if (ok) {
        *object_text = av_doc_write(source);
        *prototype_text = av_doc_write(destination);
        ok = *object_text && *prototype_text &&
             strlen(*object_text) <= WORKSPACE_DOCUMENT_BYTES_MAX &&
             strlen(*prototype_text) <= WORKSPACE_DOCUMENT_BYTES_MAX;
        if (!ok) {
            refuse(error, size, "Prototype candidates exceed the document byte limit");
        }
    }
    av_doc_free(source);
    av_doc_free(destination);
    if (!ok) {
        free(*object_text);
        free(*prototype_text);
        free(*new_path);
        *object_text = NULL;
        *prototype_text = NULL;
        *new_path = NULL;
    }
    return ok;
}
