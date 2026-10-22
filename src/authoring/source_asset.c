#include "authoring/source_asset.h"

#include "authoring/asset_compiler_host.h"
#include "support/array.h"
#include "support/asset_document.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool native_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool normalize_fields(av_value *values, const type_desc *type, int depth, char *error, size_t size)
{
    if (!av_is_obj(values) || depth >= 64 || (!type && av_obj_size(values))) {
        return native_error(error, size, "Invalid reflected prototype field object");
    }
    if (!type) {
        return true;
    }
    for (size_t i = 0; i < av_obj_size(values); i++) {
        av_value *key = av_obj_key_at(values, i), *value = av_obj_value_at(values, i);
        const type_field *found = NULL;
        for (int j = 0; j < type->field_count; j++) {
            if (script_name_matches(av_get_str(key), type->fields[j].name)) {
                if (found) {
                    return native_error(error, size, "Ambiguous reflected field spelling");
                }
                found = &type->fields[j];
            }
        }
        if (!found) {
            return native_error(error, size, "Unknown reflected field in prototype defaults");
        }
        for (size_t j = 0; j < i; j++) {
            if (!strcmp(av_get_str(av_obj_key_at(values, j)), found->name)) {
                return native_error(error, size, "Prototype defaults repeat the same reflected field");
            }
        }
        av_set_str(key, found->name);
        if (found->type == PIN_ENUM && av_is_str(value)) {
            const char *label = av_get_str(value), *dot = strrchr(label, '.');
            if (dot) {
                if (!found->type_name ||
                    (size_t)(dot - label) != strlen(found->type_name) ||
                    strncmp(label, found->type_name, (size_t)(dot - label))) {
                    return native_error(error, size, "Prototype default names the wrong enum type");
                }
                av_set_str(value, dot + 1);
            }
        }
        if (found->type == PIN_STRUCT && av_is_obj(value) &&
            !normalize_fields(value, type_catalog_find(found->type_name), depth + 1, error, size)) {
            return false;
        }
    }
    return true;
}
static bool normalize_components(
    av_value *components,
    const prototype_registry *registry,
    char *error,
    size_t size
)
{
    if (!components) {
        return true;
    }
    if (!av_is_obj(components)) {
        return native_error(error, size, "Prototype components must be an object");
    }
    for (size_t i = 0; i < av_obj_size(components); i++) {
        av_value *key = av_obj_key_at(components, i);
        const ecs_component_desc *found = NULL;
        for (int j = 0; j < registry->component_count; j++) {
            if (script_name_matches(av_get_str(key), registry->components[j]->name)) {
                if (found) {
                    return native_error(error, size, "Ambiguous prototype component spelling");
                }
                found = registry->components[j];
            }
        }
        if (!found) {
            return native_error(error, size, "Unknown prototype component");
        }
        for (size_t j = 0; j < i; j++) {
            if (!strcmp(av_get_str(av_obj_key_at(components, j)), found->name)) {
                return native_error(error, size, "Duplicate prototype component");
            }
        }
        av_set_str(key, found->name);
        if (!normalize_fields(av_obj_value_at(components, i), found->type, 0, error, size)) {
            return false;
        }
    }
    return true;
}
static av_doc *normalize_prototype(av_doc *doc, const prototype_registry *registry, char *error, size_t size)
{
    av_value *root = av_doc_root(doc);
    if (doc && (!av_is_obj(root) || !av_is_str(av_obj_get(root, "name")) ||
                !av_get_len(av_obj_get(root, "name")))) {
        native_error(error, size, "Prototype source needs an object with a nonempty name");
        av_doc_free(doc);
        return NULL;
    }
    if (doc && registry &&
        (!normalize_components(av_obj_get(root, "components"), registry, error, size) ||
         !normalize_components(av_obj_get(root, "overrides"), registry, error, size))) {
        av_doc_free(doc);
        return NULL;
    }
    return doc;
}

av_doc *authoring_prototype_source_read_with_options(
    const char *source,
    const prototype_registry *registry,
    const authoring_prototype_source_options *options,
    char *error,
    size_t size
)
{
    if (!options) {
        return normalize_prototype(asset_document_read(source, error, size), registry, error, size);
    }
    const asset_compiler_options *packaged = asset_source_default_options();
    if (!packaged) {
        if (size) {
            snprintf(error, size, "Packaged asset compiler is unavailable");
        }
        return NULL;
    }
    asset_compiler_options configured = *packaged;
    configured.evaluation_budget = options->instruction_budget;
    configured.cancelled = options->cancelled;
    configured.user = options->user;
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *doc = asset_source_read(source, NULL, &configured, &diagnostic);
    if (size) {
        if (doc) {
            error[0] = 0;
        } else {
            snprintf(
                error,
                size,
                "%d:%d: %s",
                diagnostic.line,
                diagnostic.column,
                diagnostic.message
            );
        }
    }
    return normalize_prototype(doc, registry, error, size);
}

av_doc *authoring_prototype_source_read(
    const char *source,
    const prototype_registry *registry,
    char *error,
    size_t size
)
{
    return authoring_prototype_source_read_with_options(source, registry, NULL, error, size);
}

av_doc *authoring_prototype_document_read(
    const char *source,
    const prototype_registry *registry,
    char *error,
    size_t size
)
{
    return authoring_prototype_source_read(source, registry, error, size);
}

ecs_relationship_desc *authoring_relationship_source_read(const char *source, char *error, size_t size)
{
    av_doc *doc = asset_document_read(source, error, size);
    av_value *root = av_doc_root(doc);
    const char *name = av_get_str(av_obj_get(root, "name"));
    if (!name || !*name) {
        if (size) {
            snprintf(error, size, "Relationship needs a name");
        }
        av_doc_free(doc);
        return NULL;
    }
    ecs_relationship_desc *desc = xcalloc(1, sizeof *desc);
    desc->name = xstrdup(name);
    size_t length = strlen(name) + sizeof "relationship.";
    char *key = xmalloc(length);
    snprintf(key, length, "relationship.%s", name);
    desc->key = key;
    desc->id = ecs_component_marker_id(key);
    bool valid = true;
    for (size_t i = 0; valid && i < av_obj_size(root); i++) {
        const char *property = av_get_str(av_obj_key_at(root, i));
        av_value *value = av_obj_value_at(root, i);
        if (!strcmp(property, "name")) {
            continue;
        }
        unsigned trait = !strcmp(property, "exclusive")   ? ECS_REL_EXCLUSIVE
                       : !strcmp(property, "acyclic")     ? ECS_REL_ACYCLIC
                       : !strcmp(property, "traversable") ? ECS_REL_TRAVERSABLE
                                                          : 0;
        if (trait && av_is_bool(value)) {
            if (av_get_bool(value)) {
                desc->traits |= trait;
            }
        } else if (!strcmp(property, "on_delete_target") && av_is_str(value)) {
            const char *policy = av_get_str(value);
            if (!strcmp(policy, "Remove")) {
                desc->on_delete_target = ECS_CLEANUP_REMOVE;
            } else if (!strcmp(policy, "Delete")) {
                desc->on_delete_target = ECS_CLEANUP_DELETE;
            } else if (!strcmp(policy, "Panic")) {
                desc->on_delete_target = ECS_CLEANUP_PANIC;
            } else {
                valid = false;
            }
        } else {
            valid = false;
        }
    }
    if ((desc->traits & ECS_REL_TRAVERSABLE) && !(desc->traits & ECS_REL_EXCLUSIVE)) {
        valid = false;
    }
    if (desc->traits & ECS_REL_TRAVERSABLE) {
        desc->traits |= ECS_REL_ACYCLIC;
    }
    av_doc_free(doc);
    if (!valid) {
        if (size) {
            snprintf(error, size, "Invalid relationship properties or target deletion policy");
        }
        authoring_relationship_source_free(desc);
        return NULL;
    }
    return desc;
}

void authoring_relationship_source_free(ecs_relationship_desc *desc)
{
    if (desc) {
        free((char *)desc->name);
        free((char *)desc->key);
        free(desc);
    }
}

static int relationship_order(const void *a, const void *b)
{
    const ecs_relationship_desc *const *left = a, *const *right = b;
    return strcmp((*left)->name, (*right)->name);
}

bool authoring_relationship_declarations_load(
    const char *content,
    ecs_relationship_desc ***out,
    char *error,
    size_t size
)
{
    if (size) {
        error[0] = 0;
    }
    char path[4096];
    if (snprintf(path, sizeof path, "%s/relationships", content) >= (int)sizeof path) {
        if (size) {
            snprintf(error, size, "Relationship directory path exceeds limit");
        }
        return false;
    }
    struct stat info;
    if (!lstat(path, &info) && !S_ISDIR(info.st_mode)) {
        if (size) {
            snprintf(error, size, "Relationship declaration directory must be a real directory");
        }
        return false;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        if (errno == ENOENT) {
            return true;
        }
        if (size) {
            snprintf(error, size, "Cannot open relationship declarations");
        }
        return false;
    }
    bool valid = true;
    struct dirent *entry;
    size_t total = 0;
    while (valid && (entry = readdir(directory))) {
        size_t length = strlen(entry->d_name);
        if (entry->d_name[0] == '.' || length <= 6 ||
            strcmp(entry->d_name + length - 6, ".chibi")) {
            continue;
        }
        char file[4096];
        if (snprintf(file, sizeof file, "%s/%s", path, entry->d_name) >= (int)sizeof file ||
            acount(*out) >= 1024) {
            valid = false;
            break;
        }
        if (lstat(file, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
            info.st_size > 512 * 1024 ||
            (size_t)info.st_size > 32 * 1024 * 1024 - total) {
            valid = false;
            break;
        }
        char *text = text_file_read(file);
        if (!text || strlen(text) > 512 * 1024 ||
            strlen(text) > 32 * 1024 * 1024 - total) {
            free(text);
            valid = false;
            break;
        }
        total += strlen(text);
        ecs_relationship_desc *desc = authoring_relationship_source_read(text, error, size);
        free(text);
        if (!desc) {
            valid = false;
            break;
        }
        for (int i = 0; valid && i < acount(*out); i++) {
            valid = desc->id != (*out)[i]->id && strcmp(desc->name, (*out)[i]->name);
        }
        if (!valid) {
            authoring_relationship_source_free(desc);
            break;
        }
        apush(*out, desc);
    }
    closedir(directory);
    if (!valid && size && !error[0]) {
        snprintf(error, size, "Invalid relationship file count, bytes or duplicate name");
    }
    if (valid && acount(*out) > 1) {
        qsort(*out, (size_t)acount(*out), sizeof **out, relationship_order);
    }
    return valid;
}
