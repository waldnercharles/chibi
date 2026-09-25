/* Authoring operations for typed command transactions: new projects and
 * assets, semantic graph edits, the start scene, held input and captures. */
#include "assets/scene_document.h"
#include "automation/graph_edit.h"
#include "editor/commands/internal.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "render/screenshot.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool valid_name(const char *name)
{
    static const char allowed[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqr"
                                  "stuvwxyz0123456789 _-";
    size_t length = strlen(name);
    return length && length <= 100 && strspn(name, " ") != length &&
           strspn(name, allowed) == length;
}

static int ensure_directory(const char *path)
{
    struct stat info;
    if (!stat(path, &info)) {
        return S_ISDIR(info.st_mode) ? 0 : -1;
    }
    return mkdir(path, 0755);
}

static bool exists(const char *path)
{
    struct stat info;
    return !stat(path, &info);
}

static int fail(char *error, int size, const char *message)
{
    snprintf(error, size, "%s", message);
    return -1;
}

bool command_authoring_operation(const char *op)
{
    return !strcmp(op, "set_start_scene") || !strcmp(op, "create_project") ||
           !strcmp(op, "create_asset") || !strcmp(op, "edit_graph") ||
           !strcmp(op, "input") || !strcmp(op, "capture");
}

static int write_project(const char *root, const char *name)
{
    char content[280], scenes[300], scene_path[320], manifest[280];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(scenes, sizeof scenes, "%s/scenes", content);
    snprintf(scene_path, sizeof scene_path, "%s/Main" ASSET_EXTENSION, scenes);
    snprintf(manifest, sizeof manifest, "%s/project.json", root);
    if (ensure_directory("projects") || mkdir(root, 0755) ||
        mkdir(content, 0755) || mkdir(scenes, 0755)) {
        return -1;
    }
    game_scene_document *scene = game_scene_document_new("Main");
    char *json = game_scene_document_save(scene);
    game_scene_document_free(scene);
    int result = write_text_file_atomic(scene_path, json);
    free(json);
    char project[512];
    snprintf(
        project,
        sizeof project,
        "{\n  \"version\": 1,\n  \"name\": \"%s\",\n  \"content\": "
        "\"content\",\n"
        "  \"start_scene\": \"scenes/Main" ASSET_EXTENSION "\"\n}\n",
        name
    );
    if (!result) {
        result = write_file_atomic(manifest, project);
    }
    if (result) {
        unlink(scene_path);
        unlink(manifest);
        rmdir(scenes);
        rmdir(content);
        rmdir(root);
    }
    return result;
}

static int create_project(yyjson_val *op, bool dry, char *error, int size)
{
    const char *name = command_string(op, "name");
    if (!valid_name(name)) {
        return fail(error, size, "Project name must use letters, numbers, spaces, hyphens or underscores");
    }
    char root[256];
    snprintf(root, sizeof root, "projects/%s", name);
    if (exists(root)) {
        return fail(error, size, "Project already exists");
    }
    if (dry) {
        return 0;
    }
    if (write_project(root, name)) {
        return fail(error, size, "Could not save project");
    }
    snprintf(error, size, "Created %s; open with --project-root %s", root, root);
    return 0;
}

static const char *folder(const char *kind)
{
    static const char *kinds[] = { "prototype", "scene", "behavior",
                                   "library",   "event", "material",
                                   "shape",     "field", "shader-library" };
    static const char *folders[] = { "prototypes", "scenes", "behaviors",
                                     "libraries",  "events", "materials",
                                     "shapes",     "fields", "shader-libraries" };
    for (size_t i = 0; i < sizeof kinds / sizeof *kinds; i++) {
        if (!strcmp(kind, kinds[i])) {
            return folders[i];
        }
    }
    return NULL;
}

static bool behaviors_known(app_scene_authoring *scene, char **names)
{
    for (int i = 0; i < acount(names); i++) {
        if (!game_behavior_asset_registered(scene->live->behaviors, names[i])) {
            return false;
        }
    }
    return true;
}

/* Applies a semantic graph edit to source, or builds a new graph without it. */
static int graph_document(
    app_scene_authoring *scene,
    const char *source,
    const char *kind,
    const char *name,
    yyjson_val *request,
    char **edited,
    char *error,
    int size
)
{
    const behavior_asset **libraries = NULL;
    for (int i = 0; i < acount(scene->live->definitions); i++) {
        const behavior_asset *asset = scene->live->definitions[i]->working;
        if (asset->domain == BEHAVIOR_DOMAIN_LIBRARY ||
            asset->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY ||
            asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
            apush(libraries, asset);
        }
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    int status = automation_graph_edit(
        source,
        kind,
        name,
        libraries,
        acount(libraries),
        request,
        doc,
        result,
        edited,
        error,
        size
    );
    yyjson_mut_val *failed = yyjson_mut_obj_get(result, "failed_operation");
    if (status && yyjson_mut_is_int(failed)) {
        char message[256];
        snprintf(message, sizeof message, "%s", error);
        snprintf(error, size, "Graph operation %d: %s", (int)yyjson_mut_get_int(failed), message);
    }
    yyjson_mut_doc_free(doc);
    afree(libraries);
    return status ? -1 : 0;
}

/* A prototype or scene document named `name` from the request's definition. */
static char *named_document(yyjson_val *definition, const char *kind, const char *name)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_is_obj(definition)
                             ? yyjson_val_mut_copy(doc, definition)
                             : yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_remove_key(root, "name");
    yyjson_mut_obj_add_str(doc, root, "name", name);
    if (!strcmp(kind, "scene")) {
        yyjson_mut_obj_remove_key(root, "version");
        yyjson_mut_obj_add_int(doc, root, "version", 1);
        if (!yyjson_mut_obj_get(root, "entities")) {
            yyjson_mut_obj_add_arr(doc, root, "entities");
        }
    } else if (!yyjson_mut_obj_get(root, "components")) {
        yyjson_mut_obj_add_obj(doc, root, "components");
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

/* Validates a new prototype and returns its canonical document, or NULL. */
static char *prototype_document(app_scene_authoring *scene, char *json, char *error, int size)
{
    game_prototype *prototype = game_prototype_load(json, &scene->registry, error, (size_t)size);
    free(json);
    if (!prototype) {
        return NULL;
    }
    char *saved = NULL;
    if (behaviors_known(scene, prototype->graphs)) {
        saved = game_prototype_save(prototype);
    } else {
        fail(error, size, "Create and validate attached behaviors first");
    }
    game_prototype_free(prototype);
    return saved;
}

static char *scene_document(app_scene_authoring *scene, char *json, char *error, int size)
{
    game_scene_document *document = game_scene_document_load(json, error, (size_t)size);
    bool valid = document != NULL;
    for (int i = 0; valid && i < acount(document->entities); i++) {
        if (validate_scene_entity(scene, &document->entities[i])) {
            valid = !fail(error, size, scene->message);
        }
    }
    game_scene_document_free(document);
    if (!valid) {
        free(json);
        return NULL;
    }
    return json;
}

/* The validated document of a new asset, or NULL with an error. */
static char *new_document(
    app_scene_authoring *scene,
    const char *kind,
    const char *name,
    yyjson_val *definition,
    char *error,
    int size
)
{
    if (!strcmp(kind, "prototype")) {
        return prototype_document(scene, named_document(definition, kind, name), error, size);
    }
    if (!strcmp(kind, "scene")) {
        return scene_document(scene, named_document(definition, kind, name), error, size);
    }
    char *json = NULL;
    graph_document(scene, NULL, kind, name, definition, &json, error, size);
    return json;
}

/* Registers and opens a saved new asset. */
static int adopt_asset(
    app_scene_authoring *scene,
    const char *kind,
    const char *name,
    const char *json,
    const char *path
)
{
    if (!strcmp(kind, "prototype")) {
        return app_scene_authoring_register_prototype(scene, json, path) ||
               app_scene_authoring_open_prototype(scene, name);
    }
    if (!strcmp(kind, "scene")) {
        return scene_tabs_open_scene(scene, path);
    }
    behavior_asset *asset = behavior_asset_load_json(json);
    assert(asset);
    game_live_definition *live = game_live_definition_new(name, asset);
    game_live_definition_set_path(live, path);
    if (game_live_add_definition(scene->live, live)) {
        game_live_definition_free(live);
        return -1;
    }
    return game_prototype_preview_register_behavior(scene->prototype_preview, name, asset) ||
           app_scene_authoring_open_behavior(scene, name);
}

static int create_asset(app_scene_authoring *scene, yyjson_val *op, bool dry, char *error, int size)
{
    const char *name = command_string(op, "name"), *kind = command_string(op, "kind");
    const char *directory = folder(kind);
    if (!valid_name(name) || !directory) {
        return fail(error, size, "Choose a supported asset kind and a valid unique name");
    }
    char filename[128];
    snprintf(filename, sizeof filename, "%s" ASSET_EXTENSION, name);
    char *dir = content_path_join(scene->content_root, directory);
    char *path = content_path_join(dir, filename);
    bool taken = exists(path) || find_prototype(scene, name) ||
                 game_live_find_definition(scene->live, name);
    char *json = taken
                   ? NULL
                   : new_document(scene, kind, name, yyjson_obj_get(op, "definition"), error, size);
    int result = taken ? fail(error, size, "Asset already exists")
               : json  ? 0
                       : -1;
    if (!result && !dry) {
        result = ensure_directory(dir) || write_text_file_atomic(path, json)
                   ? fail(error, size, "Could not save new asset")
                   : adopt_asset(scene, kind, name, json, path);
        asset_index_invalidate(scene);
    }
    if (!result && !dry) {
        snprintf(error, size, "Created %s %s", kind, name);
    }
    free(json);
    free(dir);
    free(path);
    return result;
}

static int edit_graph(
    app_scene_authoring *scene,
    yyjson_val *catalog,
    yyjson_val *op,
    bool dry,
    char *error,
    int size
)
{
    yyjson_val *target = command_find(yyjson_obj_get(catalog, "objects"), command_string(op, "target"));
    const char *name = command_string(target, "name");
    editor *live = scene->graph_editor;
    if (!target || !live->asset || strcmp(live->asset->name, name) ||
        !behavior_asset_mutable(live->asset)) {
        return fail(error, size, "Open the target graph asset before editing it");
    }
    char *before = ed_snapshot(live), *after = NULL;
    int result = graph_document(
        scene,
        before,
        command_string(target, "kind"),
        name,
        yyjson_obj_get(op, "definition"),
        &after,
        error,
        size
    );
    if (!result && !dry && strcmp(before, after)) {
        result = ed_adopt_behavior_asset(live, after);
        assert(!result);
        ed_push_string(&live->undo, before);
        before = NULL;
        ed_free_string_stack(&live->redo);
    }
    free(before);
    free(after);
    return result;
}

/* Rewrites the project manifest's start scene; the manifest stays JSON. */
static int set_start_scene(
    app_scene_authoring *scene,
    yyjson_val *catalog,
    yyjson_val *op,
    bool dry,
    char *error,
    int size
)
{
    yyjson_val *target = command_find(yyjson_obj_get(catalog, "objects"), command_string(op, "target"));
    const char *asset_path = command_string(target, "path");
    size_t root = strlen(scene->content_root);
    if (strcmp(command_string(target, "kind"), "scene") ||
        strncmp(asset_path, scene->content_root, root) || asset_path[root] != '/') {
        return fail(error, size, "Choose an observed scene asset");
    }
    char *path = content_path_join(scene->content_root, "../project.json");
    char *json = read_scene_file(path);
    yyjson_doc *read = json ? yyjson_read(json, strlen(json), 0) : NULL;
    int result = read ? 0 : fail(error, size, "Project manifest unavailable");
    if (read && !dry) {
        yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
        yyjson_mut_val *manifest = yyjson_mut_doc_get_root(doc);
        yyjson_mut_obj_remove_key(manifest, "start_scene");
        yyjson_mut_obj_add_str(doc, manifest, "start_scene", asset_path + root + 1);
        size_t length = 0;
        char *written = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY_TWO_SPACES, &length);
        char *updated = xmalloc(length + 2);
        snprintf(updated, length + 2, "%s\n", written);
        result = write_file_atomic(path, updated)
                   ? fail(error, size, "Could not save project manifest")
                   : 0;
        free(written);
        free(updated);
        yyjson_mut_doc_free(doc);
    }
    yyjson_doc_free(read);
    free(json);
    free(path);
    return result;
}

static int capture(yyjson_val *op, bool dry, char *error, int size)
{
    const char *path = command_string(op, "path");
    if (!*path || strlen(path) > 1024) {
        return fail(error, size, "Capture requires a PNG path");
    }
    return dry ? 0 : app_save_screenshot(path);
}

int command_authoring_apply(
    app_scene_authoring *scene,
    yyjson_val *catalog,
    yyjson_val *operation,
    bool dry,
    char *error,
    int size
)
{
    const char *op = command_string(operation, "op");
    snprintf(error, size, "%s", dry ? "Validated" : "Applied");
    if (!strcmp(op, "capture")) {
        return capture(operation, dry, error, size);
    }
    if (!strcmp(op, "input")) {
        return command_input(scene, operation, dry, error, size);
    }
    if (scene->runtime_snapshot_valid || scene->preview_playing) {
        return fail(error, size, "Stop playback before authoring");
    }
#ifdef __EMSCRIPTEN__
    /* A browser session saves only content assets of its served project. */
    if (!strcmp(op, "set_start_scene") || !strcmp(op, "create_project")) {
        return fail(error, size, "Change projects from a native editor");
    }
#endif
    if (!strcmp(op, "set_start_scene")) {
        return set_start_scene(scene, catalog, operation, dry, error, size);
    }
    if (!strcmp(op, "create_project")) {
        return create_project(operation, dry, error, size);
    }
    if (!strcmp(op, "create_asset")) {
        return create_asset(scene, operation, dry, error, size);
    }
    return edit_graph(scene, catalog, operation, dry, error, size);
}
