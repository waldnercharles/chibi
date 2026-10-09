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
    uint64_t identity = magic, version = GAME_PLAYER_PREPARED_VERSION;
    execution_wire_u64(wire, &identity);
    execution_wire_u64(wire, &version);
    if (!wire->error && (identity != magic || version != GAME_PLAYER_PREPARED_VERSION)) {
        wire->error = "Prepared player magic or version is unsupported";
    }
}
/* The front of the artifact: bounded window settings any host can read alone. */
static void application(execution_wire *wire, game_player_application *app)
{
    const char *title = app->title, *icon = app->icon;
    uint64_t width = (uint64_t)app->width, height = (uint64_t)app->height;
    execution_wire_string(wire, &title);
    execution_wire_string(wire, &icon);
    execution_wire_u64(wire, &width);
    execution_wire_u64(wire, &height);
    execution_wire_bool(wire, &app->fullscreen);
    if (wire->error) {
        return;
    }
    if ((title && (!title[0] || strlen(title) > GAME_PLAYER_APPLICATION_TEXT_MAX)) ||
        (icon && (strlen(icon) > GAME_PLAYER_APPLICATION_TEXT_MAX ||
                  !game_player_relative_path(icon))) ||
        width > GAME_PLAYER_APPLICATION_SIZE_MAX ||
        height > GAME_PLAYER_APPLICATION_SIZE_MAX) {
        wire->error = "Prepared player application settings are invalid";
        return;
    }
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
    if (*length > GAME_PLAYER_PREPARED_MAX_BYTES ||
        (wire->reading && *length > wire->size - wire->offset) ||
        (!wire->reading &&
         *length > GAME_PLAYER_PREPARED_MAX_BYTES - (size_t)acount(wire->data))) {
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
    execution_wire_count(wire, value, 8);
    if (!wire->error && *value > GAME_PACKAGE_MAX_MODULES) {
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
static void assets(execution_wire *wire, game_player_asset **records, int *count)
{
    execution_wire_count(wire, count, 40);
    if (!wire->error && *count > GAME_PLAYER_MAX_ASSETS) {
        wire->error = "Prepared player resource count exceeds limit";
    }
    size_t bytes = 0;
    for (int i = 0; i < *count && !wire->error; i++) {
        game_player_asset asset = wire->reading ? (game_player_asset){ 0 }
                                                : (*records)[i];
        uint64_t kind = asset.kind;
        wire_name(wire, &asset.name);
        execution_wire_u64(wire, &kind);
        if (!wire->error && kind >= GAME_PLAYER_ASSET_COUNT) {
            wire->error = "Prepared player resource kind is invalid";
        }
        asset.kind = (game_player_asset_kind)kind;
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
        if (!wire->error && (!game_player_asset_valid(&asset) ||
                             asset.length > GAME_PLAYER_MAX_ASSET_BYTES - bytes)) {
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
static void scene_write(execution_wire *wire, const game_scene *scene, char *error, size_t size)
{
    unsigned char *data = NULL;
    size_t length = 0;
    if (!wire->error &&
        (!scene || !game_scene_prepared_write(scene, &data, &length, error, size))) {
        wire->error = "Prepared player scene encoding failed";
    }
    if (!wire->error) {
        blob(wire, data, &length);
    }
    free(data);
}
bool game_player_prepared_write(
    const game_player_spec *spec,
    const game_player_application *app,
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
    if (spec->module_count < 0 || spec->module_count > GAME_PACKAGE_MAX_MODULES ||
        (spec->module_count && !spec->modules) || spec->prototype_count < 0 ||
        spec->prototype_count > GAME_PACKAGE_MAX_MODULES ||
        (spec->prototype_count && !spec->prototypes) || spec->world.ecs ||
        (spec->world.modules & ~GAME_WORLD_MODULES_ALL) ||
        spec->asset_count < 0 || spec->asset_count > GAME_PLAYER_MAX_ASSETS ||
        (spec->asset_count && !spec->assets)) {
        return fail(error, size, "Invalid prepared player record metadata");
    }
    execution_wire wire = { 0 };
    header(&wire);
    game_player_application settings = *app;
    application(&wire, &settings);
    uint64_t flags = spec->world.modules;
    execution_wire_u64(&wire, &flags);
    scene_write(&wire, spec->scene, error, size);
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
        !game_event_catalog_write(spec->world.events, &events, &event_length, error, size)) {
        wire.error = "Prepared player event catalog encoding failed";
    }
    if (!wire.error) {
        blob(&wire, events, &event_length);
    }
    free(events);
    int modules = spec->module_count;
    count(&wire, &modules);
    for (int i = 0; i < modules && !wire.error; i++) {
        const game_package_module *m = &spec->modules[i];
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
        const game_player_prototype *p = &spec->prototypes[i];
        const char *name = p->name;
        if (!name || !name[0] || !p->scene || game_scene_object_count(p->scene) != 1) {
            wire.error = "Prepared player prototype record is incomplete";
            break;
        }
        wire_name(&wire, &name);
        scene_write(&wire, p->scene, error, size);
    }
    game_player_asset *asset_records = (game_player_asset *)spec->assets;
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
static game_scene *scene_read(
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
    game_scene *scene = game_scene_prepared_read_budget(data, length, catalog, remaining, error, size);
    if (!scene) {
        wire->error = "Prepared player scene decoding failed";
    }
    return scene;
}
game_player *game_player_prepared_read(
    const void *data,
    size_t length,
    const game_player_host *host,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    assert(host && (!size || error));
    if (size) {
        error[0] = 0;
    }
    if (!data || length > GAME_PLAYER_PREPARED_MAX_BYTES) {
        fail(error, size, "Invalid prepared player bytes");
        return NULL;
    }
    arena storage = { 0 };
    size_t metadata_budget = GAME_PLAYER_PREPARED_MAX_BYTES;
    execution_wire wire = { .data = (unsigned char *)data,
                            .size = length,
                            .reading = true,
                            .storage = &storage,
                            .storage_budget = &metadata_budget };
    header(&wire);
    game_player_application settings = { 0 };
    application(&wire, &settings);
    uint64_t flags = 0;
    execution_wire_u64(&wire, &flags);
    if (!wire.error && (flags & ~(uint64_t)GAME_WORLD_MODULES_ALL)) {
        wire.error = "Prepared player module selection is invalid";
    }
    game_player_spec spec = { .world = { .modules = (unsigned)flags,
                                         .components = host->components,
                                         .log = host->log,
                                         .sdf_motion = host->sdf_motion },
                              .linker = host->linker,
                              .backend = backend };
    game_player *player = NULL;
    game_package_module *modules = NULL;
    game_player_prototype *prototypes = NULL;
    game_player_asset *asset_records = NULL;
    spec.resources = host->resources;
    size_t decoded_budget = host->decoded_budget ? host->decoded_budget
                                                 : GAME_PLAYER_PREPARED_MAX_BYTES;
    spec.decoded_remaining = &decoded_budget;
    spec.scene = scene_read(&wire, host->components, &decoded_budget, error, size);
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
        spec.world.events = game_event_catalog_read_budget(
            events,
            event_length,
            host->linker,
            &decoded_budget,
            error,
            size
        );
        if (!spec.world.events) {
            wire.error = "Prepared player event catalog decoding failed";
        }
    }
    count(&wire, &spec.module_count);
    for (int i = 0; i < spec.module_count && !wire.error; i++) {
        game_package_module m = { 0 };
        wire_name(&wire, &m.name);
        execution_wire_u64(&wire, &m.id);
        m.data = blob(&wire, NULL, &m.length);
        if (!wire.error) {
            apush(modules, m);
        }
    }
    spec.modules = modules;
    count(&wire, &spec.prototype_count);
    for (int i = 0; i < spec.prototype_count && !wire.error; i++) {
        game_player_prototype p = { 0 };
        wire_name(&wire, &p.name);
        p.scene = scene_read(&wire, host->components, &decoded_budget, error, size);
        if (p.scene) {
            apush(prototypes, p);
        }
    }
    spec.prototypes = prototypes;
    assets(&wire, &asset_records, &spec.asset_count);
    spec.assets = asset_records;
    if (!wire.error && wire.offset != wire.size) {
        wire.error = "Prepared player bytes have trailing data";
    }
    if (!wire.error) {
        player = game_player_new(&spec, error, size);
    } else if (!size || !error[0]) {
        fail(error, size, wire.error);
    }
    game_scene_free(spec.scene);
    for (int i = 0; i < acount(prototypes); i++) {
        game_scene_free(prototypes[i].scene);
    }
    afree(prototypes);
    afree(modules);
    afree(asset_records);
    if (spec.world.fields) {
        material_fields_free(spec.world.fields);
    }
    game_event_catalog_free(spec.world.events);
    arena_free(&storage);
    return player;
}
bool game_player_application_read(const char *path, game_player_application *app, char *error, size_t size)
{
    assert(path && app && (!size || error));
    *app = (game_player_application){ 0 };
    /* Header, two bounded strings with their sizes, two sizes and a flag. */
    unsigned char data[2 * GAME_PLAYER_APPLICATION_TEXT_MAX + 256];
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
    game_player_application read = { 0 };
    application(&wire, &read);
    if (wire.error) {
        arena_free(&storage);
        return fail(error, size, wire.error);
    }
    *app = read;
    app->title = read.title ? xstrdup(read.title) : NULL;
    app->icon = read.icon ? xstrdup(read.icon) : NULL;
    arena_free(&storage);
    return true;
}
void game_player_application_free(game_player_application *app)
{
    free(app->title);
    free(app->icon);
    *app = (game_player_application){ 0 };
}
game_player *game_player_open(
    const char *path,
    const game_player_host *host,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    assert(path && host && (!size || error));
    FILE *file = fopen(path, "rb");
    if (!file) {
        fail(error, size, "Prepared player file could not be opened");
        return NULL;
    }
    unsigned char *data = NULL;
    game_player *player = NULL;
    if (fseek(file, 0, SEEK_END)) {
        goto reject;
    }
    long length = ftell(file);
    if (length < 0 || length > GAME_PLAYER_PREPARED_MAX_BYTES ||
        fseek(file, 0, SEEK_SET)) {
        goto reject;
    }
    data = xmalloc((size_t)length);
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        goto reject;
    }
    player = game_player_prepared_read(data, (size_t)length, host, backend, error, size);
    free(data);
    fclose(file);
    return player;
reject:
    free(data);
    fclose(file);
    fail(error, size, "Prepared player file is unreadable or exceeds byte limit");
    return NULL;
}
