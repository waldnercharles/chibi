#include "authoring/image_import_commit.h"

#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void image_import_file_plan_free(image_import_file_plan *plan)
{
    free(plan->source);
    free(plan->destination);
    image_import_free(&plan->image);
    *plan = (image_import_file_plan){ 0 };
}

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}

static uint64_t hash_number(uint64_t hash, uint64_t number)
{
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (number & 255)) * UINT64_C(1099511628211);
        number >>= 8;
    }
    return hash;
}
static uint64_t hash_text(uint64_t hash, const char *text)
{
    hash = hash_number(hash, strlen(text));
    for (; *text; text++) {
        hash = (hash ^ (unsigned char)*text) * UINT64_C(1099511628211);
    }
    return hash;
}
static uint64_t fingerprint(const void *data, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static const char *semantics[] = { "color", "scalar", "normal" };

static bool texture_valid(const image_import_texture *texture, char *error, size_t size)
{
    if (!texture->source || !texture->source[0] || strlen(texture->source) > 4096 ||
        (texture->options.max_side != 0 && texture->options.max_side != 1024 &&
         texture->options.max_side != 2048) ||
        texture->options.semantic < IMAGE_IMPORT_COLOR ||
        texture->options.semantic > IMAGE_IMPORT_NORMAL) {
        return fail(error, size, "Texture requires a source path, max-side 0/1024/2048, and color/scalar/normal semantic");
    }
    for (const char *p = texture->source; *p; p++) {
        if ((unsigned char)*p < 32 || *p == '\\') {
            return fail(error, size, "Texture source contains unsupported path characters");
        }
    }
    if (texture->source[0] != '/') {
        for (const char *p = texture->source; *p;) {
            size_t n = strcspn(p, "/");
            if (!n || (n == 1 && *p == '.') || (n == 2 && !strncmp(p, "..", 2))) {
                return fail(error, size, "Relative texture sources must stay within the project root");
            }
            p += n;
            if (*p && !*++p) {
                return fail(error, size, "Texture source must name a file");
            }
        }
    }
    return true;
}

void image_import_texture_free(image_import_texture *texture)
{
    free(texture->source);
    *texture = (image_import_texture){ 0 };
}

bool image_import_texture_parse(const char *text, image_import_texture *texture, char *error, size_t size)
{
    assert(texture && !texture->source);
    if (!text || strlen(text) > 16384) {
        return fail(error, size, "Texture metadata exceeds 16 KiB");
    }
    av_doc *doc = av_doc_read(text, error, size);
    if (!doc) {
        return false;
    }
    const av_value *root = av_doc_root(doc);
    const av_value *version = av_obj_get(root, "texture-version");
    const av_value *source = av_obj_get(root, "source");
    const av_value *maximum = av_obj_get(root, "max-side");
    const av_value *semantic = av_obj_get(root, "semantic");
    bool valid = av_is_obj(root) && av_obj_size(root) == 4 && av_is_int(version) &&
                 av_get_num(version) == 1 && av_is_str(source) &&
                 av_get_len(source) == strlen(av_get_str(source)) &&
                 av_is_int(maximum) &&
                 (av_get_num(maximum) == 0 || av_get_num(maximum) == 1024 ||
                  av_get_num(maximum) == 2048) &&
                 av_is_str(semantic);
    image_import_texture candidate = { 0 };
    if (valid) {
        candidate.source = xstrdup(av_get_str(source));
        candidate.options.max_side = av_get_int(maximum);
        candidate.options.semantic = -1;
        for (int i = 0; i < 3; i++) {
            if (!strcmp(av_get_str(semantic), semantics[i]) &&
                av_get_len(semantic) == strlen(semantics[i])) {
                candidate.options.semantic = i;
            }
        }
        valid = texture_valid(&candidate, error, size);
    } else {
        fail(error, size, "Expected texture-version 1, source, max-side and semantic texture metadata fields");
    }
    av_doc_free(doc);
    if (!valid) {
        image_import_texture_free(&candidate);
        return false;
    }
    candidate.hash = fingerprint(text, strlen(text));
    *texture = candidate;
    return true;
}

char *image_import_texture_serialize(const image_import_texture *texture, char *error, size_t size)
{
    if (!texture_valid(texture, error, size)) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "texture-version", 1);
    av_obj_add_str(doc, root, "source", texture->source);
    av_obj_add_int(doc, root, "max-side", texture->options.max_side);
    av_obj_add_str(doc, root, "semantic", semantics[texture->options.semantic]);
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

#ifdef _WIN32
bool image_import_file_prepare(
    const char *content,
    const char *source,
    const char *key,
    image_import_file_options options,
    image_import_budget_host host,
    image_import_file_plan *plan,
    char *error,
    size_t size
)
{
    (void)content;
    (void)source;
    (void)key;
    (void)options;
    (void)host;
    (void)plan;
    return fail(error, size, "Secure image file import is not supported on Windows; pure image planning remains available");
}

bool image_import_file_commit(
    const char *content,
    const image_import_file_plan *plan,
    image_import_budget_host host,
    char *error,
    size_t size
)
{
    (void)content;
    (void)plan;
    (void)host;
    return fail(error, size, "Secure image file publication is not supported on Windows");
}
bool image_import_texture_read(
    const char *content,
    const char *key,
    image_import_texture *texture,
    char *error,
    size_t size
)
{
    (void)content;
    (void)key;
    (void)texture;
    return fail(error, size, "Texture files are not supported on Windows");
}
bool image_import_texture_save(
    const char *content,
    const char *key,
    const image_import_texture *texture,
    uint64_t expected_hash,
    char *error,
    size_t size
)
{
    (void)content;
    (void)key;
    (void)texture;
    (void)expected_hash;
    return fail(error, size, "Texture files are not supported on Windows");
}
bool image_import_file_reimport_prepare(
    const char *content,
    const char *key,
    image_import_budget_host host,
    image_import_file_plan *plan,
    char *error,
    size_t size
)
{
    (void)content;
    (void)key;
    (void)host;
    (void)plan;
    return fail(error, size, "Texture files are not supported on Windows");
}
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define IMAGE_FILE_BYTES_MAX (64u * 1024u * 1024u)
#define IMAGE_DESTINATION_MAX 4096
#define IMAGE_TEMP_ATTEMPTS 32

typedef struct
{
    int parent;
    char *name;
} created_directory;
typedef struct
{
    int parent;
    char *name;
    created_directory *created;
    bool missing;
} destination;

static void destination_close(destination *target, bool rollback)
{
    if (target->parent >= 0) {
        close(target->parent);
    }
    free(target->name);
    for (int i = acount(target->created) - 1; i >= 0; i--) {
        created_directory *one = &target->created[i];
        if (rollback) {
            unlinkat(one->parent, one->name, AT_REMOVEDIR);
        }
        close(one->parent);
        free(one->name);
    }
    afree(target->created);
    *target = (destination){ .parent = -1 };
}

static bool key_valid(const char *key)
{
    if (!key || strncmp(key, "content/", 8) || strlen(key) > IMAGE_DESTINATION_MAX) {
        return false;
    }
    size_t length = strlen(key);
    if (length < 13 || strcmp(key + length - 4, ".png")) {
        return false;
    }
    for (const char *part = key + 8; *part;) {
        size_t count = strcspn(part, "/");
        if (!count || count > 255 || (count == 1 && part[0] == '.') ||
            (count == 2 && !strncmp(part, "..", 2))) {
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            if ((unsigned char)part[i] < 32 || part[i] == '\\') {
                return false;
            }
        }
        part += count;
        if (*part && !*++part) {
            return false;
        }
    }
    const char *name = strrchr(key, '/') + 1;
    return strlen(name) + sizeof IMAGE_IMPORT_TEXTURE_SUFFIX - 1 <= 255;
}

static bool destination_open(
    const char *content,
    const char *key,
    bool create,
    bool existing,
    destination *target,
    char *error,
    size_t size
)
{
    *target = (destination){ .parent = -1 };
    if (!key_valid(key)) {
        return fail(error, size, "Destination must be a content-relative .png path without traversal");
    }
    target->parent = open(content, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (target->parent < 0) {
        return fail(error, size, "Cannot open project content directory without following symlinks");
    }
    char *relative = xstrdup(key + 8);
    char *part = relative;
    bool missing = false, valid = true;
    for (char *slash; (slash = strchr(part, '/'));) {
        *slash = 0;
        if (!missing) {
            int next = openat(target->parent, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0 && errno == ENOENT && create) {
                int parent = dup(target->parent);
                if (parent < 0) {
                    valid = fail(error, size, "Cannot retain image destination directory for rollback");
                    break;
                }
                if (mkdirat(target->parent, part, 0700) == 0) {
                    apush(target->created, ((created_directory){ parent, xstrdup(part) }));
                } else {
                    close(parent);
                }
                next = openat(target->parent, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            }
            if (next < 0 && errno == ENOENT && !create) {
                missing = true;
            } else if (next < 0) {
                valid = fail(error, size, "Destination directory is unavailable, not a directory, or a symlink");
                break;
            } else {
                close(target->parent);
                target->parent = next;
            }
        }
        part = slash + 1;
    }
    target->name = xstrdup(part);
    target->missing = missing;
    struct stat info;
    if (valid && !missing) {
        int found = fstatat(target->parent, target->name, &info, AT_SYMLINK_NOFOLLOW);
        if (found == 0 && (!existing || !S_ISREG(info.st_mode))) {
            valid = fail(
                error,
                size,
                existing ? "Texture image must be a regular file, not a symlink" : "Destination already exists; imports never overwrite assets"
            );
        } else if (found < 0 && (existing || errno != ENOENT)) {
            valid = fail(error, size, "Cannot inspect image import destination");
        }
    } else if (valid && existing) {
        valid = fail(error, size, "Texture image does not exist");
    }
    free(relative);
    if (!valid) {
        destination_close(target, true);
    }
    return valid;
}

static unsigned char *source_read(const char *path, size_t *length, char *error, size_t size)
{
    if (!path || path[0] != '/') {
        fail(error, size, "Image source must be an absolute file path");
        return NULL;
    }
    int file = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat info;
    if (file < 0 || fstat(file, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || (uint64_t)info.st_size > IMAGE_FILE_BYTES_MAX) {
        if (file >= 0) {
            close(file);
        }
        fail(error, size, "Image source must be a readable regular file of 1 to 64 MiB");
        return NULL;
    }
    *length = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(*length);
    size_t at = 0;
    while (at < *length) {
        ssize_t count = read(file, bytes + at, *length - at);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        at += (size_t)count;
    }
    unsigned char extra;
    bool valid = at == *length && read(file, &extra, 1) == 0;
    valid = close(file) == 0 && valid;
    if (!valid) {
        free(bytes);
        fail(error, size, "Image source changed or failed while being read");
        return NULL;
    }
    return bytes;
}

static uint64_t plan_hash(const char *content, const image_import_file_plan *plan)
{
    uint64_t hash = hash_number(UINT64_C(14695981039346656037), 1);
    hash = hash_text(hash, content);
    hash = hash_text(hash, plan->source);
    hash = hash_text(hash, plan->destination);
    hash = hash_number(hash, plan->image.source_hash);
    hash = hash_number(hash, plan->image.output_hash);
    hash = hash_number(hash, plan->options.max_side);
    hash = hash_number(hash, plan->options.semantic);
    hash = hash_number(hash, plan->reimport);
    hash = hash_number(hash, plan->texture_hash);
    hash = hash_number(hash, plan->previous_image_hash);
    hash = hash_number(hash, plan->budget.current_hash);
    hash = hash_number(hash, plan->budget.proposed_hash);
    hash = hash_number(hash, plan->budget.limit_bytes);
    return hash_number(hash, plan->budget.limit_count);
}

static bool prepare(
    const char *content,
    const char *source,
    const char *key,
    image_import_file_options options,
    bool reimport,
    uint64_t texture_hash,
    uint64_t previous_image_hash,
    image_import_budget_host host,
    image_import_file_plan *plan,
    char *error,
    size_t size
)
{
    assert(content && plan && host.measure && (!size || error));
    assert(!plan->source && !plan->destination && !plan->image.png);
    if (size) {
        error[0] = 0;
    }
    destination target;
    if (!destination_open(content, key, false, reimport, &target, error, size)) {
        return false;
    }
    if (!reimport && !target.missing) {
        char name[256];
        snprintf(name, sizeof name, "%s%s", target.name, IMAGE_IMPORT_TEXTURE_SUFFIX);
        struct stat info;
        if (fstatat(target.parent, name, &info, AT_SYMLINK_NOFOLLOW) == 0 ||
            errno != ENOENT) {
            destination_close(&target, false);
            return fail(error, size, "Texture metadata destination already exists or cannot be inspected");
        }
    }
    destination_close(&target, false);
    size_t length = 0;
    unsigned char *bytes = source_read(source, &length, error, size);
    if (!bytes) {
        return false;
    }
    image_import_file_plan candidate = { .options = options,
                                         .reimport = reimport,
                                         .texture_hash = texture_hash,
                                         .previous_image_hash = previous_image_hash };
    bool valid = image_import_prepare_fit(
        bytes,
        length,
        options.max_side,
        options.semantic,
        &candidate.image,
        error,
        size
    );
    free(bytes);
    if (!valid) {
        return false;
    }
    candidate.source = xstrdup(source);
    candidate.destination = xstrdup(key);
    image_import_proposal proposal = { key, &candidate.image };
    if (!host.measure(host.context, &proposal, 1, &candidate.budget, error, size)) {
        image_import_file_plan_free(&candidate);
        return false;
    }
    candidate.hash = plan_hash(content, &candidate);
    *plan = candidate;
    return true;
}

static bool stage(
    destination *target,
    const void *bytes,
    size_t length,
    char temporary[96],
    char *error,
    size_t size
)
{
    static unsigned sequence;
    int file = -1;
    for (int i = 0; i < IMAGE_TEMP_ATTEMPTS && file < 0; i++) {
        snprintf(temporary, 96, ".chibi-image-%ld-%u.tmp", (long)getpid(), ++sequence);
        file = openat(target->parent, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file < 0 && errno != EEXIST) {
            break;
        }
    }
    if (file < 0) {
        return fail(error, size, "Cannot create exclusive image import staging file");
    }
    size_t at = 0;
    while (at < length) {
        ssize_t count = write(file, (const unsigned char *)bytes + at, length - at);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        at += (size_t)count;
    }
    bool valid = at == length && fsync(file) == 0;
    valid = close(file) == 0 && valid;
    if (!valid) {
        unlinkat(target->parent, temporary, 0);
        return fail(error, size, "Cannot write complete image import staging file");
    }
    return true;
}

static char *metadata_name(const destination *target)
{
    size_t length = strlen(target->name) + sizeof IMAGE_IMPORT_TEXTURE_SUFFIX;
    char *name = xmalloc(length);
    snprintf(name, length, "%s%s", target->name, IMAGE_IMPORT_TEXTURE_SUFFIX);
    return name;
}

static unsigned char *read_at(
    destination *target,
    const char *name,
    size_t limit,
    size_t *length,
    char *error,
    size_t size
)
{
    int file = openat(target->parent, name, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    struct stat info;
    if (file < 0 || fstat(file, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || (uint64_t)info.st_size > limit) {
        if (file >= 0) {
            close(file);
        }
        fail(error, size, "Texture file is absent, not a regular file, or exceeds its byte limit");
        return NULL;
    }
    *length = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(*length + 1);
    size_t at = 0;
    while (at < *length) {
        ssize_t count = read(file, bytes + at, *length - at);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        at += (size_t)count;
    }
    unsigned char extra;
    bool valid = at == *length && read(file, &extra, 1) == 0;
    valid = close(file) == 0 && valid;
    if (!valid) {
        free(bytes);
        fail(error, size, "Texture file changed or failed while reading");
        return NULL;
    }
    bytes[*length] = 0;
    return bytes;
}

static bool metadata_read(destination *target, image_import_texture *texture, char *error, size_t size)
{
    char *name = metadata_name(target);
    size_t length = 0;
    unsigned char *bytes = read_at(target, name, 16384, &length, error, size);
    free(name);
    if (!bytes) {
        return false;
    }
    bool valid = strlen((char *)bytes) == length;
    if (!valid) {
        fail(error, size, "Texture metadata contains a zero byte");
    } else {
        valid = image_import_texture_parse((char *)bytes, texture, error, size);
    }
    free(bytes);
    return valid;
}

bool image_import_texture_read(
    const char *content,
    const char *key,
    image_import_texture *texture,
    char *error,
    size_t size
)
{
    assert(texture && !texture->source);
    destination target;
    if (!destination_open(content, key, false, true, &target, error, size)) {
        return false;
    }
    bool valid = metadata_read(&target, texture, error, size);
    destination_close(&target, false);
    return valid;
}

bool image_import_texture_save(
    const char *content,
    const char *key,
    const image_import_texture *texture,
    uint64_t expected_hash,
    char *error,
    size_t size
)
{
    char *text = image_import_texture_serialize(texture, error, size);
    if (!text) {
        return false;
    }
    destination target;
    if (!destination_open(content, key, false, true, &target, error, size)) {
        free(text);
        return false;
    }
    bool valid = true;
    if (expected_hash) {
        image_import_texture previous = { 0 };
        valid = metadata_read(&target, &previous, error, size);
        if (valid && previous.hash != expected_hash) {
            valid = fail(error, size, "Texture settings changed on disk; reload before saving");
        }
        image_import_texture_free(&previous);
    }
    char temporary[96];
    if (valid) {
        valid = stage(&target, text, strlen(text), temporary, error, size);
        if (valid) {
            char *name = metadata_name(&target);
            valid = (expected_hash
                         ? renameat(target.parent, temporary, target.parent, name)
                         : linkat(target.parent, temporary, target.parent, name, 0)) == 0;
            unlinkat(target.parent, temporary, 0);
            free(name);
            if (!valid) {
                fail(error, size, "Cannot publish texture settings; conflict or storage failure");
            }
        }
    }
    destination_close(&target, false);
    free(text);
    return valid;
}

static char *project_root(const char *content)
{
    size_t length = strlen(content) + sizeof "/..";
    char *path = xmalloc(length);
    snprintf(path, length, "%s/..", content);
    char *root = realpath(path, NULL);
    free(path);
    return root;
}

static char *source_resolve(const char *content, const char *source)
{
    if (source[0] == '/') {
        return realpath(source, NULL);
    }
    char *root = project_root(content);
    if (!root) {
        return NULL;
    }
    size_t length = strlen(root) + strlen(source) + 2;
    char *path = xmalloc(length);
    snprintf(path, length, "%s/%s", root, source);
    char *resolved = realpath(path, NULL);
    free(path);
    free(root);
    return resolved;
}

static char *source_record(const char *content, const char *source)
{
    char *root = project_root(content);
    size_t length = root ? strlen(root) : 0;
    const char *record = root && !strncmp(source, root, length) && source[length] == '/'
                           ? source + length + 1
                           : source;
    char *copy = xstrdup(record);
    free(root);
    return copy;
}

bool image_import_file_prepare(
    const char *content,
    const char *source,
    const char *key,
    image_import_file_options options,
    image_import_budget_host host,
    image_import_file_plan *plan,
    char *error,
    size_t size
)
{
    if (!source || source[0] != '/') {
        return fail(error, size, "Image source must be an absolute file path");
    }
    char *resolved = source_resolve(content, source);
    if (!resolved) {
        return fail(error, size, "Cannot resolve texture source file");
    }
    bool valid = prepare(content, resolved, key, options, false, 0, 0, host, plan, error, size);
    free(resolved);
    return valid;
}

bool image_import_file_reimport_prepare(
    const char *content,
    const char *key,
    image_import_budget_host host,
    image_import_file_plan *plan,
    char *error,
    size_t size
)
{
    assert(plan && !plan->source && !plan->destination && !plan->image.png);
    destination target;
    if (!destination_open(content, key, false, true, &target, error, size)) {
        return false;
    }
    image_import_texture texture = { 0 };
    bool valid = metadata_read(&target, &texture, error, size);
    size_t length = 0;
    unsigned char *old = valid
                           ? read_at(&target, target.name, IMAGE_FILE_BYTES_MAX, &length, error, size)
                           : NULL;
    uint64_t previous_hash = old ? fingerprint(old, length) : 0;
    free(old);
    valid = valid && previous_hash;
    destination_close(&target, false);
    char *source = valid ? source_resolve(content, texture.source) : NULL;
    if (valid && !source) {
        valid = fail(error, size, "Texture source is unavailable; update its source path before reimporting");
    }
    if (valid) {
        size_t path_length = strlen(content) + strlen(key + 8) + 2;
        char *path = xmalloc(path_length);
        snprintf(path, path_length, "%s/%s", content, key + 8);
        char *image_path = realpath(path, NULL);
        if (!image_path || !strcmp(image_path, source)) {
            valid = fail(error, size, "Texture source must differ from its derived PNG");
        }
        free(image_path);
        free(path);
    }
    if (valid) {
        valid = prepare(content, source, key, texture.options, true, texture.hash, previous_hash, host, plan, error, size);
    }
    free(source);
    image_import_texture_free(&texture);
    return valid;
}

static bool publish_new(
    const char *content,
    destination *target,
    const image_import_file_plan *plan,
    const char *image_temp,
    char *error,
    size_t size
)
{
    image_import_texture texture = { .source = source_record(content, plan->source),
                                     .options = plan->options };
    char *text = image_import_texture_serialize(&texture, error, size);
    image_import_texture_free(&texture);
    if (!text) {
        return false;
    }
    char meta_temp[96];
    bool valid = stage(target, text, strlen(text), meta_temp, error, size);
    free(text);
    if (!valid) {
        return false;
    }
    char *name = metadata_name(target);
    valid = linkat(target->parent, meta_temp, target->parent, name, 0) == 0;
    unlinkat(target->parent, meta_temp, 0);
    if (valid) {
        valid = linkat(target->parent, image_temp, target->parent, target->name, 0) == 0;
        if (!valid) {
            unlinkat(target->parent, name, 0);
        }
    }
    free(name);
    return valid ||
           fail(error, size, "Cannot publish texture PNG and metadata; destination conflict or storage failure");
}

bool image_import_file_commit(
    const char *content,
    const image_import_file_plan *plan,
    image_import_budget_host host,
    char *error,
    size_t size
)
{
    assert(plan && plan->source && plan->destination && plan->image.png);
    image_import_file_plan fresh = { 0 };
    bool valid = plan->reimport
                   ? image_import_file_reimport_prepare(content, plan->destination, host, &fresh, error, size)
                   : image_import_file_prepare(
                         content,
                         plan->source,
                         plan->destination,
                         plan->options,
                         host,
                         &fresh,
                         error,
                         size
                     );
    if (!valid) {
        return false;
    }
    if (fresh.hash != plan->hash) {
        image_import_file_plan_free(&fresh);
        return fail(error, size, "Image import plan is stale; inspect a fresh plan before applying");
    }
    if (!fresh.budget.fits) {
        image_import_file_plan_free(&fresh);
        return fail(error, size, "Image import exceeds the prepared visual collection budget");
    }
    destination target;
    valid = destination_open(content, fresh.destination, true, fresh.reimport, &target, error, size);
    if (valid) {
        char temporary[96];
        valid = stage(&target, fresh.image.png, fresh.image.png_bytes, temporary, error, size);
        if (valid) {
            if (fresh.reimport) {
                valid = renameat(target.parent, temporary, target.parent, target.name) ==
                        0;
                if (!valid) {
                    fail(error, size, "Cannot replace texture pixels; storage failure");
                }
            } else {
                valid = publish_new(content, &target, &fresh, temporary, error, size);
            }
            unlinkat(target.parent, temporary, 0);
        }
        destination_close(&target, !valid);
    }
    image_import_file_plan_free(&fresh);
    return valid;
}

#endif
