#include "authoring/project_prepare_internal.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define PROJECT_DOCUMENT_BYTES (512 * 1024)
#define PROJECT_DOCUMENT_TOTAL (32 * 1024 * 1024)
#define PROJECT_PROTOTYPES 1024
static bool fail(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
char *project_prepare_read(const char *path, size_t *budget, char *error, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (size) {
            snprintf(error, size, "Cannot read %s: %s", path, strerror(errno));
        }
        return NULL;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        fail(error, size, "Project document seek failed");
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > PROJECT_DOCUMENT_BYTES ||
        (budget && (size_t)length > *budget) || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        fail(error, size, "Project document exceeds preparation limits");
        return NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    bool valid = fread(text, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!valid) {
        free(text);
        fail(error, size, "Project document read was incomplete");
        return NULL;
    }
    text[length] = 0;
    if (memchr(text, 0, (size_t)length)) {
        free(text);
        fail(error, size, "Project document contains embedded NUL");
        return NULL;
    }
    if (budget) {
        *budget -= (size_t)length;
    }
    return text;
}
static const char *include(const char *name, void *user)
{
    const project_prototype *values = user;
    for (int i = 0; i < acount(values); i++) {
        if (!strcmp(values[i].name, name)) {
            return values[i].text;
        }
    }
    return NULL;
}
static int compare(const void *a, const void *b)
{
    return strcmp(
        ((const project_prototype *)a)->name,
        ((const project_prototype *)b)->name
    );
}
bool project_prepare_prototypes(
    const char *root,
    game_prototype_registry *registry,
    project_prototype **result,
    char *error,
    size_t size
)
{
    char directory[4096];
    if (snprintf(directory, sizeof directory, "%s/prototypes", root) >=
        (int)sizeof directory) {
        return fail(error, size, "Prototype directory path exceeds limit");
    }
    DIR *dir = opendir(directory);
    if (!dir) {
        return errno == ENOENT || fail(error, size, "Cannot open prototype directory");
    }
    size_t budget = PROJECT_DOCUMENT_TOTAL;
    bool valid = true;
    struct dirent *entry;
    while (valid && (entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || n < 4 || strcmp(entry->d_name + n - 4, ".kdl")) {
            continue;
        }
        char path[4096];
        if (acount(*result) == PROJECT_PROTOTYPES ||
            snprintf(path, sizeof path, "%s/%s", directory, entry->d_name) >=
                (int)sizeof path) {
            valid = fail(error, size, "Prototype file count or path exceeds limit");
            break;
        }
        char *text = project_prepare_read(path, &budget, error, size);
        av_doc *document = text ? av_doc_read(text, error, size) : NULL;
        av_value *name = document ? av_obj_get(av_doc_root(document), "name") : NULL;
        if (!name || !av_is_str(name) || !av_get_len(name) ||
            av_get_len(name) > GAME_PROTOTYPE_NAME_MAX) {
            free(text);
            av_doc_free(document);
            valid = fail(error, size, "Prototype declaration has no bounded name");
            break;
        }
        apush(*result, ((project_prototype){ .name = xstrdup(av_get_str(name)), .text = text }));
        av_doc_free(document);
    }
    closedir(dir);
    if (!valid) {
        return false;
    }
    if (acount(*result) > 1) {
        asort(*result, compare);
    }
    for (int i = 1; i < acount(*result); i++) {
        if (!strcmp((*result)[i - 1].name, (*result)[i].name)) {
            return fail(error, size, "Duplicate prototype declaration name");
        }
    }
    registry->include = include;
    registry->include_user = *result;
    int expanded = 0;
    for (int i = 0; i < acount(*result); i++) {
        (*result)[i].prototype = game_prototype_load((*result)[i].text, registry, error, size);
        if (!(*result)[i].prototype ||
            !project_prepare_expand(&expanded, (*result)[i].prototype, error, size)) {
            return false;
        }
        const char *identity = game_prototype_identity((*result)[i].prototype);
        for (int j = 0; j < i; j++) {
            if (!strcmp(identity, game_prototype_identity((*result)[j].prototype))) {
                return fail(error, size, "Duplicate prototype runtime identity");
            }
        }
    }
    return true;
}
bool project_prepare_expand(int *expanded, const game_prototype *prototype, char *error, size_t size)
{
    *expanded += 1 + prototype->expanded;
    if (*expanded < GAME_PROTOTYPE_EXPANSION_MAX) {
        return true;
    }
    if (size) {
        snprintf(error, size, "Prototypes read more than %d prototypes through their includes and children", GAME_PROTOTYPE_EXPANSION_MAX);
    }
    return false;
}
const game_prototype *project_prepare_prototype(const project_prototype *values, const char *name)
{
    for (int i = 0; i < acount(values); i++) {
        if (!strcmp(values[i].name, name)) {
            return values[i].prototype;
        }
    }
    return NULL;
}
game_prototype *project_prepare_object(
    const game_scene_entity *entity,
    const project_prototype *values,
    char *error,
    size_t size
)
{
    const game_prototype *base = project_prepare_prototype(values, entity->prototype);
    if (!base) {
        fail(error, size, "Scene references an unknown prototype");
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    /* The include stack tracks declaration names. The overlay is not a
     * declaration, even when its scene name matches the included prototype. */
    char overlay[64];
    int ordinal = 0;
    do {
        snprintf(overlay, sizeof overlay, "__scene_instance_%d", ordinal++);
    } while (project_prepare_prototype(values, overlay));
    av_obj_add_str(doc, root, "name", overlay);
    /* Keep the base's stable tag identity for native prototype selectors. */
    av_obj_add_str(doc, root, "identity", game_prototype_identity(base));
    av_value *includes = av_obj_add_arr(doc, root, "includes");
    av_arr_add_str(doc, includes, base->name);
    if (entity->components) {
        av_obj_add_val(doc, root, "components", av_clone(doc, entity->components));
    }
    game_prototype *object = game_prototype_load_value(root, base->registry, error, size);
    av_doc_free(doc);
    if (!object) {
        return NULL;
    }
    free(object->name);
    object->name = xstrdup(entity->name);
    if (!entity->own_behaviors) {
        return object;
    }
    aclear(object->graphs);
    for (int i = 0; i < acount(entity->behaviors); i++) {
        game_prototype_add_graph(object, entity->behaviors[i]);
    }
    return object;
}
void project_prepare_prototypes_free(project_prototype *values)
{
    for (int i = 0; i < acount(values); i++) {
        game_prototype_free(values[i].prototype);
        free(values[i].name);
        free(values[i].text);
    }
    afree(values);
}
