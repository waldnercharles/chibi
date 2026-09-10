#include "automation/controller.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "assets/system_document.h"
#include "ecs/ecs.h"
#include "graph/compiler/compile.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <ctype.h>
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
    ASSET_SYSTEM,
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
    { "system", "systems", ASSET_SYSTEM },
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
    char *kind;
    char *name;
    bool resolved;
} catalog_reference;

typedef struct
{
    char *kind;
    char *name;
} catalog_member;

typedef struct
{
    char *severity;
    int rule;
    char *rule_name;
    char *message;
} catalog_diagnostic;

typedef struct
{
    asset_entry asset;
    char *name;
    int schema_version;
    char *description;
    bool valid;
    int errors;
    int warnings;
    catalog_member *members;
    catalog_reference *dependencies;
    catalog_reference *dependents;
    catalog_diagnostic *diagnostics;
} catalog_asset;

typedef struct
{
    char *root;
    asset_entry *entries;
    catalog_asset *assets;
    uint64_t revision;
    uint64_t content_hash;
} catalog_snapshot;

static catalog_snapshot CATALOG_SNAPSHOT;

typedef struct
{
    yyjson_val *value;
    uint64_t id;
} inspect_json_item;

typedef struct
{
    yyjson_mut_doc *doc;
    yyjson_mut_val *root;
    bool ok;
} response;

typedef struct
{
    const char *name;
    const char *type;
    bool required;
    const char *description;
} contract_field;

typedef struct
{
    const char *name;
    const char *summary;
    const contract_field *fields;
    int field_count;
    int response_version;
    bool read_only;
    bool idempotent;
    bool destructive;
} operation_descriptor;

typedef struct
{
    uint64_t cursor;
    uint64_t limit;
    bool has_limit;
    bool summary;
} page_options;

#define CONTRACT_FIELD_COUNT(fields) ((int)(sizeof(fields) / sizeof *(fields)))

static const contract_field CONTRACT_NO_FIELDS[1] = { { NULL, NULL, false, NULL } };
static const contract_field CONTRACT_PROJECT_ROOT[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
};
static const contract_field CONTRACT_LIST_ASSETS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
    { "summary", "boolean", false, "Return compact asset records without relationship details." },
};
static const contract_field CONTRACT_ASSET_REFERENCE[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative asset path, alternatively use kind and name." },
    { "kind", "string", false, "Asset family, required with name when path is omitted." },
    { "name", "string", false, "Asset name, required with kind when path is omitted." },
};
static const contract_field CONTRACT_SEARCH_ASSETS[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "query", "string", false, "Case-insensitive text matched against asset metadata." },
    { "kinds", "string[]", false, "Asset families to include in the search." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
    { "summary", "boolean", false, "Return identity and validation fields without relationship details." },
};
static const contract_field CONTRACT_CATALOG[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "cursor", "uint64", false, "Zero-based result offset; defaults to zero." },
    { "limit", "uint64", false, "Maximum number of results; must be greater than zero." },
    { "summary", "boolean", false, "Return identity and validation fields without relationship details." },
};
static const contract_field CONTRACT_ASSET_DEPENDENCIES[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "kind", "string", true, "Known asset family." },
    { "name", "string", true, "Asset name." },
    { "direction", "string", false, "Traversal direction; defaults to outgoing." },
    { "transitive", "boolean", false, "Traverse beyond direct dependencies; defaults to false." },
};
static const contract_field CONTRACT_INSPECT_GRAPH[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative graph asset path, alternatively use kind and name." },
    { "kind", "string", false, "Graph asset family, required with name when path is omitted." },
    { "name", "string", false, "Graph asset name, required with kind when path is omitted." },
    { "graph_id", "uint64", false, "Select one graph by its stable authored ID." },
    { "graph_name", "string", false, "Select one graph by its authored name." },
};
static const contract_field CONTRACT_INSPECT_SYSTEM[] = {
    { "project_root", "string", false, "Project directory; defaults to the controller root." },
    { "path", "string", false, "Content-relative system asset path, alternatively use kind and name." },
    { "kind", "string", false, "System asset family, required with name when path is omitted." },
    { "name", "string", false, "System asset name, required with kind when path is omitted." },
};

static const operation_descriptor CONTRACT_OPERATIONS[] = {
    {
        "describe_contract",
        "Describe the native automation contract and reflected capabilities.",
        CONTRACT_NO_FIELDS,
        0,
        1,
        true,
        true,
        false,
    },
    {
        "status",
        "Return project, asset, reflection, and operation summary information.",
        CONTRACT_PROJECT_ROOT,
        CONTRACT_FIELD_COUNT(CONTRACT_PROJECT_ROOT),
        1,
        true,
        true,
        false,
    },
    {
        "list_assets",
        "List authored JSON assets and their stable content identities.",
        CONTRACT_LIST_ASSETS,
        CONTRACT_FIELD_COUNT(CONTRACT_LIST_ASSETS),
        1,
        true,
        true,
        false,
    },
    {
        "read_asset",
        "Read one authored asset as normalized JSON data.",
        CONTRACT_ASSET_REFERENCE,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_REFERENCE),
        1,
        true,
        true,
        false,
    },
    {
        "validate_asset",
        "Load and validate one authored asset.",
        CONTRACT_ASSET_REFERENCE,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_REFERENCE),
        1,
        true,
        true,
        false,
    },
    {
        "describe_registry",
        "Return reflected graph, type, service, operator, and component "
        "metadata.",
        CONTRACT_NO_FIELDS,
        0,
        1,
        true,
        true,
        false,
    },
    {
        "catalog",
        "Return the semantic asset catalog and reflected registry summary.",
        CONTRACT_CATALOG,
        CONTRACT_FIELD_COUNT(CONTRACT_CATALOG),
        1,
        true,
        true,
        false,
    },
    {
        "search_assets",
        "Search the semantic asset catalog by metadata and family.",
        CONTRACT_SEARCH_ASSETS,
        CONTRACT_FIELD_COUNT(CONTRACT_SEARCH_ASSETS),
        1,
        true,
        true,
        false,
    },
    {
        "asset_dependencies",
        "Inspect direct or transitive asset dependency relationships.",
        CONTRACT_ASSET_DEPENDENCIES,
        CONTRACT_FIELD_COUNT(CONTRACT_ASSET_DEPENDENCIES),
        1,
        true,
        true,
        false,
    },
    {
        "inspect_graph",
        "Inspect normalized graph nodes, pins, edges, calls, effects, and "
        "diagnostics.",
        CONTRACT_INSPECT_GRAPH,
        CONTRACT_FIELD_COUNT(CONTRACT_INSPECT_GRAPH),
        1,
        true,
        true,
        false,
    },
    {
        "inspect_system",
        "Inspect a system asset's canonical query, graph summary, and "
        "diagnostics.",
        CONTRACT_INSPECT_SYSTEM,
        CONTRACT_FIELD_COUNT(CONTRACT_INSPECT_SYSTEM),
        1,
        true,
        true,
        false,
    },
};

static const char *const CONTRACT_ERROR_CATEGORIES[] = {
    "invalid_request", "io_error",         "validation_failed",
    "asset_not_found", "execution_failed",
};

static int handle_describe_registry(response *result);
static int handle_describe_contract(response *result);
static uint64_t catalog_content_hash(const catalog_asset *assets);
static int load_asset(
    const char *root,
    yyjson_val *request,
    asset_ref *ref,
    char **json,
    size_t *bytes,
    uint64_t *hash,
    char *error,
    size_t error_size
);

static const ecs_component_desc *automation_query_component(const char *name, void *)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if ((component->name && !strcmp(component->name, name)) ||
            (component->key && !strcmp(component->key, name))) {
            return component;
        }
    }
    return NULL;
}

static const char *automation_query_component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key ? component->key : component->name;
        }
    }
    return NULL;
}

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

static void response_error(response *result, int *exit_code, int code, const char *message);

static bool request_page(yyjson_val *request, page_options *page, response *result, int *exit_code)
{
    *page = (page_options){ 0 };
    yyjson_val *cursor = yyjson_obj_get(request, "cursor");
    if (cursor && !request_uint(request, "cursor", &page->cursor)) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "cursor must be a non-negative integer");
        return false;
    }
    yyjson_val *limit = yyjson_obj_get(request, "limit");
    if (limit) {
        if (!request_uint(request, "limit", &page->limit) || page->limit == 0) {
            response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "limit must be a positive integer");
            return false;
        }
        page->has_limit = true;
    }
    yyjson_val *summary = yyjson_obj_get(request, "summary");
    if (summary) {
        if (!yyjson_is_bool(summary)) {
            response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "summary must be a boolean");
            return false;
        }
        page->summary = yyjson_get_bool(summary);
    }
    return true;
}

static void add_page_metadata(yyjson_mut_doc *doc, yyjson_mut_val *object, int total, uint64_t cursor, uint64_t count)
{
    uint64_t total_value = (uint64_t)total;
    uint64_t start = cursor < total_value ? cursor : total_value;
    uint64_t page_count = count < total_value - start ? count : total_value - start;
    bool has_more = start + page_count < total_value;
    yyjson_mut_obj_add_uint(doc, object, "total", total_value);
    yyjson_mut_obj_add_uint(doc, object, "count", page_count);
    yyjson_mut_obj_add_bool(doc, object, "has_more", has_more);
    if (has_more) {
        yyjson_mut_obj_add_uint(doc, object, "next_cursor", start + page_count);
    } else {
        yyjson_mut_obj_add_null(doc, object, "next_cursor");
    }
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

static const operation_descriptor *operation_by_name(const char *name)
{
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        if (!strcmp(name, CONTRACT_OPERATIONS[i].name)) {
            return &CONTRACT_OPERATIONS[i];
        }
    }
    return NULL;
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
        snprintf(
            out->relative,
            sizeof out->relative,
            "content/%s/%s",
            out->family->folder,
            out->filename
        );
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

static char *asset_name_from_filename(const char *filename)
{
    size_t length = strlen(filename) - (sizeof ".json" - 1);
    char *name = xmalloc(length + 1);
    memcpy(name, filename, length);
    name[length] = '\0';
    return name;
}

static void free_catalog_references(catalog_reference *references)
{
    for (int i = 0; i < acount(references); i++) {
        free(references[i].kind);
        free(references[i].name);
    }
    afree(references);
}

static void free_catalog_assets(catalog_asset *assets)
{
    for (int i = 0; i < acount(assets); i++) {
        catalog_asset *asset = &assets[i];
        free(asset->asset.filename);
        free(asset->asset.relative);
        free(asset->name);
        free(asset->description);
        for (int j = 0; j < acount(asset->members); j++) {
            free(asset->members[j].kind);
            free(asset->members[j].name);
        }
        afree(asset->members);
        free_catalog_references(asset->dependencies);
        free_catalog_references(asset->dependents);
        for (int j = 0; j < acount(asset->diagnostics); j++) {
            free(asset->diagnostics[j].severity);
            free(asset->diagnostics[j].rule_name);
            free(asset->diagnostics[j].message);
        }
        afree(asset->diagnostics);
    }
    afree(assets);
}

static void free_catalog_snapshot(catalog_snapshot *snapshot)
{
    free(snapshot->root);
    free_asset_entries(snapshot->entries);
    free_catalog_assets(snapshot->assets);
    memset(snapshot, 0, sizeof *snapshot);
}

static int catalog_reference_compare(const void *left, const void *right)
{
    const catalog_reference *a = left;
    const catalog_reference *b = right;
    int kind = strcmp(a->kind ? a->kind : "", b->kind ? b->kind : "");
    return kind ? kind : strcmp(a->name, b->name);
}

static int catalog_member_compare(const void *left, const void *right)
{
    const catalog_member *a = left;
    const catalog_member *b = right;
    int kind = strcmp(a->kind, b->kind);
    return kind ? kind : strcmp(a->name, b->name);
}

static void catalog_member_add(catalog_asset *asset, const char *kind, const char *name)
{
    if (!name || !name[0]) {
        return;
    }
    for (int i = 0; i < acount(asset->members); i++) {
        if (!strcmp(asset->members[i].kind, kind) &&
            !strcmp(asset->members[i].name, name)) {
            return;
        }
    }
    catalog_member member = { .kind = xstrdup(kind), .name = xstrdup(name) };
    apush(asset->members, member);
}

static char *catalog_normalize_name(const char *value)
{
    if (!value || !value[0]) {
        return NULL;
    }
    const char *name = strrchr(value, '/');
    name = name ? name + 1 : value;
    size_t length = strlen(name);
    if (length > sizeof ".json" - 1 &&
        !strcmp(name + length - (sizeof ".json" - 1), ".json")) {
        length -= sizeof ".json" - 1;
    }
    if (!length) {
        return NULL;
    }
    char *copy = xmalloc(length + 1);
    memcpy(copy, name, length);
    copy[length] = '\0';
    return copy;
}

static void catalog_reference_add(catalog_asset *asset, const char *kind, const char *value)
{
    char *name = catalog_normalize_name(value);
    if (!name) {
        return;
    }
    const char *reference_kind = kind ? kind : "";
    for (int i = 0; i < acount(asset->dependencies); i++) {
        if (!strcmp(asset->dependencies[i].kind, reference_kind) &&
            !strcmp(asset->dependencies[i].name, name)) {
            free(name);
            return;
        }
    }
    catalog_reference reference = {
        .kind = xstrdup(reference_kind),
        .name = name,
        .resolved = false,
    };
    apush(asset->dependencies, reference);
}

static void catalog_scan_references(catalog_asset *asset, yyjson_val *value)
{
    if (yyjson_is_obj(value)) {
        yyjson_obj_iter iterator = yyjson_obj_iter_with(value);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iterator))) {
            const char *field = yyjson_get_str(key);
            yyjson_val *child = yyjson_obj_iter_get_val(key);
            if (yyjson_is_str(child)) {
                const char *kind = NULL;
                if (!strcmp(field, "reference_asset") ||
                    !strcmp(field, "includes") || !strcmp(field, "include")) {
                    kind = !strcmp(asset->asset.family->kind, "material") ||
                                   !strcmp(asset->asset.family->kind, "shader-library")
                             ? "shader-library"
                             : "library";
                } else if (!strcmp(field, "prototype")) {
                    kind = "prototype";
                } else if (!strcmp(field, "material")) {
                    kind = "material";
                } else if (!strcmp(field, "shape")) {
                    kind = "shape";
                }
                if (kind) {
                    catalog_reference_add(asset, kind, yyjson_get_str(child));
                }
            } else if (yyjson_is_arr(child) && (!strcmp(field, "behaviors") || !strcmp(field, "includes"))) {
                size_t index, count;
                yyjson_val *item;
                yyjson_arr_foreach(child, index, count, item)
                {
                    if (yyjson_is_str(item)) {
                        catalog_reference_add(
                            asset,
                            !strcmp(field, "behaviors") ? "behavior" : "library",
                            yyjson_get_str(item)
                        );
                    } else if (yyjson_is_obj(item)) {
                        const char *name = request_string(item, "name");
                        catalog_reference_add(
                            asset,
                            !strcmp(field, "behaviors") ? "behavior" : "library",
                            name
                        );
                    }
                }
            }
            catalog_scan_references(asset, child);
        }
    } else if (yyjson_is_arr(value)) {
        size_t index, count;
        yyjson_val *child;
        yyjson_arr_foreach(value, index, count, child)
        {
            catalog_scan_references(asset, child);
        }
    }
}

static void catalog_scan_members(catalog_asset *asset, yyjson_val *root)
{
    yyjson_val *graphs = yyjson_obj_get(root, "graphs");
    size_t index, count;
    yyjson_val *graph_value;
    yyjson_arr_foreach(graphs, index, count, graph_value)
    {
        if (!yyjson_is_obj(graph_value)) {
            continue;
        }
        const char *name = request_string(graph_value, "name");
        const char *kind = request_string(graph_value, "kind");
        catalog_member_add(asset, kind ? kind : "graph", name);
    }
    yyjson_val *variables = yyjson_obj_get(root, "variables");
    yyjson_val *variable;
    yyjson_arr_foreach(variables, index, count, variable)
    {
        if (yyjson_is_obj(variable)) {
            catalog_member_add(asset, "variable", request_string(variable, "name"));
        }
    }
    yyjson_val *components = yyjson_obj_get(root, "components");
    yyjson_val *component;
    yyjson_arr_foreach(components, index, count, component)
    {
        if (yyjson_is_obj(component)) {
            const char *name = request_string(component, "type");
            if (!name) {
                name = request_string(component, "name");
            }
            catalog_member_add(asset, "component", name);
        }
    }
    yyjson_val *entities = yyjson_obj_get(root, "entities");
    yyjson_val *entity;
    yyjson_arr_foreach(entities, index, count, entity)
    {
        if (yyjson_is_obj(entity)) {
            catalog_member_add(asset, "entity", request_string(entity, "name"));
        }
    }
    qsort(asset->members, (size_t)acount(asset->members), sizeof *asset->members, catalog_member_compare);
}

static void catalog_add_diagnostic(
    catalog_asset *asset,
    const char *severity,
    int rule,
    const char *rule_name,
    const char *message
)
{
    catalog_diagnostic diagnostic = {
        .severity = xstrdup(severity ? severity : "error"),
        .rule = rule,
        .rule_name = xstrdup(rule_name ? rule_name : "LOAD"),
        .message = xstrdup(message ? message : "asset rejected"),
    };
    apush(asset->diagnostics, diagnostic);
}

static void catalog_validate_asset(catalog_asset *asset, const char *json)
{
    const asset_family *family = asset->asset.family;
    if (family->loader == ASSET_GRAPH) {
        behavior_asset *behavior = behavior_asset_load_json(json);
        if (!behavior) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", graph_last_error());
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset_compile(behavior, &report);
            asset->errors = report.error_count;
            asset->warnings = report.warning_count;
            for (int i = 0; i < acount(report.items); i++) {
                const compile_diagnostic *diagnostic = &report.items[i];
                catalog_add_diagnostic(
                    asset,
                    diagnostic->severity == CMPL_ERROR ? "error" : "warning",
                    diagnostic->rule,
                    cmpl_rule_name(diagnostic->rule),
                    diagnostic->message
                );
            }
            compile_report_free(&report);
            behavior_asset_free(behavior);
        }
    } else if (family->loader == ASSET_SYSTEM) {
        char error[256] = { 0 };
        game_system_document *document = game_system_document_load(
            json,
            automation_query_component,
            NULL,
            error,
            sizeof error
        );
        if (!document) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            compile_report report;
            compile_report_init(&report);
            behavior_asset_compile_for_system(game_system_document_behavior_asset(document), &report);
            asset->errors = report.error_count;
            asset->warnings = report.warning_count;
            for (int i = 0; i < acount(report.items); i++) {
                const compile_diagnostic *diagnostic = &report.items[i];
                catalog_add_diagnostic(
                    asset,
                    diagnostic->severity == CMPL_ERROR ? "error" : "warning",
                    diagnostic->rule,
                    cmpl_rule_name(diagnostic->rule),
                    diagnostic->message
                );
            }
            compile_report_free(&report);
            game_system_document_free(document);
        }
    } else if (family->loader == ASSET_PROTOTYPE) {
        game_prototype_registry registry = {
            .components = GAME_REGISTRY_COMPONENTS,
            .component_count = GAME_REGISTRY_COMPONENT_COUNT,
        };
        char error[256] = { 0 };
        game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
        if (!prototype) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            game_prototype_free(prototype);
        }
    } else {
        char error[256] = { 0 };
        game_scene_document *scene = game_scene_document_load(json, error, sizeof error);
        if (!scene) {
            asset->errors = 1;
            catalog_add_diagnostic(asset, "error", 0, "LOAD", error);
        } else {
            game_scene_document_free(scene);
        }
    }
    asset->valid = asset->errors == 0;
}

static int catalog_asset_index(const catalog_asset *assets, int count, const char *kind, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (kind && kind[0] && strcmp(assets[i].asset.family->kind, kind)) {
            continue;
        }
        if (!strcmp(assets[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int catalog_resolve_index(
    const catalog_asset *assets,
    int count,
    const catalog_asset *source,
    const catalog_reference *reference
)
{
    int index = catalog_asset_index(assets, count, reference->kind, reference->name);
    if (index >= 0) {
        return index;
    }
    if (!reference->kind[0]) {
        index = catalog_asset_index(assets, count, NULL, reference->name);
        if (index >= 0) {
            return index;
        }
    }
    if (!strcmp(reference->kind, "library")) {
        index = catalog_asset_index(assets, count, "shader-library", reference->name);
        if (index >= 0) {
            return index;
        }
    } else if (!strcmp(reference->kind, "shader-library")) {
        index = catalog_asset_index(assets, count, "library", reference->name);
        if (index >= 0) {
            return index;
        }
    }
    if (source && !strcmp(source->asset.family->kind, "shader-library")) {
        return catalog_asset_index(assets, count, "library", reference->name);
    }
    return -1;
}

static void catalog_resolve_references(catalog_asset *assets)
{
    int count = acount(assets);
    for (int i = 0; i < count; i++) {
        catalog_asset *source = &assets[i];
        qsort(
            source->dependencies,
            (size_t)acount(source->dependencies),
            sizeof *source->dependencies,
            catalog_reference_compare
        );
        for (int j = 0; j < acount(source->dependencies); j++) {
            catalog_reference *reference = &source->dependencies[j];
            int target_index = catalog_resolve_index(assets, count, source, reference);
            if (target_index < 0) {
                continue;
            }
            reference->resolved = true;
            if (strcmp(reference->kind, assets[target_index].asset.family->kind)) {
                free(reference->kind);
                reference->kind = xstrdup(assets[target_index].asset.family->kind);
            }
            catalog_reference dependent = {
                .kind = xstrdup(source->asset.family->kind),
                .name = xstrdup(source->name),
                .resolved = true,
            };
            apush(assets[target_index].dependents, dependent);
        }
    }
    for (int i = 0; i < count; i++) {
        qsort(
            assets[i].dependents,
            (size_t)acount(assets[i].dependents),
            sizeof *assets[i].dependents,
            catalog_reference_compare
        );
    }
}

static int enumerate_assets(const char *root, asset_entry **out, char *error, size_t error_size);

static int load_catalog_asset(
    const char *root,
    const asset_entry *entry,
    catalog_asset *asset,
    char *error,
    size_t error_size
)
{
    *asset = (catalog_asset) {
        .asset = {
            .family = entry->family,
            .filename = xstrdup(entry->filename),
            .relative = xstrdup(entry->relative),
            .bytes = entry->bytes,
            .hash = entry->hash,
        },
        .name = asset_name_from_filename(entry->filename),
        .valid = true,
    };
    size_t path_size = strlen(root) + strlen(entry->relative) + 2;
    char *path = xmalloc(path_size);
    snprintf(path, path_size, "%s/%s", root, entry->relative);
    char read_error[256] = { 0 };
    char *json = read_file(path, NULL, NULL, read_error, sizeof read_error);
    free(path);
    if (!json) {
        free_catalog_assets(asset);
        return set_error(error, error_size, read_error);
    }
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    if (!document) {
        asset->valid = false;
        asset->errors = 1;
        catalog_add_diagnostic(asset, "error", 0, "JSON", "asset is not valid JSON");
    } else {
        yyjson_val *json_root = yyjson_doc_get_root(document);
        yyjson_val *version = yyjson_obj_get(json_root, "version");
        if (yyjson_is_num(version)) {
            asset->schema_version = yyjson_get_sint(version);
        }
        const char *description = request_string(json_root, "description");
        if (description) {
            asset->description = xstrdup(description);
        }
        catalog_scan_members(asset, json_root);
        catalog_scan_references(asset, json_root);
        yyjson_doc_free(document);
        catalog_validate_asset(asset, json);
    }
    free(json);
    return 0;
}

static int cached_catalog_asset_index(const catalog_asset *assets, const asset_entry *entry)
{
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].asset.family == entry->family &&
            !strcmp(assets[i].asset.relative, entry->relative) &&
            assets[i].asset.bytes == entry->bytes &&
            assets[i].asset.hash == entry->hash) {
            return i;
        }
    }
    return -1;
}

static void reset_catalog_derived_data(catalog_asset *asset)
{
    for (int i = 0; i < acount(asset->dependencies); i++) {
        asset->dependencies[i].resolved = false;
    }
    free_catalog_references(asset->dependents);
    asset->dependents = NULL;
}

static int build_catalog_from_entries(
    const char *root,
    const asset_entry *entries,
    catalog_asset *previous,
    catalog_asset **out,
    char *error,
    size_t error_size
)
{
    catalog_asset *assets = NULL;
    for (int i = 0; i < acount(entries); i++) {
        int previous_index = previous
                               ? cached_catalog_asset_index(previous, &entries[i])
                               : -1;
        if (previous_index >= 0) {
            catalog_asset asset = previous[previous_index];
            previous[previous_index] = (catalog_asset){ 0 };
            reset_catalog_derived_data(&asset);
            apush(assets, asset);
            continue;
        }
        catalog_asset asset = { 0 };
        if (load_catalog_asset(root, &entries[i], &asset, error, error_size) != 0) {
            free_catalog_assets(assets);
            return -1;
        }
        apush(assets, asset);
    }
    catalog_resolve_references(assets);
    *out = assets;
    return 0;
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

static bool asset_entries_equal(const asset_entry *left, const asset_entry *right)
{
    if (acount(left) != acount(right)) {
        return false;
    }
    for (int i = 0; i < acount(left); i++) {
        if (left[i].family != right[i].family ||
            strcmp(left[i].relative, right[i].relative) ||
            left[i].bytes != right[i].bytes || left[i].hash != right[i].hash) {
            return false;
        }
    }
    return true;
}

static int catalog_snapshot_get(
    const char *root,
    catalog_asset **out,
    uint64_t *revision,
    uint64_t *content_hash,
    char *error,
    size_t error_size
)
{
    asset_entry *entries = NULL;
    if (enumerate_assets(root, &entries, error, error_size) != 0) {
        return -1;
    }
    if (CATALOG_SNAPSHOT.assets && !strcmp(CATALOG_SNAPSHOT.root, root) &&
        asset_entries_equal(entries, CATALOG_SNAPSHOT.entries)) {
        free_asset_entries(entries);
        *out = CATALOG_SNAPSHOT.assets;
        *revision = CATALOG_SNAPSHOT.revision;
        *content_hash = CATALOG_SNAPSHOT.content_hash;
        return 0;
    }
    catalog_asset *assets = NULL;
    if (build_catalog_from_entries(root, entries, CATALOG_SNAPSHOT.assets, &assets, error, error_size) !=
        0) {
        free_asset_entries(entries);
        free_catalog_snapshot(&CATALOG_SNAPSHOT);
        return -1;
    }
    uint64_t next_revision = CATALOG_SNAPSHOT.revision + 1;
    free_catalog_snapshot(&CATALOG_SNAPSHOT);
    CATALOG_SNAPSHOT.root = xstrdup(root);
    CATALOG_SNAPSHOT.entries = entries;
    CATALOG_SNAPSHOT.assets = assets;
    CATALOG_SNAPSHOT.revision = next_revision;
    CATALOG_SNAPSHOT.content_hash = catalog_content_hash(assets);
    *out = CATALOG_SNAPSHOT.assets;
    *revision = CATALOG_SNAPSHOT.revision;
    *content_hash = CATALOG_SNAPSHOT.content_hash;
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

static int handle_describe_contract(response *result)
{
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_uint(result->doc, value, "contract_version", BLUEPRINTS_AUTOMATION_CONTRACT);
    yyjson_mut_obj_add_int(result->doc, value, "response_version", 1);
    yyjson_mut_val *errors = yyjson_mut_obj_add_arr(result->doc, value, "error_categories");
    for (int i = 0;
         i < (int)(sizeof CONTRACT_ERROR_CATEGORIES / sizeof *CONTRACT_ERROR_CATEGORIES);
         i++) {
        yyjson_mut_arr_add_strcpy(result->doc, errors, CONTRACT_ERROR_CATEGORIES[i]);
    }
    yyjson_mut_val *operations = yyjson_mut_obj_add_arr(result->doc, value, "operations");
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        const operation_descriptor *operation = &CONTRACT_OPERATIONS[i];
        yyjson_mut_val *object = yyjson_mut_arr_add_obj(result->doc, operations);
        yyjson_mut_obj_add_strcpy(result->doc, object, "name", operation->name);
        yyjson_mut_obj_add_strcpy(result->doc, object, "summary", operation->summary);
        yyjson_mut_obj_add_int(
            result->doc,
            object,
            "response_version",
            operation->response_version
        );
        yyjson_mut_obj_add_bool(result->doc, object, "read_only", operation->read_only);
        yyjson_mut_obj_add_bool(result->doc, object, "idempotent", operation->idempotent);
        yyjson_mut_obj_add_bool(result->doc, object, "destructive", operation->destructive);
        yyjson_mut_val *fields = yyjson_mut_obj_add_arr(result->doc, object, "request_fields");
        for (int field_index = 0; field_index < operation->field_count; field_index++) {
            const contract_field *field = &operation->fields[field_index];
            yyjson_mut_val *field_value = yyjson_mut_arr_add_obj(result->doc, fields);
            yyjson_mut_obj_add_strcpy(result->doc, field_value, "name", field->name);
            yyjson_mut_obj_add_strcpy(result->doc, field_value, "type", field->type);
            yyjson_mut_obj_add_bool(result->doc, field_value, "required", field->required);
            yyjson_mut_obj_add_strcpy(
                result->doc,
                field_value,
                "description",
                field->description
            );
        }
    }
    yyjson_mut_val *reflection = yyjson_mut_obj_add_obj(result->doc, value, "reflection");
    add_registry(result->doc, reflection);
    return 0;
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

static yyjson_mut_val *add_catalog_reference(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_reference *reference,
    const catalog_asset *assets,
    int count
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", reference->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", reference->name);
    yyjson_mut_obj_add_bool(doc, object, "resolved", reference->resolved);
    if (reference->resolved) {
        int index = catalog_asset_index(
            assets,
            count,
            reference->kind,
            reference->name
        );
        if (index >= 0) {
            yyjson_mut_obj_add_strcpy(doc, object, "path", assets[index].asset.relative);
        }
    }
    return object;
}

static yyjson_mut_val *add_catalog_asset(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_asset *asset,
    const catalog_asset *assets,
    int count,
    bool summary
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", asset->asset.relative);
    yyjson_mut_obj_add_int(doc, object, "schema_version", asset->schema_version);
    yyjson_mut_obj_add_uint(doc, object, "bytes", asset->asset.bytes);
    add_hash(doc, object, asset->asset.hash);
    if (asset->description) {
        yyjson_mut_obj_add_strcpy(doc, object, "description", asset->description);
    }
    if (summary) {
        yyjson_mut_obj_add_bool(doc, object, "valid", asset->valid);
        yyjson_mut_obj_add_int(doc, object, "errors", asset->errors);
        yyjson_mut_obj_add_int(doc, object, "warnings", asset->warnings);
        return object;
    }
    yyjson_mut_val *members = yyjson_mut_obj_add_arr(doc, object, "members");
    for (int i = 0; i < acount(asset->members); i++) {
        yyjson_mut_val *member = yyjson_mut_arr_add_obj(doc, members);
        yyjson_mut_obj_add_strcpy(doc, member, "kind", asset->members[i].kind);
        yyjson_mut_obj_add_strcpy(doc, member, "name", asset->members[i].name);
    }
    yyjson_mut_val *dependencies = yyjson_mut_obj_add_arr(doc, object, "dependencies");
    for (int i = 0; i < acount(asset->dependencies); i++) {
        add_catalog_reference(doc, dependencies, &asset->dependencies[i], assets, count);
    }
    yyjson_mut_val *dependents = yyjson_mut_obj_add_arr(doc, object, "dependents");
    for (int i = 0; i < acount(asset->dependents); i++) {
        add_catalog_reference(doc, dependents, &asset->dependents[i], assets, count);
    }
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, object, "diagnostics");
    for (int i = 0; i < acount(asset->diagnostics); i++) {
        const catalog_diagnostic *diagnostic = &asset->diagnostics[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, diagnostics);
        yyjson_mut_obj_add_strcpy(doc, item, "severity", diagnostic->severity);
        yyjson_mut_obj_add_int(doc, item, "rule", diagnostic->rule);
        yyjson_mut_obj_add_strcpy(doc, item, "rule_name", diagnostic->rule_name);
        yyjson_mut_obj_add_strcpy(doc, item, "message", diagnostic->message);
    }
    yyjson_mut_obj_add_bool(doc, object, "valid", asset->valid);
    yyjson_mut_obj_add_int(doc, object, "errors", asset->errors);
    yyjson_mut_obj_add_int(doc, object, "warnings", asset->warnings);
    return object;
}

static void catalog_hash_text(uint64_t *hash, const char *text)
{
    for (const unsigned char *value = (const unsigned char *)text; *value; value++) {
        *hash ^= *value;
        *hash *= UINT64_C(1099511628211);
    }
}

static uint64_t catalog_content_hash(const catalog_asset *assets)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int i = 0; i < acount(assets); i++) {
        char value[64];
        catalog_hash_text(&hash, assets[i].asset.relative);
        snprintf(
            value,
            sizeof value,
            ":%016llx;",
            (unsigned long long)assets[i].asset.hash
        );
        catalog_hash_text(&hash, value);
    }
    return hash;
}

static bool text_contains_casefold(const char *text, const char *query)
{
    if (!query || !query[0]) {
        return true;
    }
    if (!text) {
        return false;
    }
    size_t query_length = strlen(query);
    size_t text_length = strlen(text);
    if (query_length > text_length) {
        return false;
    }
    for (size_t i = 0; i + query_length <= text_length; i++) {
        size_t j = 0;
        while (j < query_length && tolower((unsigned char)text[i + j]) ==
                                       tolower((unsigned char)query[j])) {
            j++;
        }
        if (j == query_length) {
            return true;
        }
    }
    return false;
}

static bool catalog_search_match(yyjson_val *request, const catalog_asset *asset)
{
    const char *query = request_string(request, "query");
    if (query && query[0]) {
        bool match = text_contains_casefold(asset->asset.family->kind, query) ||
                     text_contains_casefold(asset->name, query) ||
                     text_contains_casefold(asset->asset.relative, query) ||
                     text_contains_casefold(asset->description, query);
        for (int i = 0; !match && i < acount(asset->members); i++) {
            match = text_contains_casefold(asset->members[i].name, query);
        }
        if (!match) {
            return false;
        }
    }
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    if (!kinds) {
        return true;
    }
    size_t index, count;
    yyjson_val *kind;
    yyjson_arr_foreach(kinds, index, count, kind)
    {
        if (yyjson_is_str(kind) &&
            family_by_kind(yyjson_get_str(kind)) == asset->asset.family) {
            return true;
        }
    }
    return false;
}

static int handle_catalog(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_int(result->doc, value, "catalog_version", 1);
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    uint64_t total = (uint64_t)acount(assets);
    uint64_t start = page.cursor < total ? page.cursor : total;
    uint64_t count = total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, acount(assets), start, count);
    yyjson_mut_val *asset_array = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (uint64_t i = 0; i < count; i++) {
        add_catalog_asset(result->doc, asset_array, &assets[start + i], assets, acount(assets), page.summary);
    }
    if (page.summary) {
        yyjson_mut_val *registry = yyjson_mut_obj_add_obj(result->doc, value, "registry");
        add_registry(result->doc, registry);
    } else {
        response registry = response_new("describe_registry");
        handle_describe_registry(&registry);
        yyjson_mut_val *registry_value = yyjson_mut_obj_get(registry.root, "result");
        if (registry_value) {
            yyjson_mut_obj_add_val(
                result->doc,
                value,
                "registry",
                yyjson_mut_val_mut_copy(result->doc, registry_value)
            );
        }
        yyjson_mut_doc_free(registry.doc);
    }
    return 0;
}

static int handle_search_assets(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    yyjson_val *kinds = yyjson_obj_get(request, "kinds");
    if (kinds && !yyjson_is_arr(kinds)) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "kinds must be an array");
        return -1;
    }
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    const char *query = request_string(request, "query");
    yyjson_mut_obj_add_strcpy(result->doc, value, "query", query ? query : "");
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    int total = 0;
    for (int i = 0; i < acount(assets); i++) {
        total += catalog_search_match(request, &assets[i]);
    }
    uint64_t start = page.cursor < (uint64_t)total ? page.cursor : (uint64_t)total;
    uint64_t count = (uint64_t)total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, total, start, count);
    yyjson_mut_val *matches = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    uint64_t match_index = 0;
    for (int i = 0; i < acount(assets); i++) {
        if (catalog_search_match(request, &assets[i])) {
            if (match_index >= start && match_index < start + count) {
                add_catalog_asset(result->doc, matches, &assets[i], assets, acount(assets), page.summary);
            }
            match_index++;
        }
    }
    return 0;
}

static yyjson_mut_val *add_dependency_node(yyjson_mut_doc *doc, yyjson_mut_val *array, const catalog_asset *asset)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", asset->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, object, "name", asset->name);
    yyjson_mut_obj_add_strcpy(doc, object, "path", asset->asset.relative);
    return object;
}

static void add_dependency_edge(
    yyjson_mut_doc *doc,
    yyjson_mut_val *array,
    const catalog_asset *from,
    const catalog_asset *to,
    const catalog_reference *unresolved
)
{
    yyjson_mut_val *edge = yyjson_mut_arr_add_obj(doc, array);
    yyjson_mut_val *from_value = yyjson_mut_obj_add_obj(doc, edge, "from");
    yyjson_mut_obj_add_strcpy(doc, from_value, "kind", from->asset.family->kind);
    yyjson_mut_obj_add_strcpy(doc, from_value, "name", from->name);
    yyjson_mut_obj_add_strcpy(doc, from_value, "path", from->asset.relative);
    yyjson_mut_val *to_value = yyjson_mut_obj_add_obj(doc, edge, "to");
    if (to) {
        yyjson_mut_obj_add_strcpy(doc, to_value, "kind", to->asset.family->kind);
        yyjson_mut_obj_add_strcpy(doc, to_value, "name", to->name);
        yyjson_mut_obj_add_strcpy(doc, to_value, "path", to->asset.relative);
        yyjson_mut_obj_add_bool(doc, to_value, "resolved", true);
    } else {
        yyjson_mut_obj_add_strcpy(doc, to_value, "kind", unresolved->kind);
        yyjson_mut_obj_add_strcpy(doc, to_value, "name", unresolved->name);
        yyjson_mut_obj_add_bool(doc, to_value, "resolved", false);
    }
}

static int handle_asset_dependencies(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    const char *kind = request_string(request, "kind");
    const char *name = request_string(request, "name");
    if (!kind || !name || !name[0] || !family_by_kind(kind)) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "asset_dependencies requires a known kind and name");
        return -1;
    }
    const char *direction = request_string(request, "direction");
    if (!direction) {
        direction = "outgoing";
    }
    if (strcmp(direction, "outgoing") && strcmp(direction, "incoming") &&
        strcmp(direction, "both")) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "direction must be outgoing, incoming, or both");
        return -1;
    }
    bool transitive = false;
    yyjson_val *transitive_value = yyjson_obj_get(request, "transitive");
    if (transitive_value) {
        if (!yyjson_is_bool(transitive_value)) {
            response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "transitive must be a boolean");
            return -1;
        }
        transitive = yyjson_get_bool(transitive_value);
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    int count = acount(assets);
    int root_index = catalog_asset_index(assets, count, family_by_kind(kind)->kind, name);
    if (root_index < 0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_NOT_FOUND, "asset not found");
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_strcpy(result->doc, value, "direction", direction);
    yyjson_mut_obj_add_bool(result->doc, value, "transitive", transitive);
    yyjson_mut_val *root_value = yyjson_mut_obj_add_obj(result->doc, value, "root");
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "kind",
        assets[root_index].asset.family->kind
    );
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "name",
        assets[root_index].name
    );
    yyjson_mut_obj_add_strcpy(
        result->doc,
        root_value,
        "path",
        assets[root_index].asset.relative
    );
    yyjson_mut_obj_add_val(
        result->doc,
        value,
        "asset",
        yyjson_mut_val_mut_copy(result->doc, root_value)
    );
    yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(result->doc, value, "nodes");
    yyjson_mut_val *edges = yyjson_mut_obj_add_arr(result->doc, value, "edges");
    bool *node_seen = xcalloc((size_t)count, sizeof *node_seen);
    bool *edge_seen = xcalloc((size_t)count * (size_t)count, sizeof *edge_seen);
    int *queue = NULL;
    apush(queue, root_index);
    node_seen[root_index] = true;
    add_dependency_node(result->doc, nodes, &assets[root_index]);
    int cursor = 0;
    while (cursor < acount(queue)) {
        int current = queue[cursor++];
        catalog_asset *source = &assets[current];
        bool visit_outgoing = !strcmp(direction, "outgoing") ||
                              !strcmp(direction, "both");
        bool visit_incoming = !strcmp(direction, "incoming") ||
                              !strcmp(direction, "both");
        if (visit_outgoing) {
            for (int i = 0; i < acount(source->dependencies); i++) {
                catalog_reference *reference = &source->dependencies[i];
                int target = reference->resolved ? catalog_asset_index(
                                                       assets,
                                                       count,
                                                       reference->kind,
                                                       reference->name
                                                   )
                                                 : -1;
                if (target < 0) {
                    add_dependency_edge(result->doc, edges, source, NULL, reference);
                    continue;
                }
                if (!edge_seen[current * count + target]) {
                    edge_seen[current * count + target] = true;
                    add_dependency_edge(result->doc, edges, source, &assets[target], NULL);
                }
                if (!node_seen[target]) {
                    node_seen[target] = true;
                    add_dependency_node(result->doc, nodes, &assets[target]);
                    if (transitive) {
                        apush(queue, target);
                    }
                }
            }
        }
        if (visit_incoming) {
            for (int i = 0; i < acount(source->dependents); i++) {
                catalog_reference *reference = &source->dependents[i];
                int dependent = catalog_asset_index(
                    assets,
                    count,
                    reference->kind,
                    reference->name
                );
                if (dependent < 0) {
                    continue;
                }
                if (!edge_seen[dependent * count + current]) {
                    edge_seen[dependent * count + current] = true;
                    add_dependency_edge(result->doc, edges, &assets[dependent], source, NULL);
                }
                if (!node_seen[dependent]) {
                    node_seen[dependent] = true;
                    add_dependency_node(result->doc, nodes, &assets[dependent]);
                    if (transitive) {
                        apush(queue, dependent);
                    }
                }
            }
        }
        if (!transitive) {
            break;
        }
    }
    yyjson_mut_obj_add_int(result->doc, value, "node_count", yyjson_mut_arr_size(nodes));
    yyjson_mut_obj_add_int(result->doc, value, "edge_count", yyjson_mut_arr_size(edges));
    afree(queue);
    free(node_seen);
    free(edge_seen);
    return 0;
}

static uint64_t inspect_json_uint(yyjson_val *value)
{
    if (yyjson_is_uint(value)) {
        return yyjson_get_uint(value);
    }
    if (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0) {
        return (uint64_t)yyjson_get_sint(value);
    }
    return 0;
}

static int compare_uint64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int inspect_node_compare(const void *left, const void *right)
{
    const node *a = *(node *const *)left;
    const node *b = *(node *const *)right;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

static int inspect_item_compare(const void *left, const void *right)
{
    const inspect_json_item *a = left;
    const inspect_json_item *b = right;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

static int inspect_edge_compare(const void *left, const void *right)
{
    const yyjson_val *a = ((const inspect_json_item *)left)->value;
    const yyjson_val *b = ((const inspect_json_item *)right)->value;
    const char *a_kind = request_string((yyjson_val *)a, "kind");
    const char *b_kind = request_string((yyjson_val *)b, "kind");
    uint64_t a_from = inspect_json_uint(yyjson_obj_get(a, "from"));
    uint64_t b_from = inspect_json_uint(yyjson_obj_get(b, "from"));
    uint64_t a_from_pin = inspect_json_uint(yyjson_obj_get(a, "from_pin"));
    uint64_t b_from_pin = inspect_json_uint(yyjson_obj_get(b, "from_pin"));
    uint64_t a_to = inspect_json_uint(yyjson_obj_get(a, "to"));
    uint64_t b_to = inspect_json_uint(yyjson_obj_get(b, "to"));
    uint64_t a_to_pin = inspect_json_uint(yyjson_obj_get(a, "to_pin"));
    uint64_t b_to_pin = inspect_json_uint(yyjson_obj_get(b, "to_pin"));
    int a_order = yyjson_is_num(yyjson_obj_get(a, "order"))
                    ? (int)yyjson_get_sint(yyjson_obj_get(a, "order"))
                    : 0;
    int b_order = yyjson_is_num(yyjson_obj_get(b, "order"))
                    ? (int)yyjson_get_sint(yyjson_obj_get(b, "order"))
                    : 0;
#define INSPECT_CMP(left_value, right_value)                                   \
    if ((left_value) != (right_value))                                         \
    return (left_value) < (right_value) ? -1 : 1
    INSPECT_CMP(a_from, b_from);
    INSPECT_CMP(a_from_pin, b_from_pin);
    INSPECT_CMP(a_to, b_to);
    INSPECT_CMP(a_to_pin, b_to_pin);
    int kind = strcmp(a_kind ? a_kind : "", b_kind ? b_kind : "");
    if (kind) {
        return kind;
    }
    INSPECT_CMP(a_order, b_order);
    const char *a_conversion = request_string((yyjson_val *)a, "conversion");
    const char *b_conversion = request_string((yyjson_val *)b, "conversion");
    return strcmp(a_conversion ? a_conversion : "", b_conversion ? b_conversion : "");
#undef INSPECT_CMP
}

static void inspect_add_sorted_array(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    yyjson_val *source,
    bool edges
)
{
    yyjson_mut_val *array = yyjson_mut_obj_add_arr(doc, object, key);
    if (!yyjson_is_arr(source)) {
        return;
    }
    int count = (int)yyjson_arr_size(source);
    inspect_json_item *items = count ? xmalloc((size_t)count * sizeof *items) : NULL;
    size_t index, maximum;
    yyjson_val *item;
    int item_count = 0;
    yyjson_arr_foreach(source, index, maximum, item)
    {
        items[item_count++] = (inspect_json_item){
            .value = item,
            .id = edges ? inspect_json_uint(yyjson_obj_get(item, "from"))
                        : inspect_json_uint(yyjson_obj_get(item, "id")),
        };
    }
    qsort(items, (size_t)item_count, sizeof *items, edges ? inspect_edge_compare : inspect_item_compare);
    for (int i = 0; i < item_count; i++) {
        yyjson_mut_arr_add_val(array, yyjson_val_mut_copy(doc, items[i].value));
    }
    free(items);
}

static bool inspect_report_has_missing_reference(const compile_report *report, uint64_t node_id)
{
    for (int i = 0; i < acount(report->items); i++) {
        if (report->items[i].rule == CMPL_RULE_MISSING_REFERENCE &&
            report->items[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

static void inspect_add_calls(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const behavior_asset *asset,
    const graph *source,
    const compile_report *report
)
{
    yyjson_mut_val *calls = yyjson_mut_obj_add_arr(doc, object, "calls");
    int count = acount(source->nodes);
    node **nodes = count ? xmalloc((size_t)count * sizeof *nodes) : NULL;
    for (int i = 0; i < count; i++) {
        nodes[i] = source->nodes[i];
    }
    qsort(nodes, (size_t)count, sizeof *nodes, inspect_node_compare);
    for (int i = 0; i < count; i++) {
        const node *call = nodes[i];
        if (!call->reference_id && !call->reference_name && !call->reference_asset) {
            continue;
        }
        yyjson_mut_val *entry = yyjson_mut_arr_add_obj(doc, calls);
        yyjson_mut_obj_add_uint(doc, entry, "node_id", call->id);
        yyjson_mut_obj_add_strcpy(doc, entry, "node_type", call->desc->name);
        yyjson_mut_val *target = yyjson_mut_obj_add_obj(doc, entry, "target");
        bool resolved = true;
        if (call->reference_asset) {
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "asset");
            yyjson_mut_obj_add_strcpy(doc, target, "asset", call->reference_asset);
            if (call->reference_name) {
                yyjson_mut_obj_add_strcpy(doc, target, "name", call->reference_name);
            }
            resolved = !inspect_report_has_missing_reference(report, call->id);
        } else if (call->reference_id) {
            graph *target_graph = behavior_asset_find_graph(asset, call->reference_id);
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "local");
            yyjson_mut_obj_add_uint(doc, target, "graph_id", call->reference_id);
            if (target_graph && target_graph->name) {
                yyjson_mut_obj_add_strcpy(doc, target, "name", target_graph->name);
            }
            resolved = target_graph != NULL;
        } else {
            yyjson_mut_obj_add_strcpy(doc, target, "kind", "event");
            yyjson_mut_obj_add_strcpy(doc, target, "name", call->reference_name);
        }
        yyjson_mut_obj_add_bool(doc, entry, "resolved", resolved);
    }
    free(nodes);
}

static void inspect_add_effects(yyjson_mut_doc *doc, yyjson_mut_val *object, const compile_report *report)
{
    const compile_effects *effects = compile_report_effects(report);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(doc, object, "effects");
    yyjson_mut_val *reads = yyjson_mut_obj_add_arr(doc, value, "reads");
    int read_count = acount(effects->reads);
    uint64_t *ordered_reads = read_count
                                ? xmalloc((size_t)read_count * sizeof *ordered_reads)
                                : NULL;
    for (int i = 0; i < read_count; i++) {
        ordered_reads[i] = effects->reads[i];
    }
    qsort(ordered_reads, (size_t)read_count, sizeof *ordered_reads, compare_uint64);
    for (int i = 0; i < read_count; i++) {
        yyjson_mut_arr_add_uint(doc, reads, ordered_reads[i]);
    }
    free(ordered_reads);
    yyjson_mut_val *writes = yyjson_mut_obj_add_arr(doc, value, "writes");
    int write_count = acount(effects->writes);
    uint64_t *ordered_writes = write_count
                                 ? xmalloc((size_t)write_count * sizeof *ordered_writes)
                                 : NULL;
    for (int i = 0; i < write_count; i++) {
        ordered_writes[i] = effects->writes[i];
    }
    qsort(ordered_writes, (size_t)write_count, sizeof *ordered_writes, compare_uint64);
    for (int i = 0; i < write_count; i++) {
        yyjson_mut_arr_add_uint(doc, writes, ordered_writes[i]);
    }
    free(ordered_writes);
    yyjson_mut_obj_add_uint(doc, value, "flags", effects->flags);
    yyjson_mut_obj_add_bool(doc, value, "all_writes", effects->all_writes != 0);
    yyjson_mut_obj_add_bool(doc, value, "parallel_eligible", effects->parallel_eligible != 0);
    yyjson_mut_obj_add_uint(doc, value, "blocking_node_id", effects->blocking_node_id);
    if (effects->blocking_reason) {
        yyjson_mut_obj_add_strcpy(doc, value, "blocking_reason", effects->blocking_reason);
    } else {
        yyjson_mut_obj_add_null(doc, value, "blocking_reason");
    }
    yyjson_mut_obj_add_uint(doc, value, "topology_revision", effects->topology_revision);
}

static yyjson_mut_val *inspect_graph_value(
    yyjson_mut_doc *doc,
    const behavior_asset *asset,
    const graph *source,
    const compile_report *report
)
{
    char *serialized = graph_save_json(source);
    yyjson_doc *document = serialized
                             ? yyjson_read(serialized, strlen(serialized), 0)
                             : NULL;
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, object, "id", source->graph_id);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", graph_kind_name(source->kind));
    yyjson_mut_obj_add_strcpy(doc, object, "name", source->name ? source->name : "");
    yyjson_mut_obj_add_uint(doc, object, "entry_id", source->entry_id);
    yyjson_mut_obj_add_uint(doc, object, "return_id", source->return_id);
    yyjson_mut_obj_add_uint(doc, object, "revision", source->topology_revision);
    yyjson_mut_obj_add_bool(doc, object, "breakpoints_enabled", source->breakpoints_enabled != 0);
    yyjson_mut_obj_add_bool(doc, object, "dataflow", source->dataflow != 0);
    yyjson_mut_obj_add_int(doc, object, "call_depth_limit", source->call_depth_limit);
    if (source->source_asset) {
        yyjson_mut_obj_add_strcpy(doc, object, "source_asset", source->source_asset);
        yyjson_mut_obj_add_uint(doc, object, "source_graph_id", source->source_graph_id);
    }
    if (source->math_expression) {
        yyjson_mut_obj_add_strcpy(doc, object, "math_expression", source->math_expression);
    }
    yyjson_val *serialized_root = document ? yyjson_doc_get_root(document) : NULL;
    const char *const keys[] = { "description", "comment_boxes" };
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        yyjson_val *value = yyjson_obj_get(serialized_root, keys[i]);
        if (value) {
            yyjson_mut_obj_add_val(doc, object, keys[i], yyjson_val_mut_copy(doc, value));
        }
    }
    inspect_add_sorted_array(doc, object, "nodes", yyjson_obj_get(serialized_root, "nodes"), false);
    inspect_add_sorted_array(doc, object, "edges", yyjson_obj_get(serialized_root, "edges"), true);
    inspect_add_calls(doc, object, asset, source, report);
    if (document) {
        yyjson_doc_free(document);
    }
    free(serialized);
    return object;
}

static void inspect_add_variables(yyjson_mut_doc *doc, yyjson_mut_val *object, yyjson_val *source)
{
    yyjson_mut_val *variables = yyjson_mut_obj_add_arr(doc, object, "variables");
    if (!yyjson_is_arr(source)) {
        return;
    }
    int count = (int)yyjson_arr_size(source);
    inspect_json_item *items = count ? xmalloc((size_t)count * sizeof *items) : NULL;
    size_t index, maximum;
    yyjson_val *item;
    int item_count = 0;
    yyjson_arr_foreach(source, index, maximum, item)
    {
        items[item_count++] = (inspect_json_item){
            .value = item,
            .id = inspect_json_uint(yyjson_obj_get(item, "id")),
        };
    }
    qsort(items, (size_t)item_count, sizeof *items, inspect_item_compare);
    for (int i = 0; i < item_count; i++) {
        yyjson_mut_arr_add_val(variables, yyjson_val_mut_copy(doc, items[i].value));
    }
    free(items);
}

static void inspect_add_validation(yyjson_mut_doc *doc, yyjson_mut_val *object, const compile_report *report)
{
    yyjson_mut_val *validation = yyjson_mut_obj_add_obj(doc, object, "validation");
    yyjson_mut_obj_add_bool(doc, validation, "valid", report->error_count == 0);
    yyjson_mut_obj_add_int(doc, validation, "errors", report->error_count);
    yyjson_mut_obj_add_int(doc, validation, "warnings", report->warning_count);
    yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(doc, validation, "diagnostics");
    for (int i = 0; i < acount(report->items); i++) {
        add_diagnostic(doc, diagnostics, &report->items[i]);
    }
    inspect_add_effects(doc, validation, report);
}

static int handle_inspect_graph(const char *root, yyjson_val *request, response *result, int *exit_code)
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
    if (ref.family->loader != ASSET_GRAPH) {
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "asset does not contain graphs");
        return -1;
    }
    yyjson_doc *source_document = yyjson_read(json, strlen(json), 0);
    behavior_asset *asset = behavior_asset_load_json(json);
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    if (!source_document || !asset) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, source_document ? graph_last_error() : "asset is not valid JSON");
        yyjson_mut_obj_add_bool(result->doc, value, "valid", false);
        if (source_document) {
            yyjson_doc_free(source_document);
        }
        if (asset) {
            behavior_asset_free(asset);
        }
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "graph inspection failed");
        return -1;
    }
    uint64_t requested_id = 0;
    bool has_requested_id = yyjson_obj_get(request, "graph_id") != NULL;
    if (has_requested_id && !request_uint(request, "graph_id", &requested_id)) {
        yyjson_doc_free(source_document);
        behavior_asset_free(asset);
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "graph_id must be an unsigned integer");
        return -1;
    }
    const char *requested_name = request_string(request, "graph_name");
    int selected = -1;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *graph_value = asset->graphs[i];
        if (has_requested_id && graph_value->graph_id != requested_id) {
            continue;
        }
        if (requested_name &&
            strcmp(graph_value->name ? graph_value->name : "", requested_name)) {
            continue;
        }
        selected = i;
        break;
    }
    if ((has_requested_id || requested_name) && selected < 0) {
        yyjson_doc_free(source_document);
        behavior_asset_free(asset);
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_NOT_FOUND, "graph not found");
        return -1;
    }
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile(asset, &report);
    inspect_add_variables(
        result->doc,
        value,
        yyjson_obj_get(yyjson_doc_get_root(source_document), "variables")
    );
    yyjson_mut_val *graphs = yyjson_mut_obj_add_arr(result->doc, value, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        yyjson_mut_val *
            graph_value = inspect_graph_value(result->doc, asset, asset->graphs[i], &report);
        yyjson_mut_arr_add_val(graphs, graph_value);
        if (selected < 0 && i == 0) {
            selected = 0;
        }
    }
    if (selected >= 0) {
        yyjson_mut_val *selected_graph = yyjson_mut_arr_get(graphs, (size_t)selected);
        yyjson_mut_obj_add_val(
            result->doc,
            value,
            "graph",
            yyjson_mut_val_mut_copy(result->doc, selected_graph)
        );
    }
    inspect_add_validation(result->doc, value, &report);
    int errors = report.error_count;
    compile_report_free(&report);
    yyjson_doc_free(source_document);
    behavior_asset_free(asset);
    free(json);
    if (errors) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "graph validation failed");
        return -1;
    }
    return 0;
}

static void inspect_add_system_graphs(yyjson_mut_doc *doc, yyjson_mut_val *object, const behavior_asset *asset)
{
    yyjson_mut_val *graphs = yyjson_mut_obj_add_arr(doc, object, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *graph_value = asset->graphs[i];
        yyjson_mut_val *summary = yyjson_mut_arr_add_obj(doc, graphs);
        yyjson_mut_obj_add_uint(doc, summary, "id", graph_value->graph_id);
        yyjson_mut_obj_add_strcpy(
            doc,
            summary,
            "kind",
            graph_kind_name(graph_value->kind)
        );
        yyjson_mut_obj_add_strcpy(
            doc,
            summary,
            "name",
            graph_value->name ? graph_value->name : ""
        );
        yyjson_mut_obj_add_int(doc, summary, "nodes", acount(graph_value->nodes));
        yyjson_mut_obj_add_int(doc, summary, "edges", acount(graph_value->edges));
        yyjson_mut_obj_add_bool(doc, summary, "dataflow", graph_value->dataflow != 0);
        yyjson_mut_obj_add_uint(doc, summary, "topology_revision", graph_value->topology_revision);
    }
}

static int handle_inspect_system(const char *root, yyjson_val *request, response *result, int *exit_code)
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
    if (ref.family->loader != ASSET_SYSTEM) {
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_INVALID_REQUEST, "asset is not a system");
        return -1;
    }

    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    add_asset_identity(result->doc, value, &ref, bytes, hash);
    game_system_document *document = game_system_document_load(
        json,
        automation_query_component,
        NULL,
        error,
        sizeof error
    );
    if (!document) {
        yyjson_mut_val *diagnostics = yyjson_mut_obj_add_arr(result->doc, value, "diagnostics");
        add_load_diagnostic(result->doc, diagnostics, error);
        yyjson_mut_obj_add_bool(result->doc, value, "valid", false);
        free(json);
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "system inspection failed");
        return -1;
    }

    char *query_json = game_query_save(
        game_system_document_query(document),
        automation_query_component,
        automation_query_component_name,
        NULL
    );
    if (query_json) {
        yyjson_doc *query_document = yyjson_read(query_json, strlen(query_json), 0);
        if (query_document) {
            yyjson_mut_obj_add_val(
                result->doc,
                value,
                "query",
                yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(query_document))
            );
            yyjson_doc_free(query_document);
        } else {
            yyjson_mut_obj_add_null(result->doc, value, "query");
        }
        free(query_json);
    } else {
        yyjson_mut_obj_add_null(result->doc, value, "query");
    }

    const behavior_asset *asset = game_system_document_behavior_asset(document);
    inspect_add_system_graphs(result->doc, value, asset);
    compile_report report;
    compile_report_init(&report);
    behavior_asset_compile_for_system(asset, &report);
    inspect_add_validation(result->doc, value, &report);
    int errors = report.error_count;
    compile_report_free(&report);
    game_system_document_free(document);
    free(json);
    if (errors) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_VALIDATION_ERROR, "system validation failed");
        return -1;
    }
    return 0;
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
    for (int i = 0;
         i < (int)(sizeof CONTRACT_OPERATIONS / sizeof *CONTRACT_OPERATIONS);
         i++) {
        yyjson_mut_arr_add_strcpy(
            result->doc,
            operations,
            CONTRACT_OPERATIONS[i].name
        );
    }
    free_asset_entries(entries);
    return 0;
}

static int handle_list_assets(const char *root, yyjson_val *request, response *result, int *exit_code)
{
    page_options page;
    if (!request_page(request, &page, result, exit_code)) {
        return -1;
    }
    catalog_asset *assets = NULL;
    uint64_t revision = 0;
    uint64_t content_hash_value = 0;
    char error[256] = { 0 };
    if (catalog_snapshot_get(root, &assets, &revision, &content_hash_value, error, sizeof error) !=
        0) {
        response_error(result, exit_code, BLUEPRINTS_AUTOMATION_IO_ERROR, error);
        return -1;
    }
    yyjson_mut_val *value = yyjson_mut_obj_add_obj(result->doc, result->root, "result");
    yyjson_mut_obj_add_strcpy(result->doc, value, "project_root", root);
    char content_hash[32];
    snprintf(content_hash, sizeof content_hash, "%016llx", (unsigned long long)content_hash_value);
    yyjson_mut_obj_add_uint(result->doc, value, "catalog_revision", revision);
    yyjson_mut_obj_add_strcpy(result->doc, value, "content_hash", content_hash);
    yyjson_mut_obj_add_bool(result->doc, value, "summary", page.summary);
    uint64_t total = (uint64_t)acount(assets);
    uint64_t start = page.cursor < total ? page.cursor : total;
    uint64_t count = total - start;
    if (page.has_limit && page.limit < count) {
        count = page.limit;
    }
    add_page_metadata(result->doc, value, acount(assets), start, count);
    yyjson_mut_val *asset_array = yyjson_mut_obj_add_arr(result->doc, value, "assets");
    for (uint64_t i = 0; i < count; i++) {
        add_asset_entry(result->doc, asset_array, &assets[start + i].asset);
    }
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
    } else if (ref.family->loader == ASSET_SYSTEM) {
        char load_error[256] = { 0 };
        game_system_document *document = game_system_document_load(
            json,
            automation_query_component,
            NULL,
            load_error,
            sizeof load_error
        );
        if (!document) {
            errors = 1;
            add_load_diagnostic(result->doc, diagnostics, load_error);
        } else {
            char *query_json = game_query_save(
                game_system_document_query(document),
                automation_query_component,
                automation_query_component_name,
                NULL
            );
            if (query_json) {
                yyjson_doc *query_document = yyjson_read(query_json, strlen(query_json), 0);
                if (query_document) {
                    yyjson_mut_obj_add_val(
                        result->doc,
                        value,
                        "query",
                        yyjson_val_mut_copy(result->doc, yyjson_doc_get_root(query_document))
                    );
                    yyjson_doc_free(query_document);
                }
                free(query_json);
            }
            compile_report report;
            compile_report_init(&report);
            behavior_asset_compile_for_system(game_system_document_behavior_asset(document), &report);
            errors = report.error_count;
            warnings = report.warning_count;
            for (int i = 0; i < acount(report.items); i++) {
                add_diagnostic(result->doc, diagnostics, &report.items[i]);
            }
            compile_report_free(&report);
            game_system_document_free(document);
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
           !strcmp(operation, "read_asset") || !strcmp(operation, "validate_asset") ||
           !strcmp(operation, "catalog") || !strcmp(operation, "search_assets") ||
           !strcmp(operation, "asset_dependencies") ||
           !strcmp(operation, "inspect_graph") ||
           !strcmp(operation, "inspect_system");
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
    if (!operation_by_name(operation)) {
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
    if (!strcmp(operation, "describe_contract")) {
        handle_describe_contract(&result);
    } else if (!strcmp(operation, "status")) {
        handle_status(root, &result, exit_code);
    } else if (!strcmp(operation, "list_assets")) {
        handle_list_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "read_asset")) {
        handle_read_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "validate_asset")) {
        handle_validate_asset(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "catalog")) {
        handle_catalog(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "search_assets")) {
        handle_search_assets(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "asset_dependencies")) {
        handle_asset_dependencies(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_graph")) {
        handle_inspect_graph(root, root_value, &result, exit_code);
    } else if (!strcmp(operation, "inspect_system")) {
        handle_inspect_system(root, root_value, &result, exit_code);
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
    static const char *const page_fields[] = { "cursor", "limit", "summary" };
    for (int i = 0; i < (int)(sizeof page_fields / sizeof *page_fields); i++) {
        yyjson_val *value = params ? yyjson_obj_get(params, page_fields[i]) : NULL;
        if (value) {
            yyjson_mut_obj_add_val(doc, object, page_fields[i], yyjson_val_mut_copy(doc, value));
        }
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
