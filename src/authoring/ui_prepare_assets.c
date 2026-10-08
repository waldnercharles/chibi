#include "authoring/ui_prepare.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "runtime/ui/builtin_fonts.h"
#include "runtime/ui/font.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define UI_DOCUMENTS_MAX 1024
#define UI_DOCUMENT_BYTES_MAX (512 * 1024)
#define UI_DOCUMENT_TOTAL_MAX (32 * 1024 * 1024)
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static bool contains(const game_player_asset *assets, const char *key, game_player_asset_kind kind)
{
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].kind == kind && !strcmp(assets[i].name, key)) {
            return true;
        }
    }
    return false;
}
static bool append(
    game_player_asset **assets,
    const char *key,
    game_player_asset_kind kind,
    unsigned char *bytes,
    size_t length,
    size_t *total,
    char *error,
    size_t size
)
{
    if (!bytes || !length || acount(*assets) == GAME_PLAYER_MAX_ASSETS ||
        length > GAME_PLAYER_MAX_ASSET_BYTES - *total) {
        free(bytes);
        return fail(error, size, "Prepared UI resource collection exceeds limit");
    }
    apush(
        *assets,
        ((game_player_asset){ .name = xstrdup(key),
                              .kind = kind,
                              .data = bytes,
                              .length = length,
                              .content_hash = game_player_asset_fingerprint(bytes, length) })
    );
    *total += length;
    return true;
}
static void dependency(game_ui_document *doc, game_ui_resource_kind kind, const char *key)
{
    for (int i = 0; i < acount(doc->resources); i++) {
        if (doc->resources[i].kind == kind && !strcmp(doc->resources[i].key, key)) {
            return;
        }
    }
    apush(doc->resources, ((game_ui_resource){ kind, xstrdup(key) }));
}
static bool font(
    game_ui_document *doc,
    const char *family,
    game_player_asset **assets,
    size_t *total,
    char *error,
    size_t size
)
{
    bool found = false;
    for (int i = 0; i < game_ui_builtin_font_count(); i++) {
        game_ui_font face = game_ui_builtin_font_at(i);
        if (strcasecmp(family, face.family)) {
            continue;
        }
        found = true;
        if (!contains(*assets, face.key, GAME_PLAYER_ASSET_FONT)) {
            unsigned char *bytes = NULL;
            size_t length = 0;
            if (!game_ui_font_prepared_write(&face, &bytes, &length, error, size) ||
                !append(assets, face.key, GAME_PLAYER_ASSET_FONT, bytes, length, total, error, size)) {
                return false;
            }
        }
        dependency(doc, GAME_UI_RESOURCE_FONT, face.key);
    }
    if (!found && size) {
        snprintf(error, size, "UI font family '%s' has no supplied font bytes", family);
    }
    return found;
}
static bool image(
    game_ui_document *doc,
    const char *url,
    game_player_asset **assets,
    size_t *total,
    char *error,
    size_t size
)
{
    char *key = xstrdup(url);
    size_t length = strlen(key);
    if (length >= 8 && !strcmp(key + length - 8, "#nearest")) {
        key[length - 8] = 0;
    }
    if (strncmp(key, "content/", 8) || strstr(key, "..") || strchr(key, '\\')) {
        free(key);
        return fail(error, size, "UI image URL must resolve to a content-relative resident image");
    }
    bool valid = true;
    if (!contains(*assets, key, GAME_PLAYER_ASSET_IMAGE)) {
        unsigned char *bytes = NULL;
        size_t length = 0;
        shader_error diagnostic = { 0 };
        valid = render_image_reload(key, error, size) &&
                render_image_prepared_write(key, &bytes, &length, &diagnostic);
        if (!valid && diagnostic.message[0] && size) {
            snprintf(error, size, "%s", diagnostic.message);
        }
        if (valid) {
            valid = append(assets, key, GAME_PLAYER_ASSET_IMAGE, bytes, length, total, error, size);
        }
    }
    if (valid) {
        dependency(doc, GAME_UI_RESOURCE_IMAGE, key);
    }
    free(key);
    return valid;
}
static bool document(
    const char *content,
    const char *name,
    game_player_asset **assets,
    size_t *total,
    char *error,
    size_t size
)
{
    game_ui_document doc = { 0 };
    ui_source_resource *resources = NULL;
    bool valid = authoring_ui_prepare(content, name, &doc, &resources, error, size);
    for (int i = 0; valid && i < acount(resources); i++) {
        ui_source_resource *r = &resources[i];
        valid = r->kind == UI_SOURCE_RESOURCE_FONT_FAMILY
                  ? font(&doc, r->text, assets, total, error, size)
                  : image(&doc, r->text, assets, total, error, size);
    }
    if (valid) {
        unsigned char *bytes = NULL;
        size_t length = 0;
        valid = game_ui_document_prepared_write(&doc, &bytes, &length, error, size) &&
                append(assets, name, GAME_PLAYER_ASSET_UI, bytes, length, total, error, size);
    }
    ui_source_resources_free(resources);
    game_ui_document_free(&doc);
    return valid;
}
static int compare(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
bool authoring_ui_assets_prepare(
    const char *content,
    const game_scene *const *scenes,
    int scene_count,
    game_player_asset **assets,
    char *error,
    size_t size
)
{
    int before = acount(*assets);
    char directory[4096];
    if (snprintf(directory, sizeof directory, "%s/ui", content) >= (int)sizeof directory) {
        return fail(error, size, "UI directory path exceeds limit");
    }
    DIR *dir = opendir(directory);
    if (!dir && errno != ENOENT) {
        return fail(error, size, "Cannot open UI document directory");
    }
    char **names = NULL;
    struct dirent *entry;
    size_t input = 0, total = 0;
    bool valid = true;
    while (dir && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        size_t n = strlen(name);
        if (name[0] == '.' || n < 4 || strcmp(name + n - 4, ".rml")) {
            continue;
        }
        char path[4096];
        struct stat info;
        if (acount(names) == UI_DOCUMENTS_MAX ||
            snprintf(path, sizeof path, "%s/%s", directory, name) >= (int)sizeof path ||
            stat(path, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
            info.st_size > UI_DOCUMENT_BYTES_MAX ||
            (size_t)info.st_size > UI_DOCUMENT_TOTAL_MAX - input) {
            valid = fail(error, size, "UI source document collection exceeds limit");
            break;
        }
        input += (size_t)info.st_size;
        char *key = xstrdup(name);
        key[n - 4] = 0;
        apush(names, key);
    }
    if (dir) {
        closedir(dir);
    }
    if (acount(names) > 1) {
        qsort(names, (size_t)acount(names), sizeof *names, compare);
    }
    for (int i = 0; i < acount(*assets); i++) {
        if ((*assets)[i].length > GAME_PLAYER_MAX_ASSET_BYTES - total) {
            valid = fail(error, size, "Prepared visual resource collection exceeds limit");
            break;
        }
        total += (*assets)[i].length;
    }
    for (int i = 0; valid && i < acount(names); i++) {
        valid = document(content, names[i], assets, &total, error, size);
    }
    for (int i = 0; valid && i < scene_count; i++) {
        const char *name = game_scene_ui(scenes[i]);
        if (name && *name && !contains(*assets, name, GAME_PLAYER_ASSET_UI)) {
            valid = fail(error, size, "Scene requires a missing prepared UI document");
        }
        for (int j = 0; valid && j < game_scene_object_count(scenes[i]); j++) {
            for (int k = 0;
                 valid && k < game_scene_object_component_count(scenes[i], j);
                 k++) {
                game_scene_component c = game_scene_object_component_at(scenes[i], j, k);
                if (c.desc->id != GAME_COMPONENT_GameUi.id) {
                    continue;
                }
                const GameUi *ui = c.data;
                if (ui->document && *ui->document &&
                    !contains(*assets, ui->document, GAME_PLAYER_ASSET_UI)) {
                    valid = fail(error, size, "Entity requires a missing prepared UI document");
                }
            }
        }
    }
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    if (!valid) {
        while (acount(*assets) > before) {
            game_player_asset *asset = &alast(*assets);
            free((void *)asset->name);
            free((void *)asset->data);
            asetlen(*assets, acount(*assets) - 1);
        }
    }
    return valid;
}
