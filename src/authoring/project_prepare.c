#include "assets/input_map.h"
#include "assets/project.h"
#include "authoring/component_declarations.h"
#include "authoring/image_prepare.h"
#include "authoring/language_catalog.h"
#include "authoring/material_prepare.h"
#include "authoring/mesh_prepare.h"
#include "authoring/project_prepare_internal.h"
#include "authoring/prototype_prepare.h"
#include "authoring/scene_prepare.h"
#include "authoring/sound_prepare.h"
#include "authoring/source_asset.h"
#include "authoring/source_collection.h"
#include "authoring/ui_prepare.h"
#include "authoring/visual_prepare.h"
#include "render/scene3d_parts.h"
#include "render/shader_values.h"
#include "runtime/behaviors/module_set.h"
#include "runtime/event_catalog.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/material_field_prepared.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
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
    shader_values_clear();
    render_scene3d_parts_clear();
    render_mesh_assets_clear();
    render_skies_clear();
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
static event_catalog *events_prepare(
    const script_catalog *catalog,
    const execution_prepared_linker *types,
    char *error,
    size_t size
)
{
    event_declaration *events = NULL;
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
            ((event_declaration){
                .event = { .id = cap->id,
                           .abi = script_catalog_capability_abi(catalog, cap),
                           .name = cap->name,
                           .field_count = cap->parameter_count },
                .fields = fields })
        );
    }
    event_catalog *result = event_catalog_new(events, acount(events), types, error, size);
    for (int i = 0; i < acount(events); i++) {
        execution_module_event_field_info *fields = (void *)events[i].fields;
        afree(fields);
    }
    afree(events);
    return result;
}
static bool attachments_valid(
    const scene *prepared_scene,
    const source_catalog *catalog,
    char *error,
    size_t size
)
{
    for (int i = 0; i < scene_object_count(prepared_scene); i++) {
        for (int j = 0; j < scene_object_behavior_count(prepared_scene, i); j++) {
            const char *name = scene_object_behavior(prepared_scene, i, j);
            bool found = false;
            for (int k = 0; k < source_catalog_source_count(catalog); k++) {
                const script_analysis *declared = source_catalog_source_at(catalog, k)
                                                      ->declarations;
                if (!strcmp(declared->name, name) &&
                    declared->kind == SCRIPT_PROGRAM_BEHAVIOR) {
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
typedef struct
{
    const chibi_project *project;
    const project_prototype *prototypes;
    material_fields *fields;
    const source_catalog *catalog;
    ecs_world *schemas;
} scene_inputs;
static scene *prepare_scene(const scene_inputs *in, const char *path, char *error, size_t size)
{
    char *text = project_prepare_read(path, NULL, error, size);
    scene_document *document = text ? scene_document_load(text, error, size) : NULL;
    free(text);
    if (!document) {
        return NULL;
    }
    if (!document->ui && in->project->ui) {
        scene_document_set_ui(document, in->project->ui);
    }
    prototype **objects = NULL;
    bool valid = true;
    project_expansion expanded = { 0 };
    for (int i = 0; valid && i < acount(document->entities); i++) {
        prototype *object = project_prepare_object(&document->entities[i], in->prototypes, error, size);
        valid = object && project_prepare_expand(&expanded, object, error, size) &&
                authoring_prototype_parameters_fields(in->fields, object, error, size);
        if (object) {
            apush(objects, object);
        }
    }
    scene *prepared_scene = valid ? authoring_scene_prepare(
                                        document,
                                        (const prototype *const *)objects,
                                        acount(objects),
                                        error,
                                        size
                                    )
                                  : NULL;
    for (int i = 0; i < acount(objects); i++) {
        prototype_free(objects[i]);
    }
    afree(objects);
    scene_document_free(document);
    if (prepared_scene &&
        (!attachments_valid(prepared_scene, in->catalog, error, size) ||
         scene_register_components(prepared_scene, in->schemas, error, size))) {
        scene_free(prepared_scene);
        return NULL;
    }
    return prepared_scene;
}
static char *name_from_path(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    char *copy = xstrdup(name);
    char *dot = strrchr(copy, '.');
    if (dot) {
        *dot = 0;
    }
    return copy;
}
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
/* Every scene in content/scenes, sorted, so Load Scene can start any of them. */
static char **scene_files(const char *content)
{
    char directory[4096];
    snprintf(directory, sizeof directory, "%s/scenes", content);
    DIR *dir = opendir(directory);
    char **names = NULL;
    for (struct dirent *entry; dir && (entry = readdir(dir));) {
        size_t n = strlen(entry->d_name);
        if (entry->d_name[0] != '.' && n > 4 && !strcmp(entry->d_name + n - 4, ".kdl")) {
            apush(names, xstrdup(entry->d_name));
        }
    }
    if (dir) {
        closedir(dir);
    }
    if (acount(names) > 1) {
        qsort(names, (size_t)acount(names), sizeof *names, compare_names);
    }
    return names;
}
static bool prepare_scenes(
    const scene_inputs *in,
    const char *start,
    player_named_scene **scenes,
    char *error,
    size_t size
)
{
    char **files = scene_files(in->project->content);
    bool valid = true;
    for (int i = 0; valid && i < acount(files); i++) {
        char *name = name_from_path(files[i]);
        if (!strcmp(name, start)) {
            free(name);
            continue;
        }
        char path[4096], reason[512];
        snprintf(path, sizeof path, "%s/scenes/%s", in->project->content, files[i]);
        scene *prepared_scene = prepare_scene(in, path, reason, sizeof reason);
        if (!prepared_scene) {
            snprintf(error, size, "scenes/%s: %s", files[i], reason);
            free(name);
            valid = false;
            continue;
        }
        apush(*scenes, ((player_named_scene){ name, prepared_scene }));
    }
    for (int i = 0; i < acount(files); i++) {
        free(files[i]);
    }
    afree(files);
    return valid;
}
void authoring_project_artifact_free(authoring_project_artifact *artifact)
{
    assert(artifact);
    free(artifact->data);
    free(artifact->manifest);
    free(artifact->cost_report);
    *artifact = (authoring_project_artifact){ 0 };
}
/* The window shows the project name unless a title replaces it, so a name
 * longer than the artifact holds needs a title. */
static bool application_title_valid(const chibi_project *project, char *error, size_t size)
{
    bool valid = project->application.title ||
                 strlen(project->name) <= PLAYER_APPLICATION_TEXT_MAX;
    if (!valid && size) {
        snprintf(error, size, "Project name is longer than %d bytes; set a shorter application title", PLAYER_APPLICATION_TEXT_MAX);
    }
    return valid;
}
/* The window icon ships beside the artifact, so it must decode as a PNG. Its
 * size and hash fill `icon` for the manifest's external records. */
static bool application_icon_read(const chibi_project *project, player_asset *icon, char *error, size_t size)
{
    const char *path = project->application.icon;
    if (!path) {
        return true;
    }
    size_t length = strlen(project->root) + strlen(path) + 2;
    char *file = xmalloc(length);
    snprintf(file, length, "%s/%s", project->root, path);
    char reason[128];
    int bytes = 0;
    void *data = image_prepare_png_read(file, &bytes, reason, sizeof reason);
    free(file);
    if (!data) {
        if (size) {
            snprintf(error, size, "Application icon is not a readable PNG: %s: %s", path, reason);
        }
        return false;
    }
    *icon = (player_asset){ .name = path,
                            .path = path,
                            .external = true,
                            .length = (size_t)bytes,
                            .content_hash = player_asset_fingerprint(data, (size_t)bytes) };
    free(data);
    return true;
}
bool authoring_project_application_valid(const chibi_project *project, char *error, size_t size)
{
    player_asset icon;
    return application_title_valid(project, error, size) &&
           application_icon_read(project, &icon, error, size);
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
static void add_record(yyjson_mut_doc *doc, yyjson_mut_val *records, const char *kind, const player_asset *asset)
{
    yyjson_mut_val *record = yyjson_mut_arr_add_obj(doc, records);
    yyjson_mut_obj_add_str(doc, record, "kind", kind);
    yyjson_mut_obj_add_str(doc, record, "key", asset->name);
    yyjson_mut_obj_add_uint(doc, record, "bytes", asset->length);
    if (asset->path) {
        yyjson_mut_obj_add_str(doc, record, "path", asset->path);
    }
    char hash[17];
    snprintf(
        hash,
        sizeof hash,
        "%016" PRIx64,
        asset->external ? asset->content_hash
                        : player_asset_fingerprint(asset->data, asset->length)
    );
    yyjson_mut_obj_add_strcpy(doc, record, "content_hash", hash);
}
/* The artifact's resource closure: embedded assets, and the external files,
 * the window icon among them, that must ship beside it. */
static char *manifest(const player_asset *assets, const player_asset *icon, const authoring_project_artifact *artifact)
{
    static const char *const kinds[] = { "shader", "model",         "mesh",
                                         "image",  "shader-values", "parts",
                                         "ui",     "font",          "sky",
                                         "sound" };
    static_assert(sizeof kinds / sizeof *kinds == PLAYER_ASSET_COUNT);
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
        add_record(
            doc,
            assets[i].external ? external : embedded,
            kinds[assets[i].kind],
            &assets[i]
        );
    }
    if (icon->path) {
        add_record(doc, external, "icon", icon);
    }
    char *text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(doc);
    return text;
}
/* The catalog's first diagnostic, as source diagnostics are reported. */
static bool catalog_valid(const source_catalog *catalog, char *error, size_t size)
{
    if (!source_catalog_diagnostic_count(catalog)) {
        return true;
    }
    const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(catalog, 0);
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
/* Copies the supplied module of each source, in the catalog's order and named
 * as the source declares itself, as stage zero names its own; every module
 * must belong to exactly one source. */
static bool supplied_modules(
    const source_catalog *catalog,
    const package_module *supplied,
    int count,
    package_module **modules,
    char *error,
    size_t size
)
{
    size_t total = 0;
    for (int i = 0; i < source_catalog_source_count(catalog); i++) {
        const source_catalog_source *source = source_catalog_source_at(catalog, i);
        uint64_t id = script_catalog_id("module", source->identity);
        const package_module *module = NULL;
        for (int j = 0; j < count && !module; j++) {
            module = supplied[j].id == id ? &supplied[j] : NULL;
        }
        if (!module) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "%s: no prepared module was supplied",
                    source->identity
                );
            }
            return false;
        }
        if (module->length > PACKAGE_MAX_BYTES - total) {
            return fail(error, size, "Prepared module closure exceeds package byte limit");
        }
        total += module->length;
        unsigned char *bytes = xmalloc(module->length ? module->length : 1);
        memcpy(bytes, module->data, module->length);
        apush(
            *modules,
            ((
                package_module
            ){ xstrdup(source->declarations->name), id, bytes, module->length })
        );
    }
    return acount(*modules) == count ||
           fail(error, size, "A supplied module repeats another or belongs to no project source");
}
/* Prepares the project's game artifact. Stage zero compiles its sources when
 * `supplied` is NULL; otherwise they are the supplied prepared modules. */
static bool prepare(
    const authoring_project_options *options,
    const package_module *supplied,
    int supplied_count,
    authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    assert(options && artifact && (!size || error));
    assert(!artifact->data && !artifact->manifest && !artifact->cost_report);
    *artifact = (authoring_project_artifact){ 0 };
    if (size) {
        error[0] = 0;
    }
    if (!options->project || !options->native ||
        options->components.component_count < 0 ||
        (options->components.component_count && !options->components.components) ||
        (options->modules & ~WORLD_MODULES_ALL) ||
        options->cost_report < AUTHORING_COST_REPORT_NONE ||
        options->cost_report > AUTHORING_COST_REPORT_JSON ||
        (supplied && options->cost_report)) {
        return fail(error, size, "Invalid project preparation options");
    }
    bool valid = false;
    clear_visuals();
    chibi_project project = { 0 };
    ecs_world *schemas = ecs_world_new();
    material_fields *fields = material_fields_new();
    package_collection *collection = NULL;
    authoring_language_catalog *native = NULL;
    authoring_sources *sources = NULL;
    source_catalog *declarations = NULL;
    const source_catalog *catalog = NULL;
    project_prototype *prototypes = NULL;
    ecs_relationship_desc **relationships = NULL;
    scene *prepared_scene = NULL;
    player_named_scene *scenes = NULL;
    event_catalog *events = NULL;
    package_module *modules = NULL;
    const execution_module **definitions = NULL;
    player_prototype *prepared = NULL;
    player_asset *assets = NULL;
    player_asset icon = { 0 };
    const scene **visual_scenes = NULL;
    declared_components *declared = NULL;
    input_action *actions = NULL;
    const ecs_component_desc **components = NULL;
    size_t module_bytes = 0;
    char *scene_path = NULL, *start = NULL;
    if (chibi_project_open(&project, options->project, error, size) ||
        !application_title_valid(&project, error, size) ||
        !application_icon_read(&project, &icon, error, size)) {
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
    native = authoring_language_catalog_new(
        &(authoring_language_catalog_input){ .native = options->native },
        error,
        size
    );
    if (!native) {
        goto done;
    }
    if (supplied) {
        declarations = source_catalog_new(project.content, authoring_language_catalog_view(native));
        catalog = declarations;
        if (!catalog_valid(catalog, error, size) ||
            !supplied_modules(catalog, supplied, supplied_count, &modules, error, size)) {
            goto done;
        }
    } else {
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
        catalog = authoring_sources_source_catalog(sources);
    }
    native_prepared_context event_context = { .native = options->native };
    execution_prepared_linker native_types = native_capabilities_prepared_linker(&event_context);
    declared_components_linking event_linking = { declared, &native_types };
    execution_prepared_linker event_types = declared_components_linker(&event_linking);
    events = events_prepare(source_catalog_view(catalog), &event_types, error, size);
    if (!events) {
        goto done;
    }
    if (!project_prepare_relationships(project.content, &relationships, error, size)) {
        goto done;
    }
    prototype_registry registry = {
        .components = components,
        .component_count = acount(components),
        .relationships = (const ecs_relationship_desc *const *)relationships,
        .relationship_count = acount(relationships)
    };
    if (!project_prepare_prototypes(project.content, &registry, &prototypes, error, size)) {
        goto done;
    }
    for (int i = 0; i < acount(prototypes); i++) {
        prototype *document = prototypes[i].prototype;
        if (!authoring_prototype_parameters_fields(fields, document, error, size)) {
            goto done;
        }
        scene *one = authoring_prototype_prepare(document, error, size);
        if (!one) {
            goto done;
        }
        apush(prepared, ((player_prototype){ document->identity, one }));
        if (!attachments_valid(one, catalog, error, size) ||
            scene_register_components(one, schemas, error, size)) {
            goto done;
        }
    }
    scene_inputs inputs = { &project, prototypes, fields, catalog, schemas };
    scene_path = chibi_project_scene_path(&project, options->scene);
    if (!scene_path) {
        fail(error, size, "Project has no start scene");
        goto done;
    }
    start = name_from_path(scene_path);
    prepared_scene = prepare_scene(&inputs, scene_path, error, size);
    if (!prepared_scene || !prepare_scenes(&inputs, start, &scenes, error, size)) {
        goto done;
    }
    for (int i = 0; sources && i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        unsigned char *bytes = NULL;
        size_t length = 0;
        if (!execution_prepared_write(source->module, &bytes, &length, error, size)) {
            goto done;
        }
        if (length > PACKAGE_MAX_BYTES - module_bytes) {
            free(bytes);
            fail(error, size, "Prepared module closure exceeds package byte limit");
            goto done;
        }
        module_bytes += length;
        apush(
            modules,
            ((package_module){ xstrdup(source->analysis->name),
                               execution_module_id(source->module),
                               bytes,
                               length })
        );
        apush(definitions, source->module);
    }
    if (sources &&
        (!module_set_validate(definitions, acount(definitions), 0, error, size) ||
         !event_catalog_validate(events, definitions, acount(definitions), error, size))) {
        goto done;
    }
    if (options->cost_report) {
        artifact->cost_report = authoring_sources_cost_report(
            sources,
            options->cost_report == AUTHORING_COST_REPORT_JSON,
            error,
            size
        );
        if (!artifact->cost_report) {
            goto done;
        }
    }
    /* The encoded package is linked independently of source/compiler storage. */
    const input_action *map = source_catalog_input(catalog);
    for (int i = 0; i < acount(map); i++) {
        input_action action = map[i];
        action.name = xstrdup(action.name);
        apush(actions, action);
    }
    afree(definitions);
    definitions = NULL;
    catalog = NULL;
    authoring_sources_free(sources);
    sources = NULL;
    source_catalog_free(declarations);
    declarations = NULL;
    authoring_language_catalog_free(native);
    native = NULL;
    native_prepared_context context = { .native = options->native, .events = events };
    execution_prepared_linker native_linker = native_capabilities_prepared_linker(&context);
    declared_components_linking linking = { declared, &native_linker };
    execution_prepared_linker linker = declared_components_linker(&linking);
    size_t remaining = PACKAGE_MAX_BYTES;
    collection = package_read_budget(
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
    for (int i = 0; i < package_collection_count(collection); i++) {
        if (!execution_module_components_valid(package_collection_module(collection, i), schemas)) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "%s: Prepared component layout mismatch",
                    package_collection_name(collection, i)
                );
            }
            goto done;
        }
    }
    for (int i = 0; i < acount(prepared); i++) {
        apush(visual_scenes, prepared[i].scene);
    }
    apush(visual_scenes, prepared_scene);
    for (int i = 0; i < acount(scenes); i++) {
        apush(visual_scenes, scenes[i].scene);
    }
    if (!authoring_visual_prepare(visual_scenes, acount(visual_scenes), &assets, error, size)) {
        goto done;
    }
    if (!authoring_ui_assets_prepare(project.content, visual_scenes, acount(visual_scenes), &assets, error, size)) {
        goto done;
    }
    if (!authoring_sounds_prepare(project.content, &assets, error, size)) {
        goto done;
    }
    player_spec spec = {
        .world = { .modules = options->modules,
                   .fields = (options->modules & WORLD_MATERIALS) ? fields : NULL,
                   .events = events,
                   .actions = actions,
                   .action_count = acount(actions) },
        .declared = declared,
        .modules = modules,
        .module_count = acount(modules),
        .prototypes = prepared,
        .prototype_count = acount(prepared),
        .assets = assets,
        .asset_count = acount(assets),
        .scene = prepared_scene,
        .scene_name = start,
        .scenes = scenes,
        .scene_count = acount(scenes)
    };
    char *title = project.application.title ? project.application.title
                                            : project.name;
    player_application application = { .project_name = project.name,
                                       .title = title,
                                       .width = project.application.width,
                                       .height = project.application.height,
                                       .fullscreen = project.application.fullscreen,
                                       .icon = project.application.icon };
    valid = player_prepared_write(
        &spec,
        &application,
        &artifact->data,
        &artifact->size,
        error,
        size
    );
    if (valid) {
        artifact->manifest = manifest(assets, &icon, artifact);
        if (!artifact->manifest) {
            authoring_project_artifact_free(artifact);
            valid = fail(error, size, "Prepared resource manifest encoding failed");
        }
    }
done:
    authoring_visual_prepared_free(assets);
    afree(visual_scenes);
    if (collection) {
        package_collection_free(collection);
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
        scene_free(prepared[i].scene);
    }
    afree(prepared);
    scene_free(prepared_scene);
    for (int i = 0; i < acount(scenes); i++) {
        free((void *)scenes[i].name);
        scene_free(scenes[i].scene);
    }
    afree(scenes);
    project_prepare_prototypes_free(prototypes);
    for (int i = 0; i < acount(relationships); i++) {
        authoring_relationship_source_free(relationships[i]);
    }
    afree(relationships);
    free(start);
    free(scene_path);
    event_catalog_free(events);
    authoring_sources_free(sources);
    source_catalog_free(declarations);
    authoring_language_catalog_free(native);
    afree(components);
    input_map_free(actions);
    declared_components_free(declared);
    chibi_project_free(&project);
    authoring_mesh_unmount_content();
    clear_visuals();
    if (!valid) {
        free(artifact->cost_report);
        artifact->cost_report = NULL;
    }
    return valid;
}
bool authoring_project_prepare(
    const authoring_project_options *options,
    authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    return prepare(options, NULL, 0, artifact, error, size);
}
bool authoring_project_assemble(
    const authoring_project_options *options,
    const package_module *modules,
    int count,
    authoring_project_artifact *artifact,
    char *error,
    size_t size
)
{
    assert(count >= 0 && (!count || modules));
    static const package_module none;
    return prepare(options, count ? modules : &none, count, artifact, error, size);
}
struct authoring_project_catalog
{
    chibi_project project;
    declared_components *declared;
    authoring_language_catalog *native;
    source_catalog *sources;
};
authoring_project_catalog *authoring_project_catalog_open(
    const authoring_project_options *options,
    char *error,
    size_t size
)
{
    assert(options && options->project && options->native);
    authoring_project_catalog *catalog = xcalloc(1, sizeof *catalog);
    bool valid = !chibi_project_open(&catalog->project, options->project, error, size) &&
                 authoring_component_declarations_load(
                     catalog->project.content,
                     native_capabilities_catalog(options->native),
                     &catalog->declared,
                     error,
                     size
                 );
    if (valid) {
        catalog->native = authoring_language_catalog_new(
            &(authoring_language_catalog_input){ .native = options->native },
            error,
            size
        );
        valid = catalog->native != NULL;
    }
    if (valid) {
        catalog->sources = source_catalog_new(
            catalog->project.content,
            authoring_language_catalog_view(catalog->native)
        );
        valid = catalog_valid(catalog->sources, error, size);
    }
    if (!valid) {
        authoring_project_catalog_free(catalog);
        return NULL;
    }
    return catalog;
}
const source_catalog *authoring_project_catalog_sources(const authoring_project_catalog *catalog)
{
    return catalog->sources;
}
void authoring_project_catalog_free(authoring_project_catalog *catalog)
{
    if (!catalog) {
        return;
    }
    source_catalog_free(catalog->sources);
    authoring_language_catalog_free(catalog->native);
    declared_components_free(catalog->declared);
    chibi_project_free(&catalog->project);
    free(catalog);
}
