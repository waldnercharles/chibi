#include "automation/controller.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "ecs/ecs.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum
{
    ASSET_GRAPH,
    ASSET_PROTOTYPE,
    ASSET_SCENE,
} asset_loader;

typedef struct
{
    const char *kind;
    const char *folder;
    asset_loader loader;
} asset_family;

static const asset_family ASSET_FAMILIES[] = {
    { "behavior", "behaviors", ASSET_GRAPH },
    { "event", "events", ASSET_GRAPH },
    { "library", "libraries", ASSET_GRAPH },
    { "material", "materials", ASSET_GRAPH },
    { "shape", "shapes", ASSET_GRAPH },
    { "shader-library", "shader-libraries", ASSET_GRAPH },
    { "prototype", "prototypes", ASSET_PROTOTYPE },
    { "scene", "scenes", ASSET_SCENE },
};

typedef struct
{
    const asset_family *family;
    char filename[256];
    char relative[512];
} asset_ref;

typedef struct
{
    const asset_family *family;
    char *filename;
    char *relative;
    size_t bytes;
    uint64_t hash;
} asset_entry;

typedef struct
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    bool ok;
} response;

static const asset_family *family_by_kind(const char *kind)
{
    if (!kind) {
        return NULL;
    }
    for (int i = 0; i < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES); i++) {
        if (!strcmp(kind, ASSET_FAMILIES[i].kind) ||
            !strcmp(kind, ASSET_FAMILIES[i].folder)) {
            return &ASSET_FAMILIES[i];
        }
    }
    return NULL;
}

static bool valid_asset_filename(const char *filename)
{
    if (!filename || !filename[0] || filename[0] == '.' || strchr(filename, '/') ||
        strchr(filename, '\\') || strstr(filename, "..")) {
        return false;
    }
    size_t length = strlen(filename);
    return length > sizeof ".json" - 1 &&
           !strcmp(filename + length - (sizeof ".json" - 1), ".json");
}

static bool valid_relative_asset_path(const char *path)
{
    return path && path[0] && path[0] != '/' && !strstr(path, "..") &&
           !strstr(path, "//") && !strchr(path, '\\');
}

static int set_error(char *error, size_t size, const char *message)
{
    if (error && size) {
        snprintf(error, size, "%s", message ? message : "unknown error");
    }
    return -1;
}

static int set_errorf(char *error, size_t size, const char *format, const char *value)
{
    if (error && size) {
        snprintf(error, size, format, value ? value : "");
    }
    return -1;
}

static const char *request_string(yyjson_val *root, const char *key)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    return yyjson_is_str(value) ? yyjson_get_str(value) : NULL;
}

static bool request_uint(yyjson_val *root, const char *key, uint64_t *out)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    if (yyjson_is_uint(value)) {
        *out = yyjson_get_uint(value);
        return true;
    }
    if (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0) {
        *out = (uint64_t)yyjson_get_sint(value);
        return true;
    }
    return false;
}

static response response_new(const char *operation)
{
    response result = { .doc = yyjson_mut_doc_new(NULL), .ok = true };
    result.root = yyjson_mut_obj(result.doc);
    yyjson_mut_doc_set_root(result.doc, result.root);
    yyjson_mut_obj_add_uint(result.doc, result.root, "contract", BLUEPRINTS_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_strcpy(result.doc, result.root, "operation", operation ? operation : "");
    return result;
}

static void response_error(response *result, int *exit_code, int code, const char *message)
{
    result->ok = false;
    yyjson_mut_val *error = yyjson_mut_obj_add_obj(result->doc, result->root, "error");
    yyjson_mut_obj_add_strcpy(
        result->doc,
        error,
        "code",
        code == BLUEPRINTS_AUTOMATION_INVALID_REQUEST    ? "invalid_request"
        : code == BLUEPRINTS_AUTOMATION_IO_ERROR         ? "io_error"
        : code == BLUEPRINTS_AUTOMATION_VALIDATION_ERROR ? "validation_failed"
        : code == BLUEPRINTS_AUTOMATION_NOT_FOUND        ? "asset_not_found"
                                                         : "execution_failed"
    );
    yyjson_mut_obj_add_strcpy(result->doc, error, "message", message ? message : "unknown error");
    if (exit_code && *exit_code == BLUEPRINTS_AUTOMATION_OK) {
        *exit_code = code;
    }
}

static char *response_finish(response *result)
{
    yyjson_mut_obj_add_bool(result->doc, result->root, "ok", result->ok);
    char *json = yyjson_mut_write(result->doc, 0, NULL);
    yyjson_mut_doc_free(result->doc);
    return json ? json : xstrdup("{\"contract\":1,\"ok\":false}");
}

static char *read_file(const char *path, size_t *bytes, uint64_t *hash, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return set_errorf(error, error_size, "could not open %s", path), NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return set_error(error, error_size, "could not seek asset"), NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return set_error(error, error_size, "could not measure asset"), NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    size_t read = fread(text, 1, (size_t)length, file);
    int close_result = fclose(file);
    if (read != (size_t)length || close_result != 0) {
        free(text);
        return set_error(error, error_size, "could not read asset"), NULL;
    }
    text[read] = '\0';
    if (bytes) {
        *bytes = read;
    }
    if (hash) {
        uint64_t value = UINT64_C(1469598103934665603);
        for (size_t i = 0; i < read; i++) {
            value ^= (unsigned char)text[i];
            value *= UINT64_C(1099511628211);
        }
        *hash = value;
    }
    return text;
}

static char *root_path(const char *requested, const char *default_root, char *error, size_t error_size)
{
    const char *candidate = requested && requested[0]
                              ? requested
                              : (default_root && default_root[0] ? default_root : ".");
    char *resolved = realpath(candidate, NULL);
    if (!resolved) {
        set_errorf(error, error_size, "project root does not exist: %s", candidate);
        return NULL;
    }
    struct stat info;
    if (stat(resolved, &info) != 0 || !S_ISDIR(info.st_mode)) {
        free(resolved);
        set_error(error, error_size, "project root is not a directory");
        return NULL;
    }
    return resolved;
}

static int resolve_asset_ref(yyjson_val *root, asset_ref *out, char *error, size_t error_size)
{
    const char *path = request_string(root, "path");
    if (path) {
        if (!valid_relative_asset_path(path) || strncmp(path, "content/", 8)) {
            return set_error(error, error_size, "asset path must stay below content");
        }
        const char *folder = path + 8;
        const char *slash = strchr(folder, '/');
        if (!slash || strchr(slash + 1, '/')) {
            set_error(error, error_size, "asset not found");
            return -2;
        }
        char folder_name[64];
        size_t folder_length = (size_t)(slash - folder);
        if (folder_length >= sizeof folder_name) {
            return set_error(error, error_size, "asset folder name is too long");
        }
        memcpy(folder_name, folder, folder_length);
        folder_name[folder_length] = '\0';
        out->family = family_by_kind(folder_name);
        if (!out->family) {
            set_error(error, error_size, "asset not found");
            return -2;
        }
        if (!valid_asset_filename(slash + 1)) {
            return set_error(error, error_size, "unknown or invalid asset path");
        }
        if (strlen(slash + 1) >= sizeof out->filename ||
            strlen(path) >= sizeof out->relative) {
            return set_error(error, error_size, "asset path is too long");
        }
        snprintf(out->filename, sizeof out->filename, "%s", slash + 1);
        snprintf(out->relative, sizeof out->relative, "%s", path);
        return 0;
    }

    const char *kind = request_string(root, "kind");
    const char *name = request_string(root, "name");
    out->family = family_by_kind(kind);
    if (!out->family || !name || !name[0] || strchr(name, '/') ||
        strchr(name, '\\') || strstr(name, "..")) {
        return set_error(error, error_size, "asset requires a known kind and safe name");
    }
    if (strstr(name, ".json")) {
        if (!valid_asset_filename(name) || strlen(name) >= sizeof out->filename) {
            return set_error(error, error_size, "asset name is invalid");
        }
        snprintf(out->filename, sizeof out->filename, "%s", name);
    } else if (strlen(name) + sizeof ".json" >= sizeof out->filename) {
        return set_error(error, error_size, "asset name is too long");
    } else {
        snprintf(out->filename, sizeof out->filename, "%s.json", name);
    }
    snprintf(out->relative, sizeof out->relative, "content/%s/%s", out->family->folder, out->filename);
    return 0;
}

static char *asset_full_path(const char *root, const asset_ref *ref)
{
    size_t size = strlen(root) + strlen(ref->relative) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", root, ref->relative);
    return path;
}

/* Asset paths are rooted at a real project directory. Do not follow a
 * symlink at either content level: a local MCP client must not be able to
 * turn a project-relative asset path into an arbitrary filesystem read. */
static int verify_asset_directory(const char *root, const asset_family *family, char *error, size_t error_size)
{
    size_t content_size = strlen(root) + strlen("/content") + 1;
    char *content_path = xmalloc(content_size);
    snprintf(content_path, content_size, "%s/content", root);
    struct stat info;
    if (lstat(content_path, &info) != 0) {
        int missing = errno == ENOENT;
        free(content_path);
        if (missing) {
            return 0;
        }
        return set_errorf(error, error_size, "could not inspect %s", "content"), -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        free(content_path);
        return set_error(error, error_size, "content is not a directory"), -1;
    }
    free(content_path);

    size_t directory_size = strlen(root) + strlen("/content/") +
                            strlen(family->folder) + 1;
    char *directory_path = xmalloc(directory_size);
    snprintf(directory_path, directory_size, "%s/content/%s", root, family->folder);
    if (lstat(directory_path, &info) != 0) {
        int missing = errno == ENOENT;
        free(directory_path);
        if (missing) {
            return 0;
        }
        return set_errorf(error, error_size, "could not inspect %s", family->folder),
             -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        free(directory_path);
        return set_errorf(
                   error,
                   error_size,
                   "asset directory is not a directory: %s",
                   family->folder
               ),
             -1;
    }
    free(directory_path);
    return 1;
}

static int asset_entry_compare(const void *left, const void *right)
{
    const asset_entry *a = left;
    const asset_entry *b = right;
    return strcmp(a->relative, b->relative);
}

static void free_asset_entries(asset_entry *entries)
{
    for (int i = 0; i < acount(entries); i++) {
        free(entries[i].filename);
        free(entries[i].relative);
    }
    afree(entries);
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size)
{
    asset_entry *entries = NULL;
    for (int family_index = 0;
         family_index < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES);
         family_index++) {
        const asset_family *family = &ASSET_FAMILIES[family_index];
        int directory_status = verify_asset_directory(root, family, error, error_size);
        if (directory_status < 0) {
            free_asset_entries(entries);
            return -1;
        }
        if (directory_status == 0) {
            continue;
        }
        size_t directory_size = strlen(root) + strlen("/content/") +
                                strlen(family->folder) + 1;
        char *directory_path = xmalloc(directory_size);
        snprintf(directory_path, directory_size, "%s/content/%s", root, family->folder);
        DIR *directory = opendir(directory_path);
        if (!directory) {
            free(directory_path);
            free_asset_entries(entries);
            return set_errorf(
                error,
                error_size,
                "could not enumerate %s",
                family->folder
            );
        }
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (entry->d_name[0] == '.' || !valid_asset_filename(entry->d_name)) {
                continue;
            }
            size_t full_size = strlen(directory_path) + strlen(entry->d_name) + 2;
            char *full_path = xmalloc(full_size);
            snprintf(full_path, full_size, "%s/%s", directory_path, entry->d_name);
            struct stat info;
            int regular = lstat(full_path, &info) == 0 && S_ISREG(info.st_mode);
            if (!regular) {
                free(full_path);
                continue;
            }
            size_t bytes = 0;
            uint64_t hash = 0;
            char read_error[160] = { 0 };
            char *contents = read_file(full_path, &bytes, &hash, read_error, sizeof read_error);
            free(contents);
            if (!contents) {
                free(full_path);
                closedir(directory);
                free(directory_path);
                free_asset_entries(entries);
                return set_error(error, error_size, read_error);
            }
            asset_entry item = {
                .family = family,
                .filename = xstrdup(entry->d_name),
                .relative = xmalloc(
                    strlen("content/") + strlen(family->folder) + strlen(entry->d_name) + 2
                ),
                .bytes = bytes,
                .hash = hash,
            };
            snprintf(
                item.relative,
                strlen("content/") + strlen(family->folder) + strlen(entry->d_name) + 2,
                "content/%s/%s",
                family->folder,
                entry->d_name
            );
            apush(entries, item);
            free(full_path);
        }
        closedir(directory);
        free(directory_path);
    }
    qsort(entries, (size_t)acount(entries), sizeof *entries, asset_entry_compare);
    *out = entries;
    return 0;
}

static void add_hash(yyjson_mut_doc *doc, yyjson_mut_val *object, uint64_t hash)
{
    char text[32];
    snprintf(text, sizeof text, "%016llx", (unsigned long long)hash);
    yyjson_mut_obj_add_strcpy(doc, object, "hash", text);
}

static yyjson_mut_val *add_asset_entry(yyjson_mut_doc *doc, yyjson_mut_val *array, const asset_entry *entry)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    size_t name_size = strlen(entry->filename) - (sizeof ".json" - 1) + 1;
    char *name = xmalloc(name_size);
    memcpy(name, entry->filename, name_size - 1);
    name[name_size - 1] = '\0';
    yyjson_mut_obj_add_strcpy(doc, object, "kind", entry->family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", entry->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", entry->bytes);
    add_hash(doc, object, entry->hash);
    free(name);
    return object;
}

static const char *pin_json_name(pin_type type)
{
    const char *name = pin_type_name(type);
    return name ? name : "flow";
}

static const char *container_json_name(pin_container container)
{
    switch (container) {
        case PIN_CONTAINER_SINGLE: return "single";
        case PIN_CONTAINER_ARRAY: return "array";
        case PIN_CONTAINER_SET: return "set";
        case PIN_CONTAINER_MAP: return "map";
    }
    return "invalid";
}

static void add_pin(yyjson_mut_doc *doc, yyjson_mut_val *array, const pin_spec *pin)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "name", pin->name ? pin->name : "");
    yyjson_mut_obj_add_strcpy(doc, object, "type", pin_json_name(pin->type));
    yyjson_mut_obj_add_strcpy(doc, object, "kind", pin->kind == PIN_EXEC ? "exec" : "data");
    yyjson_mut_obj_add_strcpy(doc, object, "container", container_json_name(pin->container));
    yyjson_mut_obj_add_uint(doc, object, "id", pin->id);
    yyjson_mut_obj_add_uint(doc, object, "flags", pin->flags);
    yyjson_mut_obj_add_int(doc, object, "group", pin->group);
    if (pin->type_name) {
        yyjson_mut_obj_add_strcpy(doc, object, "type_name", pin->type_name);
    }
    if (pin->field_name) {
        yyjson_mut_obj_add_strcpy(doc, object, "field_name", pin->field_name);
    }
    if (pin->field_source) {
        yyjson_mut_obj_add_uint(doc, object, "field_source", pin->field_source);
    }
}

static yyjson_mut_val *add_pins(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const pin_spec *pins,
    int count
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        add_pin(doc, array, &pins[i]);
    }
    return array;
}

static void add_type(yyjson_mut_doc *doc, yyjson_mut_val *array, const type_desc *type)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "name", type->name ? type->name : "");
    yyjson_mut_obj_add_uint(doc, object, "bytes", type->size);
    if (type->alias_of) {
        yyjson_mut_obj_add_strcpy(doc, object, "alias_of", type->alias_of);
    }
    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, fields);
        yyjson_mut_obj_add_strcpy(doc, entry, "name", field->name ? field->name : "");
        yyjson_mut_obj_add_strcpy(doc, entry, "type", pin_json_name(field->type));
        yyjson_mut_obj_add_uint(doc, entry, "offset", field->offset);
        yyjson_mut_obj_add_uint(doc, entry, "bytes", field->size);
        if (field->type_name) {
            yyjson_mut_obj_add_strcpy(doc, entry, "type_name", field->type_name);
        }
    }
}

static void add_registry(yyjson_mut_doc *doc, yyjson_mut_val *object)
{
    yyjson_mut_obj_add_int(doc, object, "packs", REGISTRY_PACK_COUNT);
    yyjson_mut_obj_add_int(doc, object, "nodes", registry_node_count());
    yyjson_mut_obj_add_int(doc, object, "types", registry_type_count());
    yyjson_mut_obj_add_int(doc, object, "services", registry_service_count());
    yyjson_mut_obj_add_int(doc, object, "operators", registry_operator_count());
    yyjson_mut_obj_add_int(doc, object, "components", GAME_REGISTRY_COMPONENT_COUNT);
}

static void add_diagnostic(yyjson_mut_doc *doc, yyjson_mut_val *array, const compile_diagnostic *diagnostic)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "severity", diagnostic->severity == CMPL_ERROR ? "error" : "warning");
    yyjson_mut_obj_add_uint(doc, object, "graph", diagnostic->graph_id);
    yyjson_mut_obj_add_uint(doc, object, "node", diagnostic->node_id);
    yyjson_mut_obj_add_uint(doc, object, "pin", diagnostic->pin_id);
    yyjson_mut_obj_add_int(doc, object, "rule", diagnostic->rule);
    yyjson_mut_obj_add_strcpy(
        doc,
        object,
        "rule_name",
        cmpl_rule_name(diagnostic->rule)
    );
    yyjson_mut_obj_add_strcpy(doc, object, "message", diagnostic->message);
}

static void add_load_diagnostic(yyjson_mut_doc *doc, yyjson_mut_val *array, const char *message)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "severity", "error");
    yyjson_mut_obj_add_strcpy(doc, object, "rule_name", "LOAD");
    yyjson_mut_obj_add_strcpy(doc, object, "message", message ? message : "asset rejected");
}

static void add_asset_identity(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const asset_ref *ref,
    size_t bytes,
    uint64_t hash
)
{
    size_t name_size = strlen(ref->filename) - (sizeof ".json" - 1) + 1;
    char *name = xmalloc(name_size);
    memcpy(name, ref->filename, name_size - 1);
    name[name_size - 1] = '\0';
    yyjson_mut_obj_add_strcpy(doc, object, "kind", ref->family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", ref->relative);
    yyjson_mut_obj_add_uint(doc, object, "bytes", bytes);
    add_hash(doc, object, hash);
    free(name);
}

static int handle_status(const char *root, response *result, int *exit_code)
{
    asset_entry *entries = NULL;
    char error[256] = { 0 };
    if (enumerate_assets(root, &entries, error, sizeof error) != 0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_root", "content");
    yyjson_mut_obj_add_uint(result->doc, value, "assets", (uint64_t)acount(entries));
    yyjson_mut_val *families = yyjson_mut_obj_add_obj(result->doc, value, "families");
    for (int family_index = 0;
         family_index < (int)(sizeof ASSET_FAMILIES / sizeof *ASSET_FAMILIES);
         family_index++) {
        int count = 0;
        for (int i = 0; i < acount(entries); i++) {
            count += entries[i].family == &ASSET_FAMILIES[family_index];
        }
        yyjson_mut_obj_add_int(
            result->doc,
            families,
            ASSET_FAMILIES[family_index].kind,
            count
        );
    }
    yyjson_mut_val *registry = yyjson_mut_obj_add_obj(result->doc, value, "registry");
    add_registry(result->doc, registry);
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result->doc, value, "operations");
    const char *const names[] = {
        "status",         "list_assets",       "read_asset",
        "validate_asset", "describe_registry",
    };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        yyjson_mut_arr_add_strcpy(result->doc, operations, names[i]);
    }
    free_asset_entries(entries);
    return 0;
}

static int handle_list_assets(const char *root, response *result, int *exit_code)
{
    asset_entry *entries = NULL;
    char error[256] = { 0 };
    if (enumerate_assets(root, &entries, error, sizeof error) != 0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    yyjson_mut_val *assets = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (int i = 0; i < acount(entries); i++) {
        add_asset_entry(result->doc, assets, &entries[i]);
    }
    free_asset_entries(entries);
    return 0;
}

static int load_asset(
    const char *root,
    yyjson_val *request,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
)
{
    int ref_status = resolve_asset_ref(request, ref, error, error_size);
    if (ref_status != 0) {
        return ref_status == -2 ? BLUEPRINTS_AUTOMATION_NOT_FOUND
                                : BLUEPRINTS_AUTOMATION_INVALID_REQUEST;
    }
    int directory_status = verify_asset_directory(root, ref->family, error, error_size);
    if (directory_status <= 0) {
        if (directory_status == 0) {
            set_errorf(error, error_size, "asset not found: %s", ref->relative);
            return BLUEPRINTS_AUTOMATION_NOT_FOUND;
        }
        return BLUEPRINTS_AUTOMATION_IO_ERROR;
    }
    char *path = asset_full_path(root, ref);
    struct stat info;
    if (lstat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        free(path);
        return set_errorf(error, error_size, "asset not found: %s", ref->relative),
               BLUEPRINTS_AUTOMATION_NOT_FOUND;
    }
    *json = read_file(path, bytes, hash, error, error_size);
    free(path);
    return *json ? BLUEPRINTS_AUTOMATION_OK : BLUEPRINTS_AUTOMATION_IO_ERROR;
}

static int handle_read_asset(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != BLUEPRINTS_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    if (!document) {
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "asset is not valid JSON");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "document",
        yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(document))
    );
    yyjson_doc_free(document);
    free(json);
    return 0;
}

static int handle_validate_asset(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    asset_ref ref = { 0 };
    char *json = NULL;
    size_t bytes = 0;
    uint64_t hash = 0;
    char error[256] = { 0 };
    int status = load_asset(root, request, &ref, &json, &bytes, &hash, error, sizeof error);
    if (status != BLUEPRINTS_AUTOMATION_OK) {
        response_error(result, exit_code, status, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
    int errors = 0;
    int warnings = 0;
    if (ref.family->loader == ASSET_GRAPH) {
        behavior_asset *asset = behavior_asset_load_json(json);
        if (!asset) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, graph_last_error());
        } else {
            compile_report report;
            compile_report_init(&report);
            errors = behavior_asset_compile(asset, &report);
            warnings = report.warning_count;
            for (int i = 0; i < acount(report.items); i++) {
                add_diagnostic(result->doc, diagnostics, &report.items[i]);
            }
            compile_report_free(&report);
            behavior_asset_free(asset);
        }
    } else if (ref.family->loader == ASSET_PROTOTYPE) {
        game_prototype_registry registry = {
            .components = GAME_REGISTRY_COMPONENTS,
            .component_count = GAME_REGISTRY_COMPONENT_COUNT,
        };
        char load_error[256] = { 0 };
        game_prototype *prototype = game_prototype_load(json, &registry, load_error, sizeof load_error);
        if (!prototype) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            yyjson_mut_obj_add_strcpy(result->doc, value, "identity", prototype->identity);
            yyjson_mut_obj_add_int(
                result->doc,
                value,
                "components",
                acount(prototype->components)
            );
            game_prototype_free(prototype);
        }
    } else {
        char load_error[256] = { 0 };
        game_scene_document *scene = game_scene_document_load(json, load_error, sizeof load_error);
        if (!scene) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            yyjson_mut_obj_add_strcpy(result->doc, value, "scene_name", scene->name);
            yyjson_mut_obj_add_int(result->doc, value, "entities", acount(scene->entities));
            yyjson_mut_obj_add_bool(result->doc, value, "perspective", scene->perspective);
            game_scene_document_free(scene);
        }
    }
    yyjson_mut_obj_add_bool(result->doc, value, "valid", errors == 0);
    yyjson_mut_obj_add_int(result->doc, value, "errors", errors);
    yyjson_mut_obj_add_int(result->doc, value, "warnings", warnings);
    free(json);
    if (errors) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "asset validation failed");
        return -1;
    }
    return 0;
}

static int handle_describe_registry(response *result)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(result->doc, value, "nodes");
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *node = registry_node(i);
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, nodes);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", node->name);
        yyjson_mut_obj_add_bool(result->doc, object, "pure", node_desc_is_pure(node));
        yyjson_mut_obj_add_int(result->doc, object, "display_class", node->display_class);
        if (node->category) {
            yyjson_mut_obj_add_strcpy(result->doc, object, "category", node->category);
        }
        if (node->keywords) {
            yyjson_mut_obj_add_strcpy(result->doc, object, "keywords", node->keywords);
        }
        add_pins(result->doc, object, "inputs", node->inputs, node->input_count);
        add_pins(result->doc, object, "outputs", node->outputs, node->output_count);
    }
    yyjson_mut_val *types = yyjson_mut_obj_add_arr(result->doc, value, "types");
    for (int i = 0; i < registry_type_count(); i++) {
        add_type(result->doc, types, registry_type(i));
    }
    yyjson_mut_val *services = yyjson_mut_obj_add_arr(result->doc, value, "services");
    for (int i = 0; i < registry_service_count(); i++) {
        const service_desc *service = registry_service(i);
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, services);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", service->name);
        yyjson_mut_obj_add_uint(result->doc, object, "stable_id", service->stable_id);
    }
    yyjson_mut_val *operators = yyjson_mut_obj_add_arr(result->doc, value, "operators");
    for (int i = 0; i < registry_operator_count(); i++) {
        const operator_signature *signature = registry_operator(i);
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, operators);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", signature->name);
        yyjson_mut_obj_add_strcpy(
            result->doc,
            object,
            "left",
            pin_json_name(signature->left)
        );
        yyjson_mut_obj_add_strcpy(
            result->doc,
            object,
            "right",
            pin_json_name(signature->right)
        );
        yyjson_mut_obj_add_strcpy(
            result->doc,
            object,
            "result",
            pin_json_name(signature->result)
        );
        yyjson_mut_obj_add_int(result->doc, object, "priority", signature->priority);
    }
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(result->doc, value, "components");
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, components);
        yyjson_mut_obj_add_strcpy(result->doc, object, "key", component->key);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", component->name);
        yyjson_mut_obj_add_uint(result->doc, object, "id", component->id);
        yyjson_mut_obj_add_uint(result->doc, object, "bytes", component->element_size);
        yyjson_mut_obj_add_uint(result->doc, object, "layout_hash", component->layout_hash);
        if (component->type) {
            yyjson_mut_obj_add_strcpy(
                result->doc,
                object,
                "type",
                component->type->name
            );
        }
    }
    return 0;
}

static int operation_requires_root(const char *operation)
{
    return !strcmp(operation, "status") || !strcmp(operation, "list_assets") ||
           !strcmp(operation, "read_asset") || !strcmp(operation, "validate_asset");
}

char *blueprints_automation_handle(const char *request, const char *default_root, int *exit_code)
{
    if (exit_code) {
        *exit_code = BLUEPRINTS_AUTOMATION_OK;
    }
    yyjson_doc *document = request ? yyjson_read(request, strlen(request), 0) : NULL;
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document))) {
        response result = response_new("");
        response_error(&result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "request must be a JSON object");
        if (document) {
            yyjson_doc_free(document);
        }
        return response_finish(&result);
    }
    yyjson_val *root_value = yyjson_doc_get_root(document);
    uint64_t contract = 0;
    const char *operation = request_string(root_value, "operation");
    response result = response_new(operation);
    if (!request_uint(root_value, "contract", &contract) ||
        contract != BLUEPRINTS_AUTOMATION_CONTRACT) {
        response_error(&result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "unsupported or missing contract");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    if (!operation || !operation[0]) {
        response_error(&result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "operation is required");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    if (strcmp(operation, "describe_registry") && strcmp(operation, "status") &&
        strcmp(operation, "list_assets") && strcmp(operation, "read_asset") &&
        strcmp(operation, "validate_asset")) {
        response_error(&result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "unknown operation");
        yyjson_doc_free(document);
        return response_finish(&result);
    }
    char *root = NULL;
    if (operation_requires_root(operation)) {
        char error[256] = { 0 };
        root = root_path(request_string(root_value, "project_root"), default_root, error, sizeof error);
        if (!root) {
            response_error(&result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
            yyjson_doc_free(document);
            return response_finish(&result);
        }
    }
    if (!strcmp(operation, "status")) {
        handle_status(root, &result, exit_code);
    } else if (!strcmp(operation, "list_assets")) {
        handle_list_assets(root, &result, exit_code);
    } else if (!strcmp(operation, "read_asset")) {
        handle_read_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "validate_asset")) {
        handle_validate_asset(root, root_value, &result, exit_code);
    } else {
        handle_describe_registry(&result);
    }
    free(root);
    yyjson_doc_free(document);
    return response_finish(&result);
}

static const char *legacy_operation(const char *method)
{
    if (!method) {
        return NULL;
    }
    if (!strcmp(method, "project.status")) {
        return "status";
    }
    if (!strcmp(method, "assets.list")) {
        return "list_assets";
    }
    if (!strcmp(method, "asset.read")) {
        return "read_asset";
    }
    if (!strcmp(method, "asset.validate")) {
        return "validate_asset";
    }
    return NULL;
}

static char *legacy_request(const yyjson_val *input, const char *operation, const char *root)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, object);
    yyjson_mut_obj_add_uint(doc, object, "contract", BLUEPRINTS_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_strcpy(doc, object, "operation", operation ? operation : "");
    if (root && root[0]) {
        yyjson_mut_obj_add_strcpy(doc, object, "project_root", root);
    }
    const yyjson_val *params = yyjson_obj_get(input, "params");
    const char *path = params ? request_string((yyjson_val *)params, "path") : NULL;
    if (path) {
        yyjson_mut_obj_add_strcpy(doc, object, "path", path);
    }
    char *request = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return request;
}

static void legacy_aliases(yyjson_mut_doc *doc, yyjson_mut_val *root, const char *method)
{
    yyjson_mut_val *result = yyjson_mut_obj_get(root, "result");
    if (!result || !strcmp(method, "project.status")) {
        if (result && !strcmp(method, "project.status")) {
            yyjson_mut_val *project = yyjson_mut_obj_add_obj(doc, result, "project");
            yyjson_mut_obj_add_strcpy(doc, project, "name", "c-blueprints");
            yyjson_mut_val *registry = yyjson_mut_obj_get(result, "registry");
            yyjson_mut_val *nodes = registry ? yyjson_mut_obj_get(registry, "nodes")
                                             : NULL;
            yyjson_mut_obj_add_uint(
                doc,
                result,
                "reflection_node_count",
                nodes && yyjson_mut_is_int(nodes) ? yyjson_mut_get_uint(nodes) : 0
            );
        }
        return;
    }
    if (!strcmp(method, "asset.read")) {
        yyjson_mut_val *document = yyjson_mut_obj_get(result, "document");
        if (document) {
            yyjson_mut_obj_add_val(doc, result, "asset", yyjson_mut_val_mut_copy(doc, document));
        }
    }
}

static char *legacy_response(const char *response_json, const char *id, const char *method, int *status)
{
    yyjson_doc *document = yyjson_read(response_json, strlen(response_json), 0);
    if (!document) {
        return xstrdup("{\"version\":1,\"ok\":false,\"error\":{\"code\":\"execution_failed\"}}");
    }
    yyjson_mut_doc *mutable = yyjson_doc_mut_copy(document, NULL);
    yyjson_doc_free(document);
    if (!mutable) {
        return xstrdup("{\"version\":1,\"ok\":false,\"error\":{\"code\":\"execution_failed\"}}");
    }
    yyjson_mut_val *root = yyjson_mut_doc_get_root(mutable);
    yyjson_mut_obj_add_uint(mutable, root, "version", BLUEPRINTS_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_strcpy(mutable, root, "id", id ? id : "");
    yyjson_mut_obj_add_strcpy(mutable, root, "method", method ? method : "");
    legacy_aliases(mutable, root, method ? method : "");
    yyjson_mut_val *error = yyjson_mut_obj_get(root, "error");
    if (error && status && *status == BLUEPRINTS_AUTOMATION_INVALID_REQUEST) {
        yyjson_mut_obj_remove_str(error, "code");
        yyjson_mut_obj_add_strcpy(mutable, error, "code", "invalid_json");
    }
    char *result = yyjson_mut_write(mutable, 0, NULL);
    yyjson_mut_doc_free(mutable);
    return result ? result : xstrdup("{\"version\":1,\"ok\":false}");
}

char *blueprints_ctl_handle(const blueprints_ctl_context *context, const char *request, int *exit_code)
{
    const char *root = context && context->root ? context->root : ".";
    yyjson_doc *document = request ? yyjson_read(request, strlen(request), 0) : NULL;
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document))) {
        if (document) {
            yyjson_doc_free(document);
        }
        char *response = blueprints_automation_handle(request, root, exit_code);
        char *legacy = legacy_response(response, "", "", exit_code);
        free(response);
        return legacy;
    }
    yyjson_val *input = yyjson_doc_get_root(document);
    const char *id = request_string(input, "id");
    const char *method = request_string(input, "method");
    const char *operation = legacy_operation(method);
    char *canonical = legacy_request(input, operation, root);
    yyjson_doc_free(document);
    int automation_status = BLUEPRINTS_AUTOMATION_OK;
    char *response = blueprints_automation_handle(canonical, root, &automation_status);
    free(canonical);
    if (exit_code) {
        *exit_code = automation_status;
    }
    char *legacy = legacy_response(response, id, method, &automation_status);
    free(response);
    return legacy;
}
