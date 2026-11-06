#include "runtime/player_prepared.h"

#include "runtime/material_field_prepared.h"
#include "runtime/scenes/prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/value_wire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t magic = UINT64_C(0x31594c5053484343); /* CCHSPLY1 */
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static void header(execution_wire *wire)
{
    uint64_t identity = magic, version = PLAYER_PREPARED_VERSION;
    execution_wire_u64(wire, &identity);
    execution_wire_u64(wire, &version);
    if (!wire->error && (identity != magic || version != PLAYER_PREPARED_VERSION)) {
        wire->error = "Prepared player magic or version is unsupported";
    }
}
/* The front of the artifact: bounded window settings any host can read alone. */
static void application(execution_wire *wire, player_application *app)
{
    const char *project_name = app->project_name, *title = app->title,
               *icon = app->icon;
    uint64_t width = (uint64_t)app->width, height = (uint64_t)app->height;
    execution_wire_string(wire, &project_name);
    execution_wire_string(wire, &title);
    execution_wire_string(wire, &icon);
    execution_wire_u64(wire, &width);
    execution_wire_u64(wire, &height);
    execution_wire_bool(wire, &app->fullscreen);
    if (wire->error) {
        return;
    }
    if ((project_name &&
         (!project_name[0] || strlen(project_name) > PLAYER_APPLICATION_TEXT_MAX)) ||
        (title && (!title[0] || strlen(title) > PLAYER_APPLICATION_TEXT_MAX)) ||
        (icon && (strlen(icon) > PLAYER_APPLICATION_TEXT_MAX ||
                  !player_relative_path(icon))) ||
        width > PLAYER_APPLICATION_SIZE_MAX || height > PLAYER_APPLICATION_SIZE_MAX) {
        wire->error = "Prepared player application settings are invalid";
        return;
    }
    app->project_name = (char *)project_name;
    app->title = (char *)title;
    app->icon = (char *)icon;
    app->width = (int)width;
    app->height = (int)height;
}
static const void *blob(execution_wire *wire, const void *data, size_t *length)
{
    execution_wire_size(wire, length);
    if (wire->error) {
        return NULL;
    }
    if (*length > PLAYER_PREPARED_MAX_BYTES ||
        (wire->reading && *length > wire->size - wire->offset) ||
        (!wire->reading &&
         *length > PLAYER_PREPARED_MAX_BYTES - (size_t)acount(wire->data))) {
        wire->error = "Prepared player payload exceeds byte limit";
        return NULL;
    }
    if (wire->reading) {
        const void *result = wire->data + wire->offset;
        wire->offset += *length;
        return result;
    }
    if (*length && !data) {
        wire->error = "Prepared player payload is missing";
        return NULL;
    }
    int offset = acount(wire->data);
    asetlen(wire->data, offset + (int)*length);
    if (*length) {
        memcpy(wire->data + offset, data, *length);
    }
    wire->size += *length;
    wire->offset = wire->size;
    return data;
}
static void count(execution_wire *wire, int *value)
{
    execution_wire_count(wire, value, 1);
    if (!wire->error && *value > PACKAGE_MAX_MODULES) {
        wire->error = "Prepared player record count exceeds limit";
    }
}
static void wire_name(execution_wire *wire, const char **value)
{
    execution_wire_string(wire, value);
    if (!wire->error && (!*value || !(*value)[0] || strnlen(*value, 1025) > 1024)) {
        wire->error = "Prepared player record name is invalid";
    }
}
static void assets(execution_wire *wire, player_asset **records, int *count)
{
    execution_wire_count(wire, count, 5);
    if (!wire->error && *count > PLAYER_MAX_ASSETS) {
        wire->error = "Prepared player resource count exceeds limit";
    }
    size_t bytes = 0;
    for (int i = 0; i < *count && !wire->error; i++) {
        player_asset asset = wire->reading ? (player_asset){ 0 } : (*records)[i];
        uint64_t kind = asset.kind;
        wire_name(wire, &asset.name);
        execution_wire_u64(wire, &kind);
        if (!wire->error && kind >= PLAYER_ASSET_COUNT) {
            wire->error = "Prepared player resource kind is invalid";
        }
        asset.kind = (player_asset_kind)kind;
        execution_wire_bool(wire, &asset.external);
        execution_wire_string(wire, &asset.path);
        execution_wire_u64(wire, &asset.content_hash);
        execution_wire_size(wire, &asset.length);
        if (!asset.external) {
            size_t length = asset.length;
            asset.data = blob(wire, asset.data, &length);
            if (!wire->error && length != asset.length) {
                wire->error = "Prepared player resource length mismatch";
            }
        }
        if (!wire->error && (!player_asset_valid(&asset) ||
                             asset.length > PLAYER_MAX_ASSET_BYTES - bytes)) {
            wire->error = "Prepared player resource metadata is invalid";
        }
        bytes += asset.length;
        for (int j = 0; j < i && !wire->error; j++) {
            if (asset.kind == (*records)[j].kind &&
                !strcmp(asset.name, (*records)[j].name)) {
                wire->error = "Prepared player resource identity is duplicated";
            }
        }
        if (wire->reading && !wire->error) {
            apush(*records, asset);
        }
    }
}
static void actions(execution_wire *wire, input_action **records, int *count)
{
    execution_wire_count(wire, count, 5);
    for (int i = 0; i < *count && !wire->error; i++) {
        input_action action = wire->reading ? (input_action){ 0 } : (*records)[i];
        wire_name(wire, &action.name);
        execution_wire_u64(wire, &action.controls);
        execution_wire_bool(wire, &action.firing);
        execution_wire_int(wire, &action.controller);
        uint64_t buttons = action.buttons;
        execution_wire_u64(wire, &buttons);
        if (buttons > UINT32_MAX) {
            wire->error = "Prepared player controller buttons are invalid";
        }
        action.buttons = (uint32_t)buttons;
        if (wire->reading && !wire->error) {
            apush(*records, action);
        }
    }
}
/* Refuses a scene the player would refuse to enter, so compiling catches it. */
static void scene_write(execution_wire *wire, const scene *scene, char *error, size_t size)
{
    unsigned char *data = NULL;
    size_t length = 0;
    if (!wire->error && (!scene || !player_scene_bindings_valid(scene, error, size) ||
                         !scene_prepared_write(scene, &data, &length, error, size))) {
        wire->error = "Prepared player scene encoding failed";
    }
    if (!wire->error) {
        blob(wire, data, &length);
    }
    free(data);
}
bool player_prepared_write(
    const player_spec *spec,
    const player_application *app,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(spec && app && data && length && (!size || error));
    *data = NULL;
    *length = 0;
    if (size) {
        error[0] = 0;
    }
    if (!spec->scene || spec->module_count < 0 ||
        spec->module_count > PACKAGE_MAX_MODULES ||
        (spec->module_count && !spec->modules) || spec->prototype_count < 0 ||
        spec->prototype_count > PACKAGE_MAX_MODULES ||
        (spec->prototype_count && !spec->prototypes) || spec->scene_count < 0 ||
        spec->scene_count > PACKAGE_MAX_MODULES ||
        (spec->scene_count && !spec->scenes) || spec->world.ecs ||
        (spec->world.modules & ~WORLD_MODULES_ALL) || spec->asset_count < 0 ||
        spec->asset_count > PLAYER_MAX_ASSETS || (spec->asset_count && !spec->assets) ||
        !world_actions_valid(spec->world.actions, spec->world.action_count)) {
        return fail(error, size, "Invalid prepared player record metadata");
    }
    execution_wire wire = { 0 };
    header(&wire);
    player_application settings = *app;
    application(&wire, &settings);
    uint64_t flags = spec->world.modules;
    execution_wire_u64(&wire, &flags);
    declared_components *declared = spec->declared;
    declared_components_wire(&wire, &declared);
    scene_write(&wire, spec->scene, error, size);
    const char *start = spec->scene_name ? spec->scene_name : scene_name(spec->scene);
    wire_name(&wire, &start);
    unsigned char *fields = NULL;
    size_t field_length = 0;
    if (!wire.error && spec->world.fields &&
        !material_fields_prepared_write(spec->world.fields, &fields, &field_length, error, size)) {
        wire.error = "Prepared player field encoding failed";
    }
    if (!wire.error) {
        blob(&wire, fields, &field_length);
    }
    free(fields);
    unsigned char *events = NULL;
    size_t event_length = 0;
    if (!wire.error && spec->world.events &&
        !event_catalog_write(spec->world.events, &events, &event_length, error, size)) {
        wire.error = "Prepared player event catalog encoding failed";
    }
    if (!wire.error) {
        blob(&wire, events, &event_length);
    }
    free(events);
    input_action *input = (input_action *)spec->world.actions;
    int action_count = spec->world.action_count;
    actions(&wire, &input, &action_count);
    int modules = spec->module_count;
    count(&wire, &modules);
    for (int i = 0; i < modules && !wire.error; i++) {
        const package_module *m = &spec->modules[i];
        const char *name = m->name;
        uint64_t id = m->id;
        if (!name || !name[0] || !id || !m->length) {
            wire.error = "Prepared player module record is incomplete";
            break;
        }
        wire_name(&wire, &name);
        execution_wire_u64(&wire, &id);
        size_t bytes = m->length;
        blob(&wire, m->data, &bytes);
    }
    int prototypes = spec->prototype_count;
    count(&wire, &prototypes);
    for (int i = 0; i < prototypes && !wire.error; i++) {
        const player_prototype *p = &spec->prototypes[i];
        const char *name = p->name;
        if (!name || !name[0] || !p->scene || !scene_is_tree(p->scene)) {
            wire.error = "Prepared player prototype record is incomplete";
            break;
        }
        wire_name(&wire, &name);
        scene_write(&wire, p->scene, error, size);
    }
    int scenes = spec->scene_count;
    count(&wire, &scenes);
    for (int i = 0; i < scenes && !wire.error; i++) {
        const char *name = spec->scenes[i].name;
        wire_name(&wire, &name);
        scene_write(&wire, spec->scenes[i].scene, error, size);
    }
    player_asset *asset_records = (player_asset *)spec->assets;
    int asset_count = spec->asset_count;
    assets(&wire, &asset_records, &asset_count);
    if (wire.error) {
        if (!size || !error[0]) {
            fail(error, size, wire.error);
        }
        afree(wire.data);
        return false;
    }
    *length = (size_t)acount(wire.data);
    *data = xmalloc(*length);
    memcpy(*data, wire.data, *length);
    afree(wire.data);
    return true;
}
static scene *scene_read(
    execution_wire *wire,
    ecs_component_catalog catalog,
    size_t *remaining,
    char *error,
    size_t size
)
{
    size_t length = 0;
    const void *data = blob(wire, NULL, &length);
    if (wire->error) {
        return NULL;
    }
    scene *scene = scene_prepared_read_budget(data, length, catalog, remaining, error, size);
    if (!scene) {
        wire->error = "Prepared player scene decoding failed";
    }
    return scene;
}
/* Reads the module records, which borrow their bytes from the artifact. */
static void module_records(execution_wire *wire, package_module **modules, int *module_count)
{
    count(wire, module_count);
    for (int i = 0; i < *module_count && !wire->error; i++) {
        package_module m = { 0 };
        wire_name(wire, &m.name);
        execution_wire_u64(wire, &m.id);
        m.data = blob(wire, NULL, &m.length);
        if (!wire->error) {
            apush(*modules, m);
        }
    }
}
player *player_prepared_read(
    const void *data,
    size_t length,
    const player_host *host,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    assert(host && (!size || error));
    if (size) {
        error[0] = 0;
    }
    if (!data || length > PLAYER_PREPARED_MAX_BYTES) {
        fail(error, size, "Invalid prepared player bytes");
        return NULL;
    }
    arena storage = { 0 };
    size_t metadata_budget = PLAYER_PREPARED_MAX_BYTES;
    execution_wire wire = { .data = (unsigned char *)data,
                            .size = length,
                            .reading = true,
                            .storage = &storage,
                            .storage_budget = &metadata_budget };
    header(&wire);
    player_application settings = { 0 };
    application(&wire, &settings);
    uint64_t flags = 0;
    execution_wire_u64(&wire, &flags);
    if (!wire.error && (flags & ~(uint64_t)WORLD_MODULES_ALL)) {
        wire.error = "Prepared player module selection is invalid";
    }
    /* Declared components precede everything that stores their rows. */
    declared_components *declared = NULL;
    declared_components_wire(&wire, &declared);
    const ecs_component_desc **components = NULL;
    for (int i = 0; i < host->components.component_count; i++) {
        apush(components, host->components.components[i]);
    }
    ecs_component_catalog extra = declared ? declared_components_ecs(declared)
                                           : (ecs_component_catalog){ 0 };
    for (int i = 0; i < extra.component_count; i++) {
        apush(components, extra.components[i]);
    }
    ecs_component_catalog catalog = { components, acount(components) };
    declared_components_linking linking = { declared, host->linker };
    execution_prepared_linker linker = host->linker
                                         ? declared_components_linker(&linking)
                                         : (execution_prepared_linker){ 0 };
    player_spec spec = { .world = { .modules = (unsigned)flags,
                                    .components = catalog,
                                    .log = host->log },
                         .declared = declared,
                         .linker = host->linker ? &linker : NULL,
                         .natives = host->natives,
                         .native_count = host->native_count,
                         .native_runtime = host->native_runtime,
                         .backend = backend };
    player *player = NULL;
    package_module *modules = NULL;
    player_prototype *prototypes = NULL;
    player_named_scene *scenes = NULL;
    player_asset *asset_records = NULL;
    spec.resources = host->resources;
    size_t decoded_budget = host->decoded_budget ? host->decoded_budget
                                                 : PLAYER_PREPARED_MAX_BYTES;
    spec.decoded_remaining = &decoded_budget;
    spec.scene = scene_read(&wire, catalog, &decoded_budget, error, size);
    wire_name(&wire, &spec.scene_name);
    size_t field_length = 0;
    const void *fields = blob(&wire, NULL, &field_length);
    if (!wire.error && field_length) {
        spec.world.fields = material_fields_prepared_read_budget(
            fields,
            field_length,
            &decoded_budget,
            error,
            size
        );
        if (!spec.world.fields) {
            wire.error = "Prepared player field decoding failed";
        }
    }
    size_t event_length = 0;
    const void *events = blob(&wire, NULL, &event_length);
    if (!wire.error && event_length) {
        spec.world.events = event_catalog_read_budget(
            events,
            event_length,
            spec.linker,
            &decoded_budget,
            error,
            size
        );
        if (!spec.world.events) {
            wire.error = "Prepared player event catalog decoding failed";
        }
    }
    input_action *input = NULL;
    actions(&wire, &input, &spec.world.action_count);
    spec.world.actions = input;
    module_records(&wire, &modules, &spec.module_count);
    spec.modules = modules;
    count(&wire, &spec.prototype_count);
    for (int i = 0; i < spec.prototype_count && !wire.error; i++) {
        player_prototype p = { 0 };
        wire_name(&wire, &p.name);
        p.scene = scene_read(&wire, catalog, &decoded_budget, error, size);
        if (p.scene) {
            apush(prototypes, p);
        }
    }
    spec.prototypes = prototypes;
    count(&wire, &spec.scene_count);
    for (int i = 0; i < spec.scene_count && !wire.error; i++) {
        player_named_scene named = { 0 };
        wire_name(&wire, &named.name);
        named.scene = scene_read(&wire, catalog, &decoded_budget, error, size);
        if (named.scene) {
            apush(scenes, named);
        }
    }
    spec.scenes = scenes;
    assets(&wire, &asset_records, &spec.asset_count);
    spec.assets = asset_records;
    if (!wire.error && wire.offset != wire.size) {
        wire.error = "Prepared player bytes have trailing data";
    }
    if (!wire.error) {
        player = player_new(&spec, error, size);
    } else if (!size || !error[0]) {
        fail(error, size, wire.error);
    }
    scene_free(spec.scene);
    for (int i = 0; i < acount(prototypes); i++) {
        scene_free(prototypes[i].scene);
    }
    afree(prototypes);
    for (int i = 0; i < acount(scenes); i++) {
        scene_free(scenes[i].scene);
    }
    afree(scenes);
    afree(modules);
    afree(asset_records);
    afree(input);
    if (spec.world.fields) {
        material_fields_free(spec.world.fields);
    }
    event_catalog_free(spec.world.events);
    afree(components);
    declared_components_free(declared);
    arena_free(&storage);
    return player;
}
/* Skips the sections before the modules in the order player_prepared_read
 * decodes them; the two must stay in step. */
bool player_prepared_modules(
    const void *data,
    size_t length,
    arena *names,
    package_module **modules,
    size_t *first,
    size_t *end,
    char *error,
    size_t size
)
{
    assert(names && modules && first && end && (!size || error));
    *modules = NULL;
    if (!data || length > PLAYER_PREPARED_MAX_BYTES) {
        return fail(error, size, "Invalid prepared player bytes");
    }
    size_t budget = PLAYER_PREPARED_MAX_BYTES;
    execution_wire wire = { .data = (unsigned char *)data,
                            .size = length,
                            .reading = true,
                            .storage = names,
                            .storage_budget = &budget };
    header(&wire);
    player_application settings = { 0 };
    application(&wire, &settings);
    uint64_t flags = 0;
    execution_wire_u64(&wire, &flags);
    declared_components *declared = NULL;
    declared_components_wire(&wire, &declared);
    declared_components_free(declared);
    size_t skipped = 0;
    blob(&wire, NULL, &skipped);
    const char *start = NULL;
    wire_name(&wire, &start);
    blob(&wire, NULL, &skipped);
    blob(&wire, NULL, &skipped);
    input_action *input = NULL;
    int action_count = 0;
    actions(&wire, &input, &action_count);
    afree(input);
    *first = wire.offset;
    int module_count = 0;
    module_records(&wire, modules, &module_count);
    *end = wire.offset;
    if (wire.error) {
        afree(*modules);
        *modules = NULL;
        return fail(error, size, wire.error);
    }
    return true;
}
bool player_application_read(const char *path, player_application *app, char *error, size_t size)
{
    assert(path && app && (!size || error));
    *app = (player_application){ 0 };
    /* Header, three bounded strings with their sizes, two sizes and a flag. */
    unsigned char data[3 * PLAYER_APPLICATION_TEXT_MAX + 256];
    FILE *file = fopen(path, "rb");
    if (!file) {
        return fail(error, size, "Prepared player file could not be opened");
    }
    size_t length = fread(data, 1, sizeof data, file);
    fclose(file);
    /* The bounded prefix already bounds the strings this can allocate. */
    arena storage = { 0 };
    execution_wire wire = { .data = data, .size = length, .reading = true, .storage = &storage };
    header(&wire);
    player_application read = { 0 };
    application(&wire, &read);
    if (wire.error) {
        arena_free(&storage);
        return fail(error, size, wire.error);
    }
    *app = read;
    app->project_name = read.project_name ? xstrdup(read.project_name) : NULL;
    app->title = read.title ? xstrdup(read.title) : NULL;
    app->icon = read.icon ? xstrdup(read.icon) : NULL;
    arena_free(&storage);
    return true;
}
void player_application_free(player_application *app)
{
    free(app->project_name);
    free(app->title);
    free(app->icon);
    *app = (player_application){ 0 };
}
player *player_open(const char *path, const player_host *host, behavior_backend backend, char *error, size_t size)
{
    assert(path && host && (!size || error));
    FILE *file = fopen(path, "rb");
    if (!file) {
        fail(error, size, "Prepared player file could not be opened");
        return NULL;
    }
    unsigned char *data = NULL;
    player *player = NULL;
    if (fseek(file, 0, SEEK_END)) {
        goto reject;
    }
    long length = ftell(file);
    if (length < 0 || length > PLAYER_PREPARED_MAX_BYTES || fseek(file, 0, SEEK_SET)) {
        goto reject;
    }
    data = xmalloc((size_t)length);
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        goto reject;
    }
    player = player_prepared_read(data, (size_t)length, host, backend, error, size);
    free(data);
    fclose(file);
    return player;
reject:
    free(data);
    fclose(file);
    fail(error, size, "Prepared player file is unreadable or exceeds byte limit");
    return NULL;
}
