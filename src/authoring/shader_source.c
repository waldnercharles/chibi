#include "authoring/shader_source.h"

#include "authoring/shader_prepare.h"
#include "render/shader_text.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SOURCE_FILE_BYTES (512 * 1024)
#define SOURCE_TOTAL_BYTES (32 * 1024 * 1024)
#define SOURCE_DOCUMENTS 1024
#define SOURCE_INCLUDES 32
#define SOURCE_PATH_BYTES 4096

static bool fail(char *error, size_t size, const char *format, ...)
{
    if (size) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, size, format, args);
        va_end(args);
    }
    return false;
}
static char *read_file(const char *path, size_t *budget, char *error, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fail(error, size, "Cannot read shader source %s: %s", path, strerror(errno));
        return NULL;
    }
    long length = -1;
    if (!fseek(file, 0, SEEK_END)) {
        length = ftell(file);
    }
    if (length < 0 || length > SOURCE_FILE_BYTES || (size_t)length > *budget ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        fail(error, size, "%s: shader source exceeds byte limits", path);
        return NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    bool valid = fread(text, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!valid || memchr(text, 0, (size_t)length)) {
        free(text);
        fail(error, size, "%s: shader source is incomplete or contains NUL", path);
        return NULL;
    }
    text[length] = 0;
    *budget -= (size_t)length;
    return text;
}
static bool within(const char *root, const char *path)
{
    size_t n = strlen(root);
    return !strncmp(root, path, n) && path[n] == '/';
}
static bool relative(const char *path)
{
    return path && *path && *path != '/' && !strchr(path, '\\') && !strchr(path, ':');
}
static bool suffix(const char *path, const char *ending)
{
    size_t n = strlen(path), end = strlen(ending);
    return n >= end && !strcmp(path + n - end, ending);
}
static bool resolve(
    const char *root,
    const char *directory,
    const char *path,
    char resolved[SOURCE_PATH_BYTES],
    char *error,
    size_t size
)
{
    char joined[SOURCE_PATH_BYTES];
    if (!relative(path) || !suffix(path, ".glsl") ||
        snprintf(joined, sizeof joined, "%s/%s", directory, path) >= (int)sizeof joined ||
        !realpath(joined, resolved) || !within(root, resolved)) {
        return fail(error, size, "GLSL path must resolve inside project content: %s", path ? path : "");
    }
    struct stat info;
    if (stat(resolved, &info) || !S_ISREG(info.st_mode)) {
        return fail(error, size, "GLSL source is not a regular file: %s", path);
    }
    return true;
}
static bool string(const av_value *value)
{
    return av_is_str(value) && av_get_len(value) == strlen(av_get_str(value));
}
static bool keys(const av_value *value, const char *const *allowed, int count)
{
    if (!av_is_obj(value)) {
        return false;
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        if (!string(av_obj_key_at(value, i))) {
            return false;
        }
        const char *key = av_get_str(av_obj_key_at(value, i));
        bool found = false;
        for (int j = 0; j < count; j++) {
            found |= !strcmp(key, allowed[j]);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
static bool parameter_default(const av_value *value, int width, float result[4])
{
    if (width == 1 && !av_is_num(value)) {
        return false;
    }
    if (width > 1 && (!av_is_arr(value) || av_arr_size(value) != (size_t)width)) {
        return false;
    }
    for (int i = 0; i < width; i++) {
        const av_value *number = width == 1 ? value : av_arr_get(value, (size_t)i);
        if (!av_is_num(number)) {
            return false;
        }
        result[i] = (float)av_get_num(number);
        if (!isfinite(result[i])) {
            return false;
        }
    }
    return true;
}
static bool parameters(const av_value *value, shader_prepared_asset *asset, char *error, size_t size)
{
    static const char *const builtins[] = { "Index", "Count", "Seed" };
    for (int i = 0; i < 3; i++) {
        asset->names[i] = xstrdup(builtins[i]);
        asset->widths[i] = 1;
    }
    if (!value) {
        return true;
    }
    if (!av_is_obj(value) || av_obj_size(value) > SHADER_PARAMETER_MAX - 3) {
        return fail(
            error,
            size,
            "Shader parameters must be an object with at most %d entries",
            SHADER_PARAMETER_MAX - 3
        );
    }
    static const char *const fields[] = { "type", "default" };
    for (size_t i = 0; i < av_obj_size(value); i++) {
        int slot = (int)i + 3;
        const av_value *key = av_obj_key_at(value, i);
        const char *name = av_get_str(key);
        const av_value *entry = av_obj_value_at(value, i);
        const av_value *type = av_obj_get(entry, "type");
        const char *text = string(type) ? av_get_str(type) : "";
        int width = !strcmp(text, "float") ? 1
                  : !strcmp(text, "vec2")  ? 2
                  : !strcmp(text, "vec3")  ? 3
                  : !strcmp(text, "vec4")  ? 4
                                           : 0;
        if (!string(key) || !width || !keys(entry, fields, 2) ||
            !parameter_default(av_obj_get(entry, "default"), width, asset->defaults[slot])) {
            return fail(error, size, "Shader parameter %s needs type float/vec2/vec3/vec4 and a finite matching default", name);
        }
        asset->names[slot] = xstrdup(name);
        asset->widths[slot] = width;
    }
    return true;
}
static bool load(
    const char *content_root,
    const char *descriptor_path,
    const char *descriptor_text,
    size_t *budget,
    shader_prepared_asset *result,
    char *error,
    size_t size
)
{
    char root[SOURCE_PATH_BYTES], directory[SOURCE_PATH_BYTES];
    if (!realpath(content_root, root) ||
        !realpath(descriptor_path, directory) || !within(root, directory)) {
        return fail(error, size, "Shader descriptor must resolve inside project content: %s", descriptor_path);
    }
    *strrchr(directory, '/') = 0;
    char *saved = NULL;
    if (!descriptor_text) {
        saved = read_file(descriptor_path, budget, error, size);
        descriptor_text = saved;
    } else {
        size_t bytes = strlen(descriptor_text);
        if (bytes > SOURCE_FILE_BYTES || bytes > *budget) {
            return fail(error, size, "Shader descriptor exceeds byte limits");
        }
        *budget -= bytes;
    }
    av_doc *document = descriptor_text ? av_doc_read(descriptor_text, error, size)
                                       : NULL;
    free(saved);
    if (!document) {
        return false;
    }
    shader_prepared_asset asset;
    shader_prepared_asset_init(&asset);
    asset.kind = SHADER_ASSET_PARTICLE_TEXT;
    const av_value *value = av_doc_root(document);
    const av_value *version = av_obj_get(value, "version"),
                   *name = av_obj_get(value, "name"),
                   *kind = av_obj_get(value, "kind"),
                   *source = av_obj_get(value, "source"),
                   *entry = av_obj_get(value, "entry"),
                   *update = av_obj_get(value, "update"),
                   *includes = av_obj_get(value, "includes");
    static const char *const fields[] = { "version",  "name",  "kind",
                                          "source",   "entry", "parameters",
                                          "includes", "update" };
    bool valid = false;
    if (!keys(value, fields, 8) || !av_is_int(version) || av_get_num(version) != 1 ||
        !string(name) || !av_get_len(name) || av_get_len(name) > 1024 ||
        !string(kind) || strcmp(av_get_str(kind), "particle") ||
        !string(source) || !string(entry) || (update && !string(update))) {
        fail(error, size, "%s: shader-source requires version 1, name, kind particle, source and entry", descriptor_path);
        goto done;
    }
    asset.name = xstrdup(av_get_str(name));
    asset.text.entry = xstrdup(av_get_str(entry));
    asset.text.update = update ? xstrdup(av_get_str(update)) : NULL;
    if (!parameters(av_obj_get(value, "parameters"), &asset, error, size)) {
        goto done;
    }
    char path[SOURCE_PATH_BYTES];
    if (!resolve(root, directory, av_get_str(source), path, error, size)) {
        goto done;
    }
    asset.text.path = xstrdup(path + strlen(root) + 1);
    asset.text.code = read_file(path, budget, error, size);
    if (!asset.text.code) {
        goto done;
    }
    if (includes && (!av_is_arr(includes) || av_arr_size(includes) > SOURCE_INCLUDES)) {
        fail(error, size, "Shader includes must be an array with at most %d GLSL paths", SOURCE_INCLUDES);
        goto done;
    }
    for (size_t i = 0; includes && i < av_arr_size(includes); i++) {
        const av_value *include = av_arr_get(includes, i);
        if (!string(include) ||
            !resolve(root, directory, av_get_str(include), path, error, size)) {
            if (!string(include)) {
                fail(error, size, "Shader include must be a relative GLSL path");
            }
            goto done;
        }
        shader_text_include file = { .path = xstrdup(av_get_str(include)) };
        file.code = read_file(path, budget, error, size);
        apush(asset.text.includes, file);
        if (!file.code) {
            goto done;
        }
    }
    shader_error diagnostic = { 0 };
    if (!shader_text_validate(&asset.text, asset.names, asset.widths, &diagnostic)) {
        fail(error, size, "%s: %s", descriptor_path, diagnostic.message);
        goto done;
    }
    shader_prepared_asset_free(result);
    *result = asset;
    asset = (shader_prepared_asset){ 0 };
    valid = true;
done:
    shader_prepared_asset_free(&asset);
    av_doc_free(document);
    return valid;
}
bool authoring_shader_source_load(
    const char *content_root,
    const char *descriptor_path,
    const char *descriptor_text,
    shader_prepared_asset *result,
    char *error,
    size_t size
)
{
    assert(content_root && descriptor_path && result && (!size || error));
    size_t budget = SOURCE_TOTAL_BYTES;
    return load(content_root, descriptor_path, descriptor_text, &budget, result, error, size);
}
bool authoring_shader_source_validate(const shader_prepared_asset *asset, char *error, size_t size)
{
    shader_error diagnostic = { 0 };
    if (asset->kind != SHADER_ASSET_PARTICLE_TEXT ||
        !render_shader_text_validate(&asset->text, asset->names, asset->widths, &diagnostic)) {
        return fail(
            error,
            size,
            "%s: %s",
            asset->text.path ? asset->text.path : "GLSL",
            diagnostic.message
        );
    }
    return true;
}
static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
bool authoring_shader_sources_prepare(const char *content_root, bool publish, char *error, size_t size)
{
    char directory[SOURCE_PATH_BYTES];
    if (snprintf(directory, sizeof directory, "%s/shaders", content_root) >=
        (int)sizeof directory) {
        return fail(error, size, "Shader source directory path exceeds limit");
    }
    DIR *dir = opendir(directory);
    if (!dir && errno != ENOENT) {
        return fail(error, size, "Cannot open shader source directory: %s", strerror(errno));
    }
    char **paths = NULL;
    shader_prepared_asset *assets = NULL;
    bool valid = false;
    while (dir) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) {
                fail(error, size, "Cannot read shader source directory: %s", strerror(errno));
                goto done;
            }
            break;
        }
        if (entry->d_name[0] == '.' || !suffix(entry->d_name, ".kdl")) {
            continue;
        }
        char path[SOURCE_PATH_BYTES];
        if (acount(paths) == SOURCE_DOCUMENTS ||
            snprintf(path, sizeof path, "%s/%s", directory, entry->d_name) >=
                (int)sizeof path) {
            fail(error, size, "Shader source count or path exceeds limit");
            goto done;
        }
        apush(paths, xstrdup(path));
    }
    if (acount(paths) > 1) {
        asort(paths, compare_paths);
    }
    size_t budget = SOURCE_TOTAL_BYTES;
    for (int i = 0; i < acount(paths); i++) {
        shader_prepared_asset asset;
        shader_prepared_asset_init(&asset);
        if (!load(content_root, paths[i], NULL, &budget, &asset, error, size)) {
            goto done;
        }
        apush(assets, asset);
        for (int j = 0; j < i; j++) {
            if (!strcmp(assets[j].name, asset.name)) {
                fail(error, size, "Duplicate shader-source name: %s", asset.name);
                goto done;
            }
        }
    }
    valid = authoring_shader_sources_publish(assets, acount(assets), publish, error, size);
done:
    if (dir) {
        closedir(dir);
    }
    for (int i = 0; i < acount(paths); i++) {
        free(paths[i]);
    }
    afree(paths);
    for (int i = 0; i < acount(assets); i++) {
        shader_prepared_asset_free(&assets[i]);
    }
    afree(assets);
    return valid;
}
