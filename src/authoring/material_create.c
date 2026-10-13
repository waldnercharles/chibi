#include "authoring/material_create.h"

#include "assets/new_asset.h"
#include "authoring/shader_report.h"
#include "graph/document/serialize.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

standard_material material_authoring_defaults(material_authoring_preset preset)
{
    assert(preset >= 0 && preset < MATERIAL_AUTHORING_PRESET_COUNT);
    standard_material value = { .base_color = { .5f, .5f, .5f, 1 },
                                .roughness = .7f,
                                .normal_scale = 1,
                                .occlusion_strength = 1,
                                .alpha_mode = SHADER_ALPHA_OPAQUE,
                                .alpha_cutoff = .5f };
    for (int i = 0; i < STANDARD_TEXTURE_COUNT; i++) {
        value.textures[i].scale[0] = value.textures[i].scale[1] = 1;
        value.textures[i].filter = SHADER_IMAGE_BILINEAR;
    }
    if (preset == MATERIAL_AUTHORING_METAL) {
        value.metallic = 1;
        value.roughness = .25f;
    }
    if (preset == MATERIAL_AUTHORING_EMISSIVE) {
        value.emission[0] = value.emission[1] = value.emission[2] = 1;
    }
    value.unlit = preset == MATERIAL_AUTHORING_UNLIT;
    return value;
}

static bool factor(float value)
{
    return isfinite(value) && value >= 0 && value <= 1;
}

char *material_authoring_source(const char *name, const standard_material *material, shader_error *error)
{
    *error = (shader_error){ 0 };
    if (!new_asset_name_valid(name, error->message, sizeof error->message)) {
        return NULL;
    }
    bool valid = factor(material->metallic) && factor(material->roughness) &&
                 factor(material->occlusion_strength) &&
                 factor(material->alpha_cutoff) &&
                 isfinite(material->normal_scale) && material->normal_scale >= 0 &&
                 material->alpha_mode >= SHADER_ALPHA_BLEND &&
                 material->alpha_mode <= SHADER_ALPHA_MASK;
    for (int i = 0; i < 4; i++) {
        valid &= factor(material->base_color[i]);
    }
    for (int i = 0; i < 3; i++) {
        valid &= isfinite(material->emission[i]) && material->emission[i] >= 0;
    }
    for (int i = 0; i < STANDARD_TEXTURE_COUNT; i++) {
        const standard_texture *texture = &material->textures[i];
        valid &= texture->filter >= SHADER_IMAGE_NEAREST &&
                 texture->filter <= SHADER_IMAGE_BILINEAR &&
                 texture->wrap >= SHADER_IMAGE_REPEAT &&
                 texture->wrap <= SHADER_IMAGE_CLAMP && isfinite(texture->rotation);
        for (int j = 0; j < 2; j++) {
            valid &= isfinite(texture->offset[j]) && isfinite(texture->scale[j]);
        }
        if (texture->image) {
            valid &= !strncmp(texture->image, "content/", 8) &&
                     !strstr(texture->image, "..") &&
                     !strchr(texture->image, '\\') && texture->image[8];
        }
    }
    if (!valid) {
        snprintf(error->message, sizeof error->message, "Use finite material factors in [0,1], nonnegative emission/normal scale and content-relative image keys.");
        return NULL;
    }
    behavior_asset *asset = standard_material_build(name, material);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    char *text = shader_authoring_inspect(asset, &report, error)
                   ? behavior_asset_save(asset)
                   : NULL;
    shader_authoring_report_free(&report);
    behavior_asset_free(asset);
    return text;
}

enum
{
    DECLARATION_BYTES_MAX = 16 * 1024 * 1024,
    DECLARATION_SCAN_BYTES_MAX = 64 * 1024 * 1024,
    DECLARATION_SCAN_ENTRIES_MAX = 4096,
};
typedef struct
{
    size_t bytes;
    int entries;
} declaration_scan;

static char *read_declaration(int directory, const char *file, declaration_scan *scan, char *error, size_t size)
{
    int fd = openat(directory, file, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        snprintf(error, size, "Cannot open shader declaration %s: %s", file, strerror(errno));
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_size > DECLARATION_BYTES_MAX) {
        snprintf(error, size, "Shader declaration %s must be a readable regular file of at most 16 MiB", file);
        close(fd);
        errno = EINVAL;
        return NULL;
    }
    size_t length = (size_t)info.st_size;
    if (length > DECLARATION_SCAN_BYTES_MAX - scan->bytes) {
        snprintf(error, size, "Shader declaration scan exceeds 64 MiB");
        close(fd);
        errno = EFBIG;
        return NULL;
    }
    scan->bytes += length;
    char *text = xmalloc(length + 1);
    size_t used = 0;
    while (used < length) {
        ssize_t count = read(fd, text + used, length - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        used += (size_t)count;
    }
    char extra;
    ssize_t count;
    do {
        count = read(fd, &extra, 1);
    } while (count < 0 && errno == EINTR);
    bool valid = used == length && count == 0;
    valid = close(fd) == 0 && valid;
    if (!valid) {
        snprintf(error, size, "Cannot read stable shader declaration %s", file);
        free(text);
        errno = EIO;
        return NULL;
    }
    text[length] = 0;
    return text;
}

/* Read declaration names as bounded data; no graph registry/resource loading. */
static bool namespace_available(
    const char *folder,
    const char *name,
    declaration_scan *scan,
    char *error,
    size_t size
)
{
    int fd = open(folder, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) {
            return true;
        }
        snprintf(error, size, "Cannot inspect %s: %s", folder, strerror(errno));
        return false;
    }
    DIR *directory = fdopendir(fd);
    if (!directory) {
        snprintf(error, size, "Cannot inspect %s: %s", folder, strerror(errno));
        close(fd);
        return false;
    }
    bool available = true;
    while (available) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno) {
                snprintf(error, size, "Cannot scan %s: %s", folder, strerror(errno));
                available = false;
            }
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        if (scan->entries == DECLARATION_SCAN_ENTRIES_MAX) {
            snprintf(error, size, "Shader declaration scan exceeds %d directory entries", DECLARATION_SCAN_ENTRIES_MAX);
            errno = EFBIG;
            available = false;
            break;
        }
        scan->entries++;
        size_t length = strlen(entry->d_name);
        if (length < 4 || strcmp(entry->d_name + length - 4, ".kdl")) {
            continue;
        }
        size_t bytes = strlen(folder) + length + 2;
        char *path = xmalloc(bytes);
        snprintf(path, bytes, "%s/%s", folder, entry->d_name);
        char *text = read_declaration(dirfd(directory), entry->d_name, scan, error, size);
        if (!text) {
            free(path);
            available = false;
            break;
        }
        av_doc *document = av_doc_read(text, error, size);
        const char *declared = document
                                 ? av_get_str(av_obj_get(av_doc_root(document), "name"))
                                 : NULL;
        bool collision = declared && !strcmp(declared, name);
        if (!document || collision) {
            snprintf(
                error,
                size,
                collision ? "Shader declaration %s already exists in %s"
                          : "Cannot inspect shader declaration %s in %s",
                name,
                path
            );
            available = false;
            errno = collision ? EEXIST : EINVAL;
        }
        av_doc_free(document);
        free(text);
        free(path);
    }
    if (closedir(directory) != 0 && available) {
        snprintf(error, size, "Cannot finish scanning %s: %s", folder, strerror(errno));
        available = false;
    }
    return available;
}

char *material_authoring_create(
    const char *content,
    const char *name,
    const standard_material *material,
    char *error,
    size_t size
)
{
    shader_error diagnostic;
    char *source = material_authoring_source(name, material, &diagnostic);
    if (!source) {
        snprintf(error, size, "%s", diagnostic.message);
        errno = EINVAL;
        return NULL;
    }
    size_t bytes = strlen(content) + strlen(name) + 32;
    char *path = xmalloc(bytes);
    const char *folders[] = { "materials", "shapes", "models" };
    declaration_scan scan = { 0 };
    bool valid = new_asset_real_folder(content, error, size);
    for (int i = 0; valid && i < 3; i++) {
        snprintf(path, bytes, "%s/%s", content, folders[i]);
        struct stat info;
        if (lstat(path, &info) == 0) {
            valid = new_asset_real_folder(path, error, size);
        } else if (errno != ENOENT) {
            snprintf(error, size, "Cannot inspect %s: %s", path, strerror(errno));
            valid = false;
        }
        if (valid) {
            valid = namespace_available(path, name, &scan, error, size);
        }
    }
    snprintf(path, bytes, "%s/materials", content);
    if (valid && mkdir(path, 0755) != 0 && errno != EEXIST) {
        snprintf(error, size, "Cannot create material folder: %s", strerror(errno));
        valid = false;
    }
    snprintf(path, bytes, "%s/materials/%s.kdl", content, name);
    FILE *file = valid ? fopen(path, "wbx") : NULL;
    if (valid && !file) {
        snprintf(error, size, "Cannot create %s: %s", path, strerror(errno));
    }
    if (file) {
        size_t length = strlen(source);
        valid = fwrite(source, 1, length, file) == length;
        valid = fclose(file) == 0 && valid;
        if (!valid) {
            snprintf(error, size, "Cannot finish writing %s", path);
            unlink(path);
        }
    } else {
        valid = false;
    }
    free(source);
    if (!valid) {
        free(path);
        return NULL;
    }
    return path;
}
