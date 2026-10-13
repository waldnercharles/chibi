#include "assets/model_import_recipe.h"

#include "assets/new_asset.h"
#include "assets/project.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define RECIPE_BYTES_MAX (2 * 1024 * 1024)
#define RECIPE_OUTPUTS_MAX 4096
/* A destination snapshot owns old bytes for validation; backups are renamed,
 * rather than reconstructed, to retain original bytes and permissions. */
typedef struct
{
    char *key, *identity, *path;
    bool editable, exists;
    uint64_t baseline;
    unsigned char *old;
    size_t old_size;
    const unsigned char *next;
    size_t next_size;
    char *staged, *backup;
    bool published, backup_original;
    model_import_action_kind action;
} destination;
struct model_import_recipe_plan
{
    char *project, *content, *source, *name;
    destination manifest;
    model_import_plan *import;
    destination *destinations;
    model_import_action *actions;
    char *recipe_text;
    char **directories;
    bool applied;
};
static bool fail(char *error, size_t size, const char *message, const char *path)
{
    errno = EINVAL;
    snprintf(error, size, "%s: %s", message, path);
    return false;
}
static bool conflict(char *error, size_t size, const char *message, const char *path)
{
    fail(error, size, message, path);
    errno = EEXIST;
    return false;
}
static bool stale(char *error, size_t size, const char *message, const char *path)
{
    fail(error, size, message, path);
    errno = ESTALE;
    return false;
}
static bool io_fail(char *error, size_t size, const char *message, const char *path)
{
    fail(error, size, message, path);
    errno = EIO;
    return false;
}
static char *join(const char *a, const char *b)
{
    size_t size = strlen(a) + strlen(b) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", a, b);
    return path;
}
static bool contained(const char *root, const char *path)
{
    size_t n = strlen(root);
    return !strncmp(root, path, n) && path[n] == '/';
}
static bool relative_path(const char *path)
{
    if (!path || !*path || *path == '/' || strchr(path, '\\') || strchr(path, ':')) {
        return false;
    }
    for (const char *part = path; *part;) {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (!end) {
            return true;
        }
        part = end + 1;
        if (!*part) {
            return false;
        }
    }
    return false;
}
/* Output keys are limited to importer-owned families. Recipes cannot grant
 * ownership of project.kdl, executable sources or arbitrary project files. */
static bool output_key(const char *key, bool editable)
{
    if (!relative_path(key)) {
        return false;
    }
    const char *prefixes[] = { "content/meshes/",
                               "content/textures/",
                               "content/materials/",
                               "content/prototypes/" };
    for (int i = 0; i < 4; i++) {
        if (!strncmp(key, prefixes[i], strlen(prefixes[i]))) {
            return editable == (i >= 2);
        }
    }
    return false;
}
static bool safe_parents(const char *project, const char *path, char *error, size_t size)
{
    char *part = xstrdup(path);
    bool valid = contained(project, path);
    for (char *slash = valid ? strchr(part + strlen(project) + 1, '/') : NULL;
         slash && valid;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        struct stat info;
        int status = lstat(part, &info);
        valid = status ? errno == ENOENT : S_ISDIR(info.st_mode);
        *slash = '/';
    }
    free(part);
    return valid ||
           fail(error, size, "Import destination parent is not a real directory", path);
}
static unsigned char *read_bytes(const char *path, size_t limit, size_t *length)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        (size_t)info.st_size > limit) {
        close(fd);
        return NULL;
    }
    FILE *file = fdopen(fd, "rb");
    if (!file) {
        close(fd);
        return NULL;
    }
    *length = (size_t)info.st_size;
    unsigned char *bytes = xmalloc(*length + 1);
    bool valid = fread(bytes, 1, *length, file) == *length;
    valid = fclose(file) == 0 && valid;
    bytes[*length] = 0;
    if (valid) {
        return bytes;
    }
    free(bytes);
    return NULL;
}
static bool snapshot(const char *project, destination *dest, char *error, size_t size)
{
    if (!safe_parents(project, dest->path, error, size)) {
        return false;
    }
    struct stat info;
    if (lstat(dest->path, &info)) {
        return errno == ENOENT ||
               io_fail(error, size, "Cannot inspect import destination", dest->key);
    }
    dest->exists = true;
    dest->old = read_bytes(dest->path, MODEL_IMPORT_BYTES_MAX, &dest->old_size);
    return dest->old ||
           fail(error, size, "Import destination is not a bounded regular file", dest->key);
}
static bool unchanged(const char *project, const destination *dest, char *error, size_t size)
{
    if (!safe_parents(project, dest->path, error, size)) {
        return false;
    }
    struct stat info;
    if (lstat(dest->path, &info)) {
        return !dest->exists && errno == ENOENT
                 ? true
                 : stale(error, size, "Import destination changed", dest->key);
    }
    size_t length = 0;
    unsigned char *bytes = read_bytes(dest->path, MODEL_IMPORT_BYTES_MAX, &length);
    bool valid = dest->exists && bytes && length == dest->old_size &&
                 !memcmp(bytes, dest->old, length);
    free(bytes);
    return valid || stale(error, size, "Import destination changed", dest->key);
}
static char *hash_text(uint64_t hash)
{
    char value[17];
    snprintf(value, sizeof value, "%016" PRIx64, hash);
    return xstrdup(value);
}
static bool parse_hash(const av_value *value, uint64_t *out)
{
    if (!av_is_str(value) || av_get_len(value) != 16) {
        return false;
    }
    const char *text = av_get_str(value);
    for (int i = 0; i < 16; i++) {
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) {
            return false;
        }
    }
    *out = strtoull(text, NULL, 16);
    return true;
}
static int find(const destination *destinations, const char *key)
{
    for (int i = 0; i < acount(destinations); i++) {
        if (!strcmp(destinations[i].key, key)) {
            return i;
        }
    }
    return -1;
}
static bool load_recipe(model_import_recipe_plan *plan, const destination *recipe, char *error, size_t size)
{
    if (!recipe->exists || recipe->old_size > RECIPE_BYTES_MAX ||
        memchr(recipe->old, 0, recipe->old_size)) {
        return fail(error, size, "Reimport needs a bounded saved recipe", recipe->key);
    }
    av_doc *doc = av_doc_read((const char *)recipe->old, error, size);
    if (!doc) {
        return false;
    }
    const av_value *root = av_doc_root(doc);
    const av_value *version = av_obj_get(root, "version"),
                   *importer = av_obj_get(root, "importer_version");
    const av_value *name = av_obj_get(root, "name"),
                   *source = av_obj_get(root, "source");
    const av_value *units = av_obj_get(root, "units"),
                   *scale = av_obj_get(root, "scale");
    const av_value *outputs = av_obj_get(root, "outputs");
    bool valid = av_is_int(version) && av_get_int(version) == 1 &&
                 av_is_int(importer) && av_get_int(importer) == 1 &&
                 av_is_str(name) && !strcmp(av_get_str(name), plan->name) &&
                 av_is_str(source) && relative_path(av_get_str(source)) &&
                 av_is_str(units) && !strcmp(av_get_str(units), "meters") &&
                 av_is_num(scale) && av_get_num(scale) == 1 &&
                 av_is_arr(outputs) && av_arr_size(outputs) > 0 &&
                 av_arr_size(outputs) <= RECIPE_OUTPUTS_MAX;
    if (valid) {
        plan->source = xstrdup(av_get_str(source));
        for (size_t i = 0; valid && i < av_arr_size(outputs); i++) {
            const av_value *item = av_arr_get(outputs, i);
            const av_value *key = av_obj_get(item, "key"),
                           *identity = av_obj_get(item, "identity"),
                           *editable = av_obj_get(item, "editable");
            uint64_t hash = 0;
            valid = av_is_str(key) && av_is_str(identity) &&
                    av_get_len(identity) > 0 && av_get_len(identity) <= 4096 &&
                    av_is_bool(editable) &&
                    output_key(av_get_str(key), av_get_bool(editable)) &&
                    parse_hash(av_obj_get(item, "hash"), &hash) &&
                    find(plan->destinations, av_get_str(key)) < 0;
            if (!valid) {
                break;
            }
            destination dest = { .key = xstrdup(av_get_str(key)),
                                 .identity = xstrdup(av_get_str(identity)),
                                 .editable = av_get_bool(editable),
                                 .baseline = hash };
            dest.path = join(plan->content, dest.key + 8);
            apush(plan->destinations, dest);
        }
    }
    av_doc_free(doc);
    return valid ||
           fail(error, size, "Invalid or unsupported import recipe", recipe->key);
}
static char *save_recipe(model_import_recipe_plan *plan)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "version", 1);
    av_obj_add_int(doc, root, "importer_version", 1);
    av_obj_add_str(doc, root, "name", plan->name);
    av_obj_add_str(doc, root, "source", plan->source);
    av_obj_add_str(doc, root, "units", "meters");
    av_obj_add_float(doc, root, "scale", 1);
    av_value *deps = av_obj_add_arr(doc, root, "dependencies");
    int count = 0;
    const model_import_dependency
        *dependencies = model_import_plan_dependencies(plan->import, &count);
    for (int i = 0; i < count; i++) {
        av_value *item = av_arr_add_obj(doc, deps);
        av_obj_add_str(doc, item, "path", dependencies[i].path + strlen(plan->project) + 1);
        char *hash = hash_text(dependencies[i].hash);
        av_obj_add_str(doc, item, "hash", hash);
        free(hash);
        av_obj_add_uint(doc, item, "bytes", dependencies[i].size);
    }
    av_value *outputs = av_obj_add_arr(doc, root, "outputs");
    for (int i = 0; i < acount(plan->destinations); i++) {
        const destination *dest = &plan->destinations[i];
        if (dest->action == MODEL_IMPORT_REMOVE) {
            continue;
        }
        av_value *item = av_arr_add_obj(doc, outputs);
        av_obj_add_str(doc, item, "key", dest->key);
        av_obj_add_str(doc, item, "identity", dest->identity);
        av_obj_add_bool(doc, item, "editable", dest->editable);
        uint64_t value = dest->action == MODEL_IMPORT_PRESERVE
                           ? dest->baseline
                           : model_import_fingerprint(dest->next, dest->next_size);
        char *hash = hash_text(value);
        av_obj_add_str(doc, item, "hash", hash);
        free(hash);
    }
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}
bool model_import_recipe_prepare(
    const char *project,
    const char *source,
    const char *name,
    bool reimport,
    bool strict,
    model_import_recipe_plan **result,
    char *error,
    size_t size
)
{
    *result = NULL;
    errno = EINVAL;
    if (!new_asset_name_valid(name, error, size)) {
        return false;
    }
    model_import_recipe_plan *plan = xcalloc(1, sizeof *plan);
    plan->project = realpath(project, NULL);
    plan->name = xstrdup(name);
    if (!plan->project) {
        model_import_recipe_free(plan);
        return fail(error, size, "Cannot resolve project", project);
    }
    plan->manifest.key = xstrdup("project.kdl");
    plan->manifest.path = join(plan->project, "project.kdl");
    if (!snapshot(plan->project, &plan->manifest, error, size)) {
        int code = errno;
        model_import_recipe_free(plan);
        errno = code;
        return false;
    }
    char *configured = chibi_project_content(plan->project, error, size);
    plan->content = configured ? realpath(configured, NULL) : NULL;
    free(configured);
    if (!plan->content || !contained(plan->project, plan->content)) {
        model_import_recipe_free(plan);
        return fail(error, size, "Saved imports require an existing content folder inside the project", project);
    }
    if (!unchanged(plan->project, &plan->manifest, error, size)) {
        int code = errno;
        model_import_recipe_free(plan);
        errno = code;
        return false;
    }
    char key[256];
    snprintf(key, sizeof key, "content/imports/%s.kdl", name);
    destination recipe = { .key = xstrdup(key), .identity = xstrdup("recipe") };
    recipe.path = join(plan->content, key + 8);
    bool valid = snapshot(plan->project, &recipe, error, size);
    if (valid && reimport) {
        valid = load_recipe(plan, &recipe, error, size);
    }
    if (valid && !reimport && recipe.exists) {
        valid = conflict(error, size, "Import recipe already exists", key);
    }
    if (valid && source && plan->source && strcmp(source, plan->source)) {
        valid = fail(error, size, "Reimport must use the saved source", plan->source);
    }
    if (valid && !plan->source) {
        valid = source && *source;
        if (valid) {
            plan->source = xstrdup(source);
        }
        if (!valid) {
            fail(error, size, "Import requires a source", name);
        }
    }
    char *path = valid ? (plan->source[0] == '/' ? xstrdup(plan->source)
                                                 : join(plan->project, plan->source))
                       : NULL;
    char *real = path ? realpath(path, NULL) : NULL;
    if (valid) {
        valid = real && contained(plan->project, real);
    }
    if (valid) {
        free(plan->source);
        plan->source = xstrdup(real + strlen(plan->project) + 1);
        valid = model_import_prepare(plan->content, real, name, reimport, &plan->import, error, size);
    } else if (path) {
        fail(error, size, "Saved imports require a source inside the project", path);
    }
    free(path);
    free(real);
    if (valid && strict && acount(model_import_plan_report(plan->import)->warnings)) {
        valid = fail(error, size, "Strict import rejected warnings", name);
    }
    int count = 0;
    const model_import_output *outputs = valid
                                           ? model_import_plan_outputs(plan->import, &count)
                                           : NULL;
    for (int i = 0; valid && i < acount(plan->destinations); i++) {
        destination *dest = &plan->destinations[i];
        size_t total = recipe.old_size;
        for (int j = 0; j < i; j++) {
            total += plan->destinations[j].old_size;
        }
        struct stat info;
        if (!lstat(dest->path, &info) && info.st_size > 0 &&
            (size_t)info.st_size > MODEL_IMPORT_BYTES_MAX * (size_t)2 - total) {
            valid = fail(error, size, "Import destination snapshots exceed 512 MiB", name);
            break;
        }
        valid = snapshot(plan->project, dest, error, size);
        if (!valid) {
            break;
        }
        if (!dest->exists) {
            valid = conflict(error, size, "Previously imported output is missing", dest->key);
            break;
        }
        bool edited = model_import_fingerprint(dest->old, dest->old_size) !=
                      dest->baseline;
        dest->action = MODEL_IMPORT_REMOVE;
        for (int j = 0; j < count; j++) {
            if (strcmp(dest->key, outputs[j].key)) {
                continue;
            }
            valid = dest->editable == outputs[j].editable &&
                    !strcmp(dest->identity, outputs[j].identity);
            if (!valid) {
                conflict(error, size, "Imported identity changed underneath an output", dest->key);
                break;
            }
            dest->next = outputs[j].data;
            dest->next_size = outputs[j].size;
            dest->action = edited && dest->editable ? MODEL_IMPORT_PRESERVE
                                                    : MODEL_IMPORT_UPDATE;
            break;
        }
        if (valid && edited && dest->action != MODEL_IMPORT_PRESERVE) {
            valid = conflict(error, size, "Modified imported output conflicts with reimport", dest->key);
        }
    }
    if (valid) {
        bool authored = false;
        for (int i = 0; i < acount(plan->destinations); i++) {
            authored |= plan->destinations[i].action == MODEL_IMPORT_PRESERVE;
        }
        for (int i = 0; valid && authored && i < acount(plan->destinations); i++) {
            if (plan->destinations[i].action == MODEL_IMPORT_REMOVE) {
                valid = conflict(
                    error,
                    size,
                    "Source removes an output that preserved authored assets "
                    "may reference",
                    plan->destinations[i].key
                );
            }
        }
    }
    for (int i = 0; valid && i < count; i++) {
        if (find(plan->destinations, outputs[i].key) >= 0) {
            continue;
        }
        destination dest = { .key = xstrdup(outputs[i].key),
                             .identity = xstrdup(outputs[i].identity),
                             .editable = outputs[i].editable,
                             .next = outputs[i].data,
                             .next_size = outputs[i].size,
                             .action = MODEL_IMPORT_CREATE };
        dest.path = join(plan->content, dest.key + 8);
        valid = snapshot(plan->project, &dest, error, size);
        if (valid && dest.exists) {
            valid = conflict(error, size, "New import output already exists", dest.key);
        }
        apush(plan->destinations, dest);
    }
    if (valid) {
        size_t total = recipe.old_size;
        for (int i = 0; valid && i < acount(plan->destinations); i++) {
            size_t bytes = plan->destinations[i].old_size;
            valid = bytes <= MODEL_IMPORT_BYTES_MAX * (size_t)2 - total;
            if (valid) {
                total += bytes;
            }
        }
        if (!valid) {
            fail(error, size, "Import destination snapshots exceed 512 MiB", name);
        }
    }
    if (valid) {
        int deps = 0;
        const model_import_dependency
            *dependencies = model_import_plan_dependencies(plan->import, &deps);
        for (int i = 0; valid && i < deps; i++) {
            valid = contained(plan->project, dependencies[i].path);
        }
        if (!valid) {
            fail(error, size, "Import dependency escapes project", name);
        }
    }
    if (valid) {
        plan->recipe_text = save_recipe(plan);
        recipe.next = (const unsigned char *)plan->recipe_text;
        recipe.next_size = strlen(plan->recipe_text);
        recipe.action = recipe.exists ? MODEL_IMPORT_UPDATE : MODEL_IMPORT_CREATE;
    }
    apush(plan->destinations, recipe);
    if (!valid) {
        int code = errno;
        model_import_recipe_free(plan);
        errno = code;
        return false;
    }
    for (int i = 0; i < acount(plan->destinations); i++) {
        destination *dest = &plan->destinations[i];
        apush(plan->actions, ((model_import_action){ dest->key, dest->action }));
    }
    *result = plan;
    return true;
}
const model_import_plan *model_import_recipe_import_plan(const model_import_recipe_plan *plan)
{
    return plan->import;
}
const model_import_report *model_import_recipe_report(const model_import_recipe_plan *plan)
{
    return model_import_plan_report(plan->import);
}
const model_import_action *model_import_recipe_actions(const model_import_recipe_plan *plan, int *count)
{
    *count = acount(plan->actions);
    return plan->actions;
}
static bool parents(model_import_recipe_plan *plan, const char *path, char *error, size_t size)
{
    if (!safe_parents(plan->project, path, error, size)) {
        return false;
    }
    char *part = xstrdup(path);
    bool valid = true;
    for (char *slash = strchr(part + strlen(plan->project) + 1, '/'); slash && valid;
         slash = strchr(slash + 1, '/')) {
        *slash = 0;
        if (!mkdir(part, 0755)) {
            apush(plan->directories, xstrdup(part));
        } else {
            valid = errno == EEXIST;
        }
        *slash = '/';
    }
    free(part);
    return valid || io_fail(error, size, "Cannot create import directory", path);
}
static char *temporary(const char *path, const char *kind)
{
    size_t size = strlen(path) + strlen(kind) + 24;
    char *value = xmalloc(size);
    snprintf(value, size, "%s.chibi-%s-XXXXXX", path, kind);
    return value;
}
static bool stage(destination *dest, char *error, size_t size)
{
    char *staged = temporary(dest->path, "stage");
    int fd = mkstemp(staged);
    if (fd < 0) {
        free(staged);
        return io_fail(error, size, "Cannot stage imported output", dest->key);
    }
    dest->staged = staged;
    FILE *file = fdopen(fd, "wb");
    bool valid = file && fwrite(dest->next, 1, dest->next_size, file) == dest->next_size;
    if (file) {
        valid = fclose(file) == 0 && valid;
    } else {
        close(fd);
    }
    return valid ||
           io_fail(error, size, "Cannot write staged imported output", dest->key);
}
static void residual(char *error, size_t size, const char *message, const char *path)
{
    size_t used = strlen(error);
    if (used < size) {
        snprintf(error + used, size - used, "%s%s: %s", used ? "; " : "", message, path);
    }
}
static bool remove_owned(const char *path, const char *message, char *error, size_t size)
{
    if (!unlink(path) || errno == ENOENT) {
        return true;
    }
    residual(error, size, message, path);
    return false;
}
static bool clean_transaction(model_import_recipe_plan *plan, bool valid, char *error, size_t size)
{
    bool clean = true;
    /* Restore originals first, before reporting less important stage debris.
     * Rename directly over our replacement avoids an unnecessary unlink. */
    for (int i = acount(plan->destinations) - 1; !valid && i >= 0; i--) {
        destination *dest = &plan->destinations[i];
        if (!dest->backup_original) {
            continue;
        }
        if (rename(dest->backup, dest->path)) {
            if (clean) {
                error[0] = 0;
            }
            residual(error, size, "Cannot restore import output; original retained at", dest->backup);
            clean = false;
        } else {
            free(dest->backup);
            dest->backup = NULL;
            dest->backup_original = false;
            dest->published = false;
        }
    }
    if (valid) {
        error[0] = 0;
    }
    for (int i = acount(plan->destinations) - 1; i >= 0; i--) {
        destination *dest = &plan->destinations[i];
        if (!valid && dest->published && !dest->backup_original) {
            clean = remove_owned(dest->path, "Cannot remove newly published output; retained at", error, size) &&
                    clean;
        }
        if (dest->backup && (valid || !dest->backup_original)) {
            clean = remove_owned(
                        dest->backup,
                        valid ? "Import published; cannot remove backup "
                                "retained at"
                              : "Cannot remove backup placeholder retained at",
                        error,
                        size
                    ) &&
                    clean;
        }
        if (dest->staged) {
            clean = remove_owned(
                        dest->staged,
                        valid ? "Import published; cannot remove stage "
                                "retained at"
                              : "Cannot remove stage retained at",
                        error,
                        size
                    ) &&
                    clean;
        }
        free(dest->backup);
        dest->backup = NULL;
        dest->backup_original = false;
        free(dest->staged);
        dest->staged = NULL;
        dest->published = false;
    }
    for (int i = acount(plan->directories) - 1; i >= 0; i--) {
        if (!valid && rmdir(plan->directories[i]) && errno != ENOENT) {
            residual(error, size, "Cannot remove import directory retained at", plan->directories[i]);
            clean = false;
        }
        free(plan->directories[i]);
    }
    afree(plan->directories);
    return clean;
}
bool model_import_recipe_apply(model_import_recipe_plan *plan, char *error, size_t size)
{
    if (plan->applied) {
        return fail(error, size, "Import plan was already applied", plan->name);
    }
    error[0] = 0;
    errno = EINVAL;
    if (!unchanged(plan->project, &plan->manifest, error, size)) {
        return false;
    }
    for (int i = 0; i < acount(plan->destinations); i++) {
        if (!unchanged(plan->project, &plan->destinations[i], error, size)) {
            return false;
        }
    }
    if (!model_import_plan_valid(plan->import, error, size)) {
        return false;
    }
    bool valid = true;
    for (int i = 0; valid && i < acount(plan->destinations); i++) {
        destination *dest = &plan->destinations[i];
        if (dest->action == MODEL_IMPORT_PRESERVE || dest->action == MODEL_IMPORT_REMOVE) {
            continue;
        }
        valid = parents(plan, dest->path, error, size) && stage(dest, error, size);
    }
    /* Staging may take time: revalidate every input once all bytes are ready. */
    if (valid) {
        valid = unchanged(plan->project, &plan->manifest, error, size);
    }
    if (valid) {
        valid = model_import_plan_valid(plan->import, error, size);
    }
    for (int i = 0; valid && i < acount(plan->destinations); i++) {
        valid = unchanged(plan->project, &plan->destinations[i], error, size);
    }
    for (int i = 0; valid && i < acount(plan->destinations); i++) {
        destination *dest = &plan->destinations[i];
        if (dest->action == MODEL_IMPORT_PRESERVE) {
            continue;
        }
        if (dest->exists) {
            char *backup = temporary(dest->path, "backup");
            int fd = mkstemp(backup);
            if (fd < 0) {
                free(backup);
                valid = false;
            } else {
                close(fd);
                dest->backup = backup;
                valid = rename(dest->path, backup) == 0;
                dest->backup_original = valid;
            }
        }
        if (valid && dest->action != MODEL_IMPORT_REMOVE) {
            /* link refuses an unexpected destination, unlike rename. Staged
             * files are beside their destination on the same filesystem. */
            valid = link(dest->staged, dest->path) == 0;
            dest->published = valid;
        }
        if (!valid) {
            io_fail(error, size, "Cannot publish imported output", dest->key);
        }
    }
    int code = errno;
    bool clean = clean_transaction(plan, valid, error, size);
    if (!valid || !clean) {
        errno = clean ? code : EIO;
    }
    /* A committed import cannot be retried even when cleanup leaves debris. */
    plan->applied = valid;
    return valid && clean;
}
bool model_import_recipe_committed(const model_import_recipe_plan *plan)
{
    return plan->applied;
}
void model_import_recipe_free(model_import_recipe_plan *plan)
{
    if (!plan) {
        return;
    }
    for (int i = 0; i < acount(plan->destinations); i++) {
        destination *dest = &plan->destinations[i];
        free(dest->key);
        free(dest->identity);
        free(dest->path);
        free(dest->old);
        free(dest->staged);
        free(dest->backup);
    }
    afree(plan->destinations);
    afree(plan->actions);
    free(plan->recipe_text);
    free(plan->manifest.key);
    free(plan->manifest.path);
    free(plan->manifest.old);
    free(plan->content);
    model_import_plan_free(plan->import);
    free(plan->project);
    free(plan->source);
    free(plan->name);
    free(plan);
}
