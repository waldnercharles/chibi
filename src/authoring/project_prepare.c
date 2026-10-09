#include "assets/project.h"
#include "authoring/component_declarations.h"
#include "authoring/image_prepare.h"
#include "authoring/language_catalog.h"
#include "authoring/material_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/native_graph_prepare.h"
#include "authoring/project_prepare_internal.h"
#include "authoring/prototype_prepare.h"
#include "authoring/scene_prepare.h"
#include "authoring/shader_prepare.h"
#include "authoring/source_collection.h"
#include "authoring/ui_prepare.h"
#include "authoring/visual_prepare.h"
#include "render/arena_parts.h"
#include "render/shader_values.h"
#include "runtime/behaviors/module_set.h"
#include "runtime/event_catalog.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/material_field_prepared.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static void clear_visuals()
{
    authoring_shader_catalog_clear();
    shader_values_clear();
    render_arena_parts_clear();
    render_mesh_assets_clear();
    render_image_catalog_clear();
}
static bool sources_valid(const authoring_sources *sources, char *error, size_t size)
{
    if (authoring_sources_valid(sources)) {
        return true;
    }
    if (authoring_sources_catalog_diagnostic_count(sources)) {
        const source_catalog_diagnostic
            *diagnostic = authoring_sources_catalog_diagnostic_at(sources, 0);
        if (size) {
            snprintf(
                error,
                size,
                "%s:%d:%d (byte %d): %s",
                diagnostic->path,
                diagnostic->range.line,
                diagnostic->range.column,
                diagnostic->range.start,
                diagnostic->message
            );
        }
        return false;
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (source->diagnostic_count) {
            const script_diagnostic *diagnostic = &source->diagnostics[0];
            if (size) {
                snprintf(
                    error,
                    size,
                    "%s:%d:%d (byte %d): %s",
                    source->path,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->range.start,
                    diagnostic->message
                );
            }
            return false;
        }
    }
    return fail(error, size, "Project executable sources are invalid");
}
static pin_value initial(const script_catalog *catalog, const script_catalog_parameter *parameter)
{
    pin_value value = parameter->has_default
                        ? parameter->default_value
                        : (pin_value){ .type = parameter->type.storage,
                                       .container = parameter->type.container };
    if (parameter->type.nominal) {
        const script_catalog_type
            *type = script_catalog_type_by_id(catalog, parameter->type.nominal);
        if (type) {
            value.type_name = type->name;
        } else if (parameter->type.nominal == catalog->entity_type && value.type == PIN_HANDLE) {
            value.type_name = "ecs.entity";
        }
    }
    return value;
}
static game_event_catalog *events_prepare(
    const script_catalog *catalog,
    const execution_prepared_linker *types,
    char *error,
    size_t size
)
{
    game_event_declaration *events = NULL;
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *cap = &catalog->capabilities[i];
        if (cap->kind != SCRIPT_CAPABILITY_EVENT) {
            continue;
        }
        execution_module_event_field_info *fields = NULL;
        for (int j = 0; j < cap->parameter_count; j++) {
            const script_catalog_parameter *parameter = &cap->parameters[j];
            apush(
                fields,
                ((execution_module_event_field_info){
                    .name = parameter->name,
                    .initial = initial(catalog, parameter),
                    .nominal = parameter->type.nominal,
                    .has_default = parameter->has_default,
                    .default_owner = parameter->default_owner })
            );
        }
        apush(
            events,
            ((game_event_declaration){
                .event = { .id = cap->id,
                           .abi = script_catalog_capability_abi(catalog, cap),
                           .name = cap->name,
                           .field_count = cap->parameter_count },
                .fields = fields })
        );
    }
    game_event_catalog *result = game_event_catalog_new(events, acount(events), types, error, size);
    for (int i = 0; i < acount(events); i++) {
        execution_module_event_field_info *fields = (void *)events[i].fields;
        afree(fields);
    }
    afree(events);
    return result;
}
static bool attachments_valid(
    const game_scene *scene,
    const authoring_sources *sources,
    char *error,
    size_t size
)
{
    for (int i = 0; i < game_scene_object_count(scene); i++) {
        for (int j = 0; j < game_scene_object_behavior_count(scene, i); j++) {
            const char *name = game_scene_object_behavior(scene, i, j);
            bool found = false;
            for (int k = 0; k < authoring_sources_count(sources); k++) {
                const authoring_source *source = authoring_sources_at(sources, k);
                if (!strcmp(source->analysis->name, name) &&
                    source->analysis->kind == SCRIPT_PROGRAM_BEHAVIOR) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (size) {
                    snprintf(error, size, "Scene attachment '%s' has no prepared behavior source", name);
                }
                return false;
            }
        }
    }
    return true;
}
void authoring_project_artifact_free(authoring_project_artifact *artifact)
{
    assert(artifact);
    free(artifact->data);
    free(artifact->manifest);
    *artifact = (authoring_project_artifact){ 0 };
}
static uint64_t content_hash(const void *data, size_t size)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
static char *manifest(const game_player_asset *assets, const authoring_project_artifact *artifact)
{
    static const char *const kinds[] = { "shader", "model",         "mesh",
                                         "image",  "shader-values", "parts",
                                         "ui",     "font" };
    static_assert(sizeof kinds / sizeof *kinds == GAME_PLAYER_ASSET_COUNT);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    char hash[17];
    snprintf(
        hash,
        sizeof hash,
        "%016" PRIx64,
        content_hash(artifact->data, artifact->size)
    );
    yyjson_mut_obj_add_strcpy(doc, root, "artifact_hash", hash);
    yyjson_mut_val *embedded = yyjson_mut_obj_add_arr(doc, root, "embedded");
    yyjson_mut_val *external = yyjson_mut_obj_add_arr(doc, root, "external");
    for (int i = 0; i < acount(assets); i++) {
        yyjson_mut_val *record = yyjson_mut_arr_add_obj(doc, assets[i].external ? external : embedded);
        yyjson_mut_obj_add_str(doc, record, "kind", kinds[assets[i].kind]);
        yyjson_mut_obj_add_str(doc, record, "key", assets[i].name);
        yyjson_mut_obj_add_uint(doc, record, "bytes", assets[i].length);
        if (assets[i].path) {
            yyjson_mut_obj_add_str(doc, record, "path", assets[i].path);
        }
        snprintf(
            hash,
            sizeof hash,
            "%016" PRIx64,
            assets[i].external
                ? assets[i].content_hash
                : game_player_asset_fingerprint(assets[i].data, assets[i].length)
        );
        yyjson_mut_obj_add_strcpy(doc, record, "content_hash", hash);
    }
    char *text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(doc);
    return text;
}
bool authoring_project_prepare(
    const authoring_project_options *options,
    authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    assert(options && artifact && (!size || error));
    assert(!artifact->data && !artifact->manifest);
    *artifact = (authoring_project_artifact){ 0 };
    if (size) {
        error[0] = 0;
    }
    if (!options->project || !options->native ||
        options->components.component_count < 0 ||
        (options->components.component_count && !options->components.components) ||
        (options->modules & ~GAME_WORLD_MODULES_ALL)) {
        return fail(error, size, "Invalid project preparation options");
    }
    bool valid = false;
    clear_visuals();
    chibi_project project = { 0 };
    ecs_world *schemas = ecs_world_new();
    material_fields *fields = material_fields_new();
    game_package_collection *collection = NULL;
    authoring_language_catalog *native = NULL;
    authoring_sources *sources = NULL;
    project_prototype *prototypes = NULL;
    game_prototype **objects = NULL;
    game_scene_document *document = NULL;
    game_scene *scene = NULL;
    game_event_catalog *events = NULL;
    game_package_module *modules = NULL;
    const execution_module **definitions = NULL;
    game_player_prototype *prepared = NULL;
    game_player_asset *assets = NULL;
    const game_scene **visual_scenes = NULL;
    declared_components *declared = NULL;
    const ecs_component_desc **components = NULL;
    size_t module_bytes = 0;
    char *scene_path = NULL, *text = NULL;
    if (chibi_project_open(&project, options->project, error, size)) {
        goto done;
    }
    if (!render_image_mount_content(project.content, error, size) ||
        !authoring_mesh_mount_content(project.content, error, size)) {
        goto done;
    }
    if (!authoring_component_declarations_load(
            project.content,
            native_capabilities_catalog(options->native),
            &declared,
            error,
            size
        )) {
        goto done;
    }
    for (int i = 0; i < options->components.component_count; i++) {
        apush(components, options->components.components[i]);
    }
    ecs_component_catalog extra = declared ? declared_components_ecs(declared)
                                           : (ecs_component_catalog){ 0 };
    for (int i = 0; i < extra.component_count; i++) {
        apush(components, extra.components[i]);
    }
    for (int i = 0; i < acount(components); i++) {
        if (ecs_world_register_component(schemas, components[i], NULL)) {
            fail(error, size, "Project component schema installation failed");
            goto done;
        }
    }
    if (!authoring_native_graphs_prepare(project.content, fields, error, size)) {
        goto done;
    }
    native = authoring_language_catalog_new(
        &(authoring_language_catalog_input){
            .native = options->native,
            .project = declared ? declared_components_catalog(declared) : NULL },
        error,
        size
    );
    if (!native) {
        goto done;
    }
    script_lower_bindings bindings = authoring_language_bindings(native);
    sources = authoring_sources_prepare(
        project.content,
        authoring_language_catalog_view(native),
        &bindings,
        BEHAVIOR_BYTECODE
    );
    if (!sources || !sources_valid(sources, error, size)) {
        goto done;
    }
    native_prepared_context event_context = { .native = options->native };
    execution_prepared_linker native_types = native_capabilities_prepared_linker(&event_context);
    declared_components_linking event_linking = { declared, &native_types };
    execution_prepared_linker event_types = declared_components_linker(&event_linking);
    events = events_prepare(authoring_sources_catalog(sources), &event_types, error, size);
    if (!events) {
        goto done;
    }
    game_prototype_registry registry = { .components = components,
                                         .component_count = acount(components) };
    if (!project_prepare_prototypes(project.content, &registry, &prototypes, error, size)) {
        goto done;
    }
    for (int i = 0; i < acount(prototypes); i++) {
        game_prototype *prototype = prototypes[i].prototype;
        if (!authoring_prototype_parameters_fields(fields, prototype, error, size)) {
            goto done;
        }
        game_scene *one = authoring_prototype_prepare(prototype, error, size);
        if (!one) {
            goto done;
        }
        apush(prepared, ((game_player_prototype){ prototype->identity, one }));
        if (!attachments_valid(one, sources, error, size) ||
            game_scene_register_components(one, schemas, error, size)) {
            goto done;
        }
    }
    scene_path = chibi_project_scene_path(&project, options->scene);
    text = scene_path ? project_prepare_read(scene_path, NULL, error, size) : NULL;
    document = text ? game_scene_document_load(text, error, size) : NULL;
    if (!document) {
        goto done;
    }
    if (!document->ui && project.ui) {
        game_scene_document_set_ui(document, project.ui);
    }
    for (int i = 0; i < acount(document->entities); i++) {
        game_prototype *object = project_prepare_object(&document->entities[i], prototypes, error, size);
        if (!object) {
            goto done;
        }
        apush(objects, object);
        if (!authoring_prototype_parameters_fields(fields, object, error, size)) {
            goto done;
        }
    }
    scene = authoring_scene_prepare(
        document,
        (const game_prototype *const *)objects,
        acount(objects),
        error,
        size
    );
    if (!scene || !attachments_valid(scene, sources, error, size) ||
        game_scene_register_components(scene, schemas, error, size)) {
        goto done;
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        unsigned char *bytes = NULL;
        size_t length = 0;
        if (!execution_prepared_write(source->module, &bytes, &length, error, size)) {
            goto done;
        }
        if (length > GAME_PACKAGE_MAX_BYTES - module_bytes) {
            free(bytes);
            fail(error, size, "Prepared module closure exceeds package byte limit");
            goto done;
        }
        module_bytes += length;
        apush(
            modules,
            ((game_package_module){ xstrdup(source->analysis->name),
                                    execution_module_id(source->module),
                                    bytes,
                                    length })
        );
        apush(definitions, source->module);
    }
    if (!game_module_set_validate(definitions, acount(definitions), 0, error, size) ||
        !game_event_catalog_validate(events, definitions, acount(definitions), error, size)) {
        goto done;
    }
    /* The encoded package is linked independently of source/compiler storage. */
    afree(definitions);
    authoring_sources_free(sources);
    sources = NULL;
    authoring_language_catalog_free(native);
    native = NULL;
    native_prepared_context context = { .native = options->native, .events = events };
    execution_prepared_linker native_linker = native_capabilities_prepared_linker(&context);
    declared_components_linking linking = { declared, &native_linker };
    execution_prepared_linker linker = declared_components_linker(&linking);
    size_t remaining = GAME_PACKAGE_MAX_BYTES;
    collection = game_package_read_budget(
        modules,
        acount(modules),
        &linker,
        events,
        BEHAVIOR_BYTECODE,
        &remaining,
        error,
        size
    );
    if (!collection) {
        goto done;
    }
    for (int i = 0; i < game_package_collection_count(collection); i++) {
        if (!execution_module_components_valid(game_package_collection_module(collection, i), schemas)) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "%s: Prepared component layout mismatch",
                    game_package_collection_name(collection, i)
                );
            }
            goto done;
        }
    }
    for (int i = 0; i < acount(prepared); i++) {
        apush(visual_scenes, prepared[i].scene);
    }
    apush(visual_scenes, scene);
    if (!authoring_visual_prepare(visual_scenes, acount(visual_scenes), &assets, error, size)) {
        goto done;
    }
    if (!authoring_ui_assets_prepare(project.content, visual_scenes, acount(visual_scenes), &assets, error, size)) {
        goto done;
    }
    game_player_spec spec = {
        .world = { .modules = options->modules,
                   .fields = (options->modules & GAME_WORLD_MATERIALS) ? fields : NULL,
                   .events = events },
        .declared = declared,
        .modules = modules,
        .module_count = acount(modules),
        .prototypes = prepared,
        .prototype_count = acount(prepared),
        .assets = assets,
        .asset_count = acount(assets),
        .scene = scene
    };
    valid = game_player_prepared_write(&spec, &artifact->data, &artifact->size, error, size);
    if (valid) {
        artifact->manifest = manifest(assets, artifact);
        if (!artifact->manifest) {
            authoring_project_artifact_free(artifact);
            valid = fail(error, size, "Prepared resource manifest encoding failed");
        }
    }
done:
    authoring_visual_prepared_free(assets);
    afree(visual_scenes);
    if (collection) {
        game_package_collection_free(collection);
    }
    ecs_world_free(schemas);
    material_fields_free(fields);
    for (int i = 0; i < acount(modules); i++) {
        free((void *)modules[i].name);
        free((void *)modules[i].data);
    }
    afree(modules);
    afree(definitions);
    for (int i = 0; i < acount(prepared); i++) {
        game_scene_free(prepared[i].scene);
    }
    afree(prepared);
    game_scene_free(scene);
    for (int i = 0; i < acount(objects); i++) {
        game_prototype_free(objects[i]);
    }
    afree(objects);
    project_prepare_prototypes_free(prototypes);
    game_scene_document_free(document);
    free(text);
    free(scene_path);
    game_event_catalog_free(events);
    authoring_sources_free(sources);
    authoring_language_catalog_free(native);
    afree(components);
    declared_components_free(declared);
    chibi_project_free(&project);
    authoring_mesh_unmount_content();
    clear_visuals();
    return valid;
}
