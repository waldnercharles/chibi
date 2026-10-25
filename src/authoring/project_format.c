#include "authoring/project_format.h"

#include "assets/input_map.h"
#include "assets/project.h"
#include "assets/prototype.h"
#include "assets/scenario_document.h"
#include "assets/scene_document.h"
#include "authoring/component_declarations.h"
#include "authoring/component_presentation.h"
#include "authoring/language_catalog.h"
#include "authoring/source_asset.h"
#include "authoring/source_catalog.h"
#include "authoring/source_format.h"
#include "authoring/source_minimize.h"
#include "authoring/source_package_files.h"
#include "graph/document/serialize.h"
#include "runtime/declared_components.h"
#include "support/array.h"
#include "support/asset_format.h"
#include "support/asset_text.h"
#include "support/file_io.h"
#include "support/xalloc.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Formatting and optional identity remapping consume exact syntax snapshots. */
#define CANONICAL_SOURCE_MAX_BYTES (8 * 1024 * 1024)

typedef enum
{
    FORMAT_SOURCE,
    FORMAT_GRAPH,
    FORMAT_PROTOTYPE,
    FORMAT_RELATIONSHIP,
    FORMAT_SCENE,
    FORMAT_EDITOR,
    FORMAT_SCENARIO,
} format_kind;
typedef struct
{
    const char *folder, *extension;
    format_kind kind;
} asset_family;
/* The content folders whose assets have a canonical form, and its writer. */
static const asset_family FAMILIES[] = {
    { "behaviors", CODE_ASSET_EXTENSION, FORMAT_SOURCE },
    { "events", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "libraries", CODE_ASSET_EXTENSION, FORMAT_SOURCE },
    { "materials", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "shapes", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "models", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "fields", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "shader-libraries", DECLARATIVE_ASSET_EXTENSION, FORMAT_GRAPH },
    { "systems", CODE_ASSET_EXTENSION, FORMAT_SOURCE },
    { "prototypes", CODE_ASSET_EXTENSION, FORMAT_PROTOTYPE },
    { "relationships", CODE_ASSET_EXTENSION, FORMAT_RELATIONSHIP },
    { "scenes", DECLARATIVE_ASSET_EXTENSION, FORMAT_SCENE },
    { "editor", DECLARATIVE_ASSET_EXTENSION, FORMAT_EDITOR },
    { "scenarios", DECLARATIVE_ASSET_EXTENSION, FORMAT_SCENARIO },
};
typedef struct
{
    const asset_family *family;
    char *relative; /* content/FOLDER/NAME */
} asset_entry;
typedef struct
{
    char *content;
    bool check;
    source_catalog *sources;
    /* Prototypes by the names they declare, read when one is first included. */
    char **names, **texts;
    /* The project's relationships, which prototype links name. */
    ecs_relationship_desc **relationships;
    bool prototypes_read, prototypes_valid;
    char prototypes_error[256];
} formatter;

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

/* The disk file of a content/ path. */
static char *content_path(const formatter *f, const char *relative)
{
    return join(f->content, relative + strlen("content/"));
}

/* Writes text over the file at path; false when it cannot. */
static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    bool written = file && fputs(text, file) >= 0;
    if (file) {
        written = fclose(file) == 0 && written;
    }
    return written;
}

/* Compares an asset with its canonical text and, unless checking, rewrites
 * it. 1 when they differ, 0 when not, -1 when the rewrite fails. */
static int settle(bool check, const char *name, const char *path, const char *text, const char *canonical)
{
    if (!strcmp(text, canonical)) {
        return 0;
    }
    fprintf(stderr, "%s %s\n", check ? "not canonical:" : "formatted", name);
    if (!check && !write_text(path, canonical)) {
        fprintf(stderr, "format: could not write %s\n", name);
        return -1;
    }
    return 1;
}

static char *canonical_source(const formatter *f, const char *text, const char *path, char *error, size_t size)
{
    if (strlen(text) > CANONICAL_SOURCE_MAX_BYTES) {
        snprintf(error, size, "source exceeds formatter byte limit");
        return NULL;
    }
    for (int i = 0; i < source_catalog_source_count(f->sources); i++) {
        const source_catalog_source *entry = source_catalog_source_at(f->sources, i);
        if (!strcmp(entry->path, path)) {
            return authoring_source_canonical(
                text,
                source_catalog_view(f->sources),
                entry->identity,
                error,
                size
            );
        }
    }
    script_syntax *source = script_parse(text);
    char *saved = authoring_source_format(source, error, size);
    script_syntax_free(source);
    return saved;
}

/* A regular file, not a link: formatting rewrites it in place. */
static bool regular_file(const char *path)
{
    struct stat info;
    return !lstat(path, &info) && S_ISREG(info.st_mode);
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The regular files of folder with extension, sorted, into *names; false
 * when the folder exists but cannot be read. */
static bool folder_files(const char *folder, const char *extension, char ***names)
{
    DIR *dir = opendir(folder);
    if (!dir) {
        return errno == ENOENT;
    }
    for (struct dirent *entry; (entry = readdir(dir));) {
        size_t n = strlen(entry->d_name), suffix = strlen(extension);
        if (entry->d_name[0] == '.' || n <= suffix ||
            strcmp(entry->d_name + n - suffix, extension)) {
            continue;
        }
        char *path = join(folder, entry->d_name);
        if (regular_file(path)) {
            apush(*names, xstrdup(entry->d_name));
        }
        free(path);
    }
    closedir(dir);
    if (*names) {
        qsort(*names, (size_t)acount(*names), sizeof **names, compare_names);
    }
    return true;
}

/* Prototypes are code or declarative files; every other family has one
 * extension. The names stay sorted together. */
static bool family_files(const asset_family *family, const char *folder, char ***names)
{
    if (!folder_files(folder, family->extension, names)) {
        return false;
    }
    if (family->kind != FORMAT_PROTOTYPE) {
        return true;
    }
    return folder_files(folder, DECLARATIVE_ASSET_EXTENSION, names);
}

static bool code_path(const char *path)
{
    size_t length = strlen(path), suffix = strlen(CODE_ASSET_EXTENSION);
    return length > suffix && !strcmp(path + length - suffix, CODE_ASSET_EXTENSION);
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* Every prototype by the name it declares, as the player finds them: false
 * when one cannot be read or two declare the same name. */
static bool read_prototypes(formatter *f, char *error, size_t size)
{
    char *folder = join(f->content, "prototypes");
    char **files = NULL;
    const asset_family prototypes = { "prototypes", CODE_ASSET_EXTENSION, FORMAT_PROTOTYPE };
    bool valid = family_files(&prototypes, folder, &files);
    if (!valid) {
        snprintf(error, size, "could not read %s", folder);
    }
    for (int i = 0; valid && i < acount(files); i++) {
        char *path = join(folder, files[i]);
        char *text = file_read_text(path);
        av_doc *document = text
                             ? authoring_prototype_document_read(text, NULL, error, size)
                             : NULL;
        const char *name = document
                             ? av_get_str(av_obj_get(av_doc_root(document), "name"))
                             : NULL;
        valid = document != NULL;
        if (!text) {
            snprintf(error, size, "could not read %s", path);
        }
        for (int j = 0; valid && name && j < acount(f->names); j++) {
            if (!strcmp(f->names[j], name)) {
                snprintf(error, size, "two prototypes declare %s", name);
                valid = false;
            }
        }
        if (valid && name && name[0]) {
            apush(f->names, xstrdup(name));
            apush(f->texts, text);
            text = NULL;
        }
        av_doc_free(document);
        free(text);
        free(path);
    }
    free_names(files);
    free(folder);
    return valid;
}

/* Prototype includes name other prototypes by the names they declare. */
static const char *include_prototype(const char *name, void *user)
{
    formatter *f = user;
    for (int i = 0; i < acount(f->names); i++) {
        if (!strcmp(f->names[i], name)) {
            return f->texts[i];
        }
    }
    return NULL;
}

static char *format_text(const char *text, char *error, size_t size)
{
    script_syntax *syntax = script_parse(text);
    char *saved = authoring_source_format(syntax, error, size);
    script_syntax_free(syntax);
    return saved;
}

static char *canonical_graph(const char *text, char *error, size_t size)
{
    behavior_asset *asset = behavior_asset_load(text);
    if (!asset) {
        snprintf(error, size, "%s", graph_last_error());
        return NULL;
    }
    char *saved = behavior_asset_save(asset);
    behavior_asset *reopened = saved ? behavior_asset_load(saved) : NULL;
    if (!reopened || !behavior_asset_equal(asset, reopened)) {
        free(saved);
        saved = NULL;
        snprintf(error, size, "formatted graph does not preserve the complete native asset");
    }
    behavior_asset_free(reopened);
    behavior_asset_free(asset);
    return saved;
}

/* It must resolve, but saves as authored: its own content, naming its
 * includes and children's prototypes. */
static char *canonical_prototype(formatter *f, const char *text, const char *path, char *error, size_t size)
{
    if (!f->prototypes_read) {
        f->prototypes_valid = read_prototypes(f, f->prototypes_error, sizeof f->prototypes_error);
        f->prototypes_read = true;
    }
    if (!f->prototypes_valid) {
        snprintf(error, size, "%s", f->prototypes_error);
        return NULL;
    }
    ecs_component_catalog components = project_components();
    prototype_registry registry = {
        .components = components.components,
        .component_count = components.component_count,
        .include = include_prototype,
        .include_user = f,
        .relationships = (const ecs_relationship_desc *const *)f->relationships,
        .relationship_count = acount(f->relationships),
        .decode = authoring_prototype_document_read
    };
    prototype *resolved = prototype_load(text, &registry, error, size);
    prototype *authored = resolved
                            ? prototype_load_authored(text, &registry, error, size)
                            : NULL;
    char *saved = !authored       ? NULL
                : code_path(path) ? format_text(text, error, size)
                                  : prototype_save(authored);
    prototype_free(authored);
    prototype_free(resolved);
    return saved;
}

/* Owned canonical file text for one asset, from its native loader and saver. */
static char *canonical_asset(
    formatter *f,
    const asset_family *family,
    const char *text,
    const char *path,
    char *error,
    size_t size
)
{
    switch (family->kind) {
        case FORMAT_SOURCE: return canonical_source(f, text, path, error, size);
        case FORMAT_GRAPH: return canonical_graph(text, error, size);
        case FORMAT_PROTOTYPE:
            return canonical_prototype(f, text, path, error, size);
        case FORMAT_RELATIONSHIP: {
            ecs_relationship_desc
                *relationship = authoring_relationship_source_read(text, error, size);
            if (!relationship) {
                return NULL;
            }
            authoring_relationship_source_free(relationship);
            return format_text(text, error, size);
        }
        case FORMAT_SCENE: {
            scene_document *scene = scene_document_load(text, error, size);
            char *saved = scene ? scene_document_save(scene, project_components())
                                : NULL;
            scene_document_free(scene);
            return saved;
        }
        case FORMAT_EDITOR: {
            /* Validated as written; the shared code writer canonicalizes it. */
            component_presentation presentation = { 0 };
            bool valid = component_presentation_read(text, project_components(), &presentation, error, size);
            component_presentation_free(&presentation);
            return valid ? xstrdup(text) : NULL;
        }
        case FORMAT_SCENARIO: {
            scenario_document *scenario = scenario_document_load(text, error, size);
            char *saved = scenario ? scenario_document_save(scenario) : NULL;
            scenario_document_free(scenario);
            return saved;
        }
    }
    return NULL;
}

static int format_asset(formatter *f, const asset_entry *entry)
{
    char error[512] = { 0 };
    char *path = content_path(f, entry->relative);
    char *text = file_read_text(path);
    char *canonical = text ? canonical_asset(f, entry->family, text, path, error, sizeof error)
                           : NULL;
    if (canonical && !code_path(path)) {
        char *disk = asset_text_to_disk(canonical, error, sizeof error);
        free(canonical);
        canonical = disk;
    }
    int differing = -1;
    if (!canonical) {
        fprintf(stderr, "format: %s: %s\n", entry->relative, text ? error : "could not read it");
    } else {
        differing = settle(f->check, entry->relative, path, text, canonical);
    }
    free(canonical);
    free(text);
    free(path);
    return differing;
}

/* The input map is canonical as its own writer emits it. */
static int format_input_map(const formatter *f)
{
    char *path = join(f->content, INPUT_MAP_FILE);
    struct stat info;
    if (stat(path, &info) && errno == ENOENT) {
        free(path);
        return 0;
    }
    char error[256] = "could not read it";
    input_action *map = NULL;
    char *text = file_read_text(path);
    char *written = text && input_map_read(text, &map, error, sizeof error)
                      ? input_map_write(map)
                      : NULL;
    char *canonical = written ? asset_text_to_disk(written, error, sizeof error) : NULL;
    int differing = -1;
    if (!canonical) {
        fprintf(stderr, "format: content/" INPUT_MAP_FILE ": %s\n", error);
    } else {
        differing = settle(f->check, "content/" INPUT_MAP_FILE, path, text, canonical);
    }
    input_map_free(map);
    free(canonical);
    free(written);
    free(text);
    free(path);
    return differing;
}

/* One component source, already validated by loading the declarations. */
static int format_component(bool check, const char *folder, const char *name)
{
    char error[256] = "could not read it",
         *relative = join("content/components", name);
    char *path = join(folder, name);
    char *text = file_read_text(path);
    script_syntax *syntax = text ? script_parse(text) : NULL;
    char *canonical = syntax ? authoring_source_format(syntax, error, sizeof error)
                             : NULL;
    int differing = -1;
    if (!canonical) {
        fprintf(stderr, "format: %s: %s\n", relative, error);
    } else {
        differing = settle(check, relative, path, text, canonical);
    }
    script_syntax_free(syntax);
    free(canonical);
    free(text);
    free(path);
    free(relative);
    return differing;
}

static int format_components(const formatter *f)
{
    char *folder = join(f->content, "components");
    char **names = NULL;
    int differing = folder_files(folder, CODE_ASSET_EXTENSION, &names) ? 0 : -1;
    if (differing < 0) {
        fprintf(stderr, "format: could not read content/components\n");
    }
    for (int i = 0; i < acount(names) && differing >= 0; i++) {
        int one = format_component(f->check, folder, names[i]);
        differing = one < 0 ? one : differing + one;
    }
    free_names(names);
    free(folder);
    return differing;
}

/* A folder the formatter rewrites in, not a link to one elsewhere; 0 when it
 * does not exist and -1 when it is not a directory. */
static int real_folder(const char *path, char *error, size_t size)
{
    struct stat info;
    if (lstat(path, &info)) {
        if (errno == ENOENT) {
            return 0;
        }
        snprintf(error, size, "could not inspect %s", path);
        return -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        snprintf(error, size, "not a directory: %s", path);
        return -1;
    }
    return 1;
}

static int compare_entries(const void *a, const void *b)
{
    return strcmp(((const asset_entry *)a)->relative, ((const asset_entry *)b)->relative);
}

static void free_entries(asset_entry *entries)
{
    for (int i = 0; i < acount(entries); i++) {
        free(entries[i].relative);
    }
    afree(entries);
}

/* The assets of every family, by content path; false with why in error. */
static bool list_assets(const formatter *f, asset_entry **out, char *error, size_t size)
{
    int content = real_folder(f->content, error, size);
    if (content <= 0) {
        return content == 0;
    }
    for (size_t i = 0; i < sizeof FAMILIES / sizeof *FAMILIES; i++) {
        const asset_family *family = &FAMILIES[i];
        char *folder = join(f->content, family->folder);
        int status = real_folder(folder, error, size);
        char **names = NULL;
        if (status > 0 && !family_files(family, folder, &names)) {
            snprintf(error, size, "could not read %s", folder);
            status = -1;
        }
        for (int j = 0; j < acount(names); j++) {
            size_t length = strlen(family->folder) + strlen(names[j]) + sizeof "content//";
            char *relative = xmalloc(length);
            snprintf(relative, length, "content/%s/%s", family->folder, names[j]);
            apush(*out, ((asset_entry){ family, relative }));
        }
        free_names(names);
        free(folder);
        if (status < 0) {
            return false;
        }
    }
    if (*out) {
        qsort(*out, (size_t)acount(*out), sizeof **out, compare_entries);
    }
    return true;
}

int authoring_project_format(const char *root, native_capabilities *native, bool check)
{
    char error[512] = { 0 };
    formatter f = { .content = chibi_project_content(root, error, sizeof error),
                    .check = check };
    declared_components *declared = NULL;
    authoring_language_catalog *catalog = NULL;
    asset_entry *entries = NULL;
    int differing = -1;
    if (!f.content ||
        !authoring_component_declarations_load(
            f.content,
            native_capabilities_catalog(native),
            &declared,
            error,
            sizeof error
        ) ||
        !authoring_relationship_declarations_load(f.content, &f.relationships, error, sizeof error) ||
        !list_assets(&f, &entries, error, sizeof error)) {
        fprintf(stderr, "format: %s\n", error);
        goto done;
    }
    catalog = authoring_language_catalog_new(
        &(authoring_language_catalog_input){ .native = native },
        error,
        sizeof error
    );
    if (!catalog) {
        fprintf(stderr, "format: %s\n", error);
        goto done;
    }
    f.sources = source_catalog_new(f.content, authoring_language_catalog_view(catalog));
    differing = 0;
    for (int i = 0; i < acount(entries) && differing >= 0; i++) {
        int one = format_asset(&f, &entries[i]);
        differing = one < 0 ? one : differing + one;
    }
    if (differing >= 0) {
        int map = format_input_map(&f);
        differing = map < 0 ? map : differing + map;
    }
    if (differing >= 0) {
        int components = format_components(&f);
        differing = components < 0 ? components : differing + components;
    }
done:
    source_catalog_free(f.sources);
    authoring_language_catalog_free(catalog);
    free_entries(entries);
    for (int i = 0; i < acount(f.names); i++) {
        free(f.names[i]);
        free(f.texts[i]);
    }
    afree(f.names);
    afree(f.texts);
    for (int i = 0; i < acount(f.relationships); i++) {
        authoring_relationship_source_free(f.relationships[i]);
    }
    afree(f.relationships);
    declared_components_free(declared);
    free(f.content);
    return differing;
}

/* Whether a source declares a package. */
static bool package_source(const script_syntax *syntax)
{
    TSNode root = ts_tree_root_node(syntax->tree);
    return !ts_node_is_null(ts_node_child_by_field_name(root, "package", 7));
}

int authoring_packages_format(const char *root, bool check)
{
    source_package_input *files = NULL;
    source_package_load_error error;
    if (!source_package_files_read(root, &files, &error)) {
        fprintf(stderr, "format: %s: %s\n", error.path, error.message);
        return -1;
    }
    if (!acount(files)) {
        fprintf(stderr, "format: %s holds no project.kdl or package files\n", root);
        return -1;
    }
    size_t prefix = strlen(root) + 1;
    char **canonical = NULL;
    int differing = 0;
    for (int i = 0; i < acount(files) && differing >= 0; i++) {
        assert(!strncmp(files[i].path, root, prefix - 1) && files[i].path[prefix - 1] == '/');
        char message[256] = "it does not declare a package";
        script_syntax *syntax = script_parse(files[i].text);
        char *text = package_source(syntax)
                       ? authoring_source_format(syntax, message, sizeof message)
                       : NULL;
        script_syntax_free(syntax);
        if (!text) {
            fprintf(stderr, "format: %s: %s\n", files[i].path + prefix, message);
            differing = -1;
        }
        apush(canonical, text);
    }
    for (int i = 0; i < acount(canonical) && differing >= 0; i++) {
        const char *name = files[i].path + prefix;
        int one = settle(check, name, files[i].path, files[i].text, canonical[i]);
        differing = one < 0 ? one : differing + one;
    }
    for (int i = 0; i < acount(canonical); i++) {
        free(canonical[i]);
    }
    afree(canonical);
    source_package_files_free(files);
    return differing;
}
