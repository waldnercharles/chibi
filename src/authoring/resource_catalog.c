#include "authoring/resource_catalog.h"

#include "authoring/image_prepare.h"
#include "runtime/audio.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* A bounded, sorted snapshot: no symlinks, resident catalogs or GPU calls. */
typedef struct
{
    char *key;
    size_t bytes;
    const char *kind;
    av_value *record;
} resource_file;
typedef struct
{
    av_doc *doc;
    av_value *root, *records;
    resource_file *files;
    size_t remaining;
    uint64_t fingerprint, resource_fingerprint;
    int entries, occurrences;
    bool complete;
} inventory;
static const char *extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot : "";
}
static const char *kind(const char *path)
{
    const char *ext = extension(path);
    if (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".jpg") ||
        !strcasecmp(ext, ".jpeg")) {
        return "image";
    }
    if (!strcasecmp(ext, ".mesh")) {
        return "mesh";
    }
    if (!strcasecmp(ext, ".wav") || !strcasecmp(ext, ".ogg")) {
        return "sound";
    }
    if (!strcasecmp(ext, ".hdr")) {
        return "sky";
    }
    if (!strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".otf")) {
        return "font";
    }
    return NULL;
}
static bool source(const char *path)
{
    const char *ext = extension(path);
    return !strcmp(ext, ".kdl") || !strcmp(ext, ".chibi") ||
           !strcmp(ext, ".rml") || !strcmp(ext, ".rcss");
}
static char *join(const char *root, const char *key)
{
    size_t size = strlen(root) + strlen(key) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", root, key);
    return path;
}
static int compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static void walk(inventory *in, const char *content, const char *relative, int depth)
{
    char *path = join(content, relative);
    DIR *dir = opendir(path);
    free(path);
    if (!dir) {
        in->complete = false;
        return;
    }
    char **names = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (++in->entries > AUTHORING_RESOURCE_FILES_MAX) {
            in->complete = false;
            break;
        }
        apush(names, xstrdup(entry->d_name));
    }
    closedir(dir);
    if (acount(names) > 1) {
        asort(names, compare);
    }
    for (int i = 0; i < acount(names); i++) {
        char *relative_file = relative[0] ? join(relative, names[i])
                                          : xstrdup(names[i]);
        char *file = join(content, relative_file);
        struct stat info;
        if (lstat(file, &info)) {
            in->complete = false;
        } else if (S_ISDIR(info.st_mode)) {
            if (depth < AUTHORING_RESOURCE_DEPTH_MAX &&
                in->entries <= AUTHORING_RESOURCE_FILES_MAX) {
                walk(in, content, relative_file, depth + 1);
            } else {
                in->complete = false;
            }
        } else if (S_ISREG(info.st_mode) && (kind(file) || source(file))) {
            char *key = join("content", relative_file);
            apush(
                in->files,
                ((resource_file){ .key = key,
                                  .bytes = (size_t)info.st_size,
                                  .kind = kind(file) })
            );
        } else if (S_ISLNK(info.st_mode)) {
            in->complete = false;
        }
        free(file);
        free(relative_file);
        free(names[i]);
    }
    afree(names);
}
static int compare_files(const void *a, const void *b)
{
    return strcmp(((const resource_file *)a)->key, ((const resource_file *)b)->key);
}
static void status(inventory *in, av_value *record, const char *value, const char *message)
{
    av_obj_set(in->doc, record, "status", av_str(in->doc, value));
    if (message && message[0]) {
        av_obj_set(in->doc, record, "message", av_str(in->doc, message));
    }
}
static unsigned char *read_file(inventory *in, const char *content, resource_file *file)
{
    if (file->bytes > AUTHORING_RESOURCE_FILE_BYTES_MAX || file->bytes > in->remaining) {
        in->complete = false;
        if (file->record) {
            status(in, file->record, "limit_exceeded", "File or total inspection byte limit exceeded");
        }
        return NULL;
    }
    char *path = join(content, file->key + 8);
    FILE *handle = fopen(path, "rb");
    free(path);
    unsigned char *bytes = xmalloc(file->bytes + 1);
    bool valid = handle && fread(bytes, 1, file->bytes, handle) == file->bytes &&
                 fgetc(handle) == EOF && !ferror(handle);
    if (handle && fclose(handle)) {
        valid = false;
    }
    in->remaining -= file->bytes;
    if (!valid) {
        free(bytes);
        in->complete = false;
        if (file->record) {
            status(in, file->record, "failed", "File unreadable or changed during inspection");
        }
        return NULL;
    }
    bytes[file->bytes] = 0;
    return bytes;
}
static uint64_t hash_extend(uint64_t hash, const unsigned char *bytes, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
static unsigned be16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static void payload(inventory *in, av_value *record, size_t bytes, const char *basis)
{
    av_value *cost = av_obj_get(record, "costs");
    av_obj_add_uint(in->doc, cost, "cpu_payload_bytes", bytes);
    av_obj_add_str(in->doc, cost, "basis", basis);
}
static void dimensions(inventory *in, av_value *meta, int width, int height)
{
    av_obj_add_int(in->doc, meta, "width", width);
    av_obj_add_int(in->doc, meta, "height", height);
}
static bool image_dimensions(const unsigned char *p, size_t n, int *width, int *height)
{
    if (n >= 24 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8)) {
        uint32_t w = be32(p + 16), h = be32(p + 20);
        if (w > RENDER_IMAGE_SIDE_MAX || h > RENDER_IMAGE_SIDE_MAX) {
            return false;
        }
        *width = (int)w;
        *height = (int)h;
        return w && h;
    }
    if (n < 4 || p[0] != 255 || p[1] != 216) {
        return false;
    }
    for (size_t i = 2; i + 4 <= n;) {
        if (p[i++] != 255) {
            return false;
        }
        while (i < n && p[i] == 255) {
            i++;
        }
        if (i >= n) {
            return false;
        }
        unsigned marker = p[i++];
        if (marker == 217 || marker == 218 || n - i < 2) {
            return false;
        }
        unsigned length = be16(p + i);
        if (length < 2 || length > n - i) {
            return false;
        }
        if ((marker >= 192 && marker <= 195) || (marker >= 197 && marker <= 199) ||
            (marker >= 201 && marker <= 203) || (marker >= 205 && marker <= 207)) {
            if (length < 8) {
                return false;
            }
            *height = (int)be16(p + i + 3);
            *width = (int)be16(p + i + 5);
            return *width > 0 && *height > 0 && *width <= RENDER_IMAGE_SIDE_MAX &&
                 *height <= RENDER_IMAGE_SIDE_MAX;
        }
        i += length;
    }
    return false;
}
static void inspect(inventory *in, resource_file *file, const unsigned char *bytes)
{
    av_value *record = file->record, *meta = av_obj_get(record, "metadata");
    char error[256] = { 0 };
    status(in, record, "metadata", NULL);
    if (!strcmp(file->kind, "image")) {
        int width = 0, height = 0;
        if (!image_dimensions(bytes, file->bytes, &width, &height)) {
            status(in, record, "failed", "Invalid or unsupported image dimensions/header");
            return;
        }
        dimensions(in, meta, width, height);
        payload(in, record, (size_t)width * (size_t)height * 4, "RGBA8 pixel payload; header estimate, excludes decoder, catalog and GPU allocations");
        av_obj_add_str(in->doc, meta, "validation", "header-only; pixel decode not performed");
        return;
    }
    if (!strcmp(file->kind, "mesh")) {
        render_mesh_asset mesh = { 0 };
        size_t remaining = AUTHORING_RESOURCE_TOTAL_BYTES_MAX;
        if (!render_mesh_asset_prepared_read_budget(bytes, file->bytes, &remaining, &mesh, error, sizeof error)) {
            status(in, record, "failed", error);
            return;
        }
        av_obj_add_int(in->doc, meta, "vertices", acount(mesh.vertices));
        av_obj_add_int(in->doc, meta, "triangles", acount(mesh.indices) / 3);
        av_obj_add_int(in->doc, meta, "groups", acount(mesh.groups));
        av_value *bounds = av_obj_add_arr(in->doc, meta, "bounds");
        float values[] = { mesh.min.x, mesh.min.y, mesh.min.z,
                           mesh.max.x, mesh.max.y, mesh.max.z };
        for (int i = 0; i < 6; i++) {
            av_arr_add_val(bounds, av_float(in->doc, values[i]));
        }
        payload(in, record, AUTHORING_RESOURCE_TOTAL_BYTES_MAX - remaining, "Prepared mesh reader budget charge; excludes allocator and GPU overhead");
        status(in, record, "decoded", NULL);
        render_mesh_asset_free(&mesh);
        return;
    }
    if (!strcmp(file->kind, "sky")) {
        render_sky sky = { 0 };
        if (!image_prepare_sky_decode(bytes, file->bytes, &sky, error, sizeof error)) {
            status(in, record, "failed", error);
            return;
        }
        dimensions(in, meta, sky.width, sky.height);
        payload(in, record, (size_t)sky.width * (size_t)sky.height * 4, "RGBE texels; excludes derived cube maps, prefilter mips and GPU overhead");
        status(in, record, "decoded", NULL);
        render_sky_free(&sky);
        return;
    }
    if (!strcmp(file->kind, "sound")) {
        sound_format format = sound_format_of(bytes, file->bytes);
        bool expected = !strcasecmp(extension(file->key), ".ogg")
                          ? format == SOUND_OGG
                          : format == SOUND_WAV;
        if (!expected) {
            status(in, record, "failed", "Not a supported mono/stereo 44.1 kHz WAV or Ogg Vorbis stream");
            return;
        }
        av_obj_add_int(in->doc, meta, "sample_rate", 44100);
        av_obj_add_str(in->doc, meta, "format", format == SOUND_WAV ? "wav" : "ogg-vorbis");
        if (format == SOUND_OGG) {
            av_obj_add_int(in->doc, meta, "channels", bytes[39]);
            av_obj_add_str(in->doc, meta, "validation", "container/header only; audio packets not decoded");
            return;
        }
        int channels = 0, bits = 0;
        for (size_t at = 12; at + 8 <= file->bytes;) {
            size_t length = le32(bytes + at + 4);
            if (length > file->bytes - at - 8) {
                break;
            }
            if (!memcmp(bytes + at, "fmt ", 4) && length >= 16) {
                channels = bytes[at + 10] | (int)bytes[at + 11] << 8;
                bits = bytes[at + 22] | (int)bytes[at + 23] << 8;
                av_obj_add_int(in->doc, meta, "channels", channels);
            }
            if (!memcmp(bytes + at, "data", 4) && channels && bits) {
                size_t frames = length / ((size_t)channels * (size_t)bits / 8);
                av_obj_add_uint(in->doc, meta, "frames", frames);
                payload(in, record, frames * (size_t)channels * sizeof(float), "Float32 decoded sample estimate; excludes voice buffers and mixer allocations");
                break;
            }
            at += 8 + length + (length & 1);
        }
        return;
    }
    if (file->bytes < 12 ||
        (be32(bytes) != UINT32_C(0x00010000) && memcmp(bytes, "OTTO", 4))) {
        status(in, record, "failed", "Not a TrueType or OpenType sfnt header");
        return;
    }
    av_obj_add_str(in->doc, meta, "validation", "sfnt header only; font tables and glyphs not validated");
    payload(in, record, file->bytes, "Font file bytes; excludes font engine allocations and glyph atlases");
}
static av_value *record(inventory *in, const char *key, const char *type)
{
    av_value *value = av_arr_add_obj(in->doc, in->records);
    av_obj_add_str(in->doc, value, "kind", type);
    av_obj_add_str(in->doc, value, "key", key);
    av_obj_add_str(in->doc, value, "path", key);
    av_obj_add_obj(in->doc, value, "metadata");
    av_value *cost = av_obj_add_obj(in->doc, value, "costs");
    av_obj_add_null(in->doc, cost, "gpu_payload_bytes");
    av_obj_add_arr(in->doc, value, "consumers");
    bool sound_directory = !strcmp(type, "sound") &&
                           !strncmp(key, "content/sounds/", 15) &&
                           !strchr(key + 15, '/');
    const char
        *policy = sound_directory
                    ? "All immediate supported files in content/sounds are "
                      "prepared, including files without static consumers"
                    : "Inclusion depends on actual project preparation; this "
                      "inventory does not prove shipping or non-use";
    av_obj_add_str(in->doc, value, "inclusion_policy", policy);
    return value;
}
static av_value *find_record(inventory *in, const char *key)
{
    for (size_t i = 0; i < av_arr_size(in->records); i++) {
        av_value *value = av_arr_get(in->records, i);
        if (!strcmp(av_get_str(av_obj_get(value, "key")), key)) {
            return value;
        }
    }
    return NULL;
}
/* Text occurrences, deliberately not a substitute for the semantic reference index. */
static void consumers(inventory *in, const resource_file *file, const unsigned char *bytes)
{
    size_t counted = 0;
    int line = 1, column = 1;
    for (size_t at = 0; at < file->bytes; at++) {
        if (bytes[at] != '"' && bytes[at] != '\'') {
            continue;
        }
        unsigned char quote = bytes[at];
        size_t start = ++at;
        bool escaped = false;
        for (; at < file->bytes && bytes[at] != quote; at++) {
            if (bytes[at] == '\\') {
                escaped = true;
                if (at + 1 < file->bytes) {
                    at++;
                }
            }
        }
        if (at == file->bytes || escaped || at - start < 9 ||
            memcmp(bytes + start, "content/", 8)) {
            continue;
        }
        size_t length = at - start;
        if (length >= 8 && !memcmp(bytes + at - 8, "#nearest", 8)) {
            length -= 8;
        }
        char *key = xmalloc(length + 1);
        memcpy(key, bytes + start, length);
        key[length] = 0;
        const char *type = kind(key);
        if (!type || strstr(key, "..") || strchr(key, '\\') ||
            memchr(key, '\n', length) || memchr(key, 0, length)) {
            free(key);
            continue;
        }
        if (in->occurrences == AUTHORING_RESOURCE_OCCURRENCES_MAX) {
            in->complete = false;
            free(key);
            return;
        }
        in->occurrences++;
        av_value *resource = find_record(in, key);
        if (!resource) {
            resource = record(in, key, type);
            status(in, resource, in->complete ? "missing" : "not_inspected", "Literal path has no inventoried file; consult catalog completeness");
        }
        av_value *use = av_arr_add_obj(in->doc, av_obj_get(resource, "consumers"));
        av_obj_add_str(in->doc, use, "path", file->key);
        av_obj_add_str(in->doc, use, "role", "quoted-path-occurrence");
        av_obj_add_uint(in->doc, use, "start", start);
        av_obj_add_uint(in->doc, use, "end", start + length);
        for (; counted < start; counted++) {
            if (bytes[counted] == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }
        av_obj_add_int(in->doc, use, "line", line);
        av_obj_add_int(in->doc, use, "column", column);
        free(key);
    }
}
static int compare_records(const void *a, const void *b)
{
    return strcmp(
        av_get_str(av_obj_get(*(av_value *const *)a, "key")),
        av_get_str(av_obj_get(*(av_value *const *)b, "key"))
    );
}
av_doc *authoring_resource_catalog(const char *content, char *error, size_t size)
{
    assert(content && (!size || error));
    if (size) {
        error[0] = 0;
    }
    DIR *directory = opendir(content);
    if (!directory) {
        if (size) {
            snprintf(error, size, "Cannot inspect content directory: %s", strerror(errno));
        }
        return NULL;
    }
    closedir(directory);
    inventory in = { .doc = av_doc_new(),
                     .remaining = AUTHORING_RESOURCE_TOTAL_BYTES_MAX,
                     .complete = true,
                     .fingerprint = UINT64_C(14695981039346656037),
                     .resource_fingerprint = UINT64_C(14695981039346656037) };
    in.root = av_obj(in.doc);
    av_doc_set_root(in.doc, in.root);
    av_obj_add_int(in.doc, in.root, "version", 1);
    av_obj_add_str(in.doc, in.root, "hash_algorithm", "fnv1a64");
    av_obj_add_str(in.doc, in.root, "coverage", "Regular PNG/JPEG, mesh, HDR, WAV/OGG, TTF/OTF files. Quoted content paths in KDL/.chibi/RML/RCSS include comments; dynamic names, relative URLs, escaped literals and bare sound names are not resolved. Hidden files and symlinks are not inspected. No GPU allocations measured.");
    in.records = av_obj_add_arr(in.doc, in.root, "resources");
    av_value *authored = av_obj_add_arr(in.doc, in.root, "authored_sources");
    walk(&in, content, "", 0);
    if (acount(in.files) > 1) {
        asort(in.files, compare_files);
    }
    for (int i = 0; i < acount(in.files); i++) {
        resource_file *file = &in.files[i];
        if (!file->kind) {
            continue;
        }
        file->record = record(&in, file->key, file->kind);
        av_obj_add_uint(in.doc, file->record, "bytes", file->bytes);
        unsigned char *bytes = read_file(&in, content, file);
        if (bytes) {
            char hash[17];
            snprintf(
                hash,
                sizeof hash,
                "%016" PRIx64,
                hash_extend(UINT64_C(14695981039346656037), bytes, file->bytes)
            );
            av_obj_add_str(in.doc, file->record, "content_hash", hash);
            in.fingerprint = hash_extend(
                in.fingerprint,
                (const unsigned char *)file->key,
                strlen(file->key) + 1
            );
            in.fingerprint = hash_extend(in.fingerprint, (const unsigned char *)hash, sizeof hash);
            in.resource_fingerprint = in.fingerprint;
            inspect(&in, file, bytes);
            free(bytes);
        }
    }
    for (int i = 0; i < acount(in.files); i++) {
        resource_file *file = &in.files[i];
        if (source(file->key)) {
            file->record = av_arr_add_obj(in.doc, authored);
            av_obj_add_str(in.doc, file->record, "path", file->key);
            av_obj_add_uint(in.doc, file->record, "bytes", file->bytes);
            unsigned char *bytes = read_file(&in, content, file);
            if (bytes) {
                char hash[17];
                snprintf(
                    hash,
                    sizeof hash,
                    "%016" PRIx64,
                    hash_extend(UINT64_C(14695981039346656037), bytes, file->bytes)
                );
                av_obj_add_str(in.doc, file->record, "content_hash", hash);
                status(&in, file->record, "inspected", NULL);
                in.fingerprint = hash_extend(
                    in.fingerprint,
                    (const unsigned char *)file->key,
                    strlen(file->key) + 1
                );
                in.fingerprint = hash_extend(in.fingerprint, (const unsigned char *)hash, sizeof hash);
                consumers(&in, file, bytes);
                free(bytes);
            }
        }
        free(file->key);
    }
    afree(in.files);
    if (!in.complete) {
        for (size_t i = 0; i < av_arr_size(in.records); i++) {
            av_value *item = av_arr_get(in.records, i);
            const char *state = av_get_str(av_obj_get(item, "status"));
            if (state && !strcmp(state, "missing")) {
                status(&in, item, "not_inspected", "Inventory incomplete; absence has not been established");
            }
        }
    }
    av_obj_add_bool(in.doc, in.root, "complete", in.complete);
    if (in.complete) {
        char hash[17];
        snprintf(hash, sizeof hash, "%016" PRIx64, in.fingerprint);
        av_obj_add_str(in.doc, in.root, "content_hash", hash);
        snprintf(hash, sizeof hash, "%016" PRIx64, in.resource_fingerprint);
        av_obj_add_str(in.doc, in.root, "resource_hash", hash);
    }
    av_obj_add_str(in.doc, in.root, "content_hash_scope", "Sorted raw resource keys and byte fingerprints, followed by sorted authored source paths and byte fingerprints; absent when inventory incomplete");
    av_obj_add_str(in.doc, in.root, "resource_hash_scope", "Sorted raw resource keys and byte fingerprints only; absent when inventory incomplete");
    av_value **ordered = NULL;
    for (size_t i = 0; i < av_arr_size(in.records); i++) {
        apush(ordered, av_arr_get(in.records, i));
    }
    if (acount(ordered) > 1) {
        asort(ordered, compare_records);
    }
    av_value *sorted = av_arr(in.doc);
    while (av_arr_size(in.records)) {
        av_arr_remove(in.records, 0);
    }
    for (int i = 0; i < acount(ordered); i++) {
        av_arr_add_val(sorted, ordered[i]);
    }
    afree(ordered);
    av_obj_set(in.doc, in.root, "resources", sorted);
    av_obj_add_str(in.doc, in.root, "ordering", "Resources by key; authored sources by path; consumers by source path and byte offset");
    return in.doc;
}
