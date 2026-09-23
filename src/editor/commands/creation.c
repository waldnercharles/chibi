#include "assets/scene_document.h"
#include "automation/graph_edit.h"
#include "editor/commands/internal.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "render/screenshot.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool valid_name(const char *name)
{
    return *name && strlen(name) <= 100 && strspn(name, " ") != strlen(name) &&
           strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-") ==
               strlen(name);
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

static int create_project(yyjson_val *op, bool dry, char *error, int size)
{
#ifdef __EMSCRIPTEN__
    return fail(error, size, "Create projects from a native editor; browser sessions stay in their served project");
#else
    const char *name = command_string(op, "name");
    if (!valid_name(name)) {
        return fail(error, size, "Project name must use letters, numbers, spaces, hyphens or underscores");
    }
    char root[256], content[280], scenes[300], scene_path[320], manifest[280];
    snprintf(root, sizeof root, "projects/%s", name);
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(scenes, sizeof scenes, "%s/scenes", content);
    snprintf(scene_path, sizeof scene_path, "%s/Main.json", scenes);
    snprintf(manifest, sizeof manifest, "%s/project.json", root);
    if (exists(root)) {
        return fail(error, size, "Project already exists");
    }
    if (dry) {
        return 0;
    }
    if (ensure_directory("projects") || mkdir(root, 0755) ||
        mkdir(content, 0755) || mkdir(scenes, 0755)) {
        return fail(error, size, "Could not create project directories");
    }
    game_scene_document *scene = game_scene_document_new("Main");
    char *json = game_scene_document_save(scene);
    game_scene_document_free(scene);
    int result = write_text_file_atomic(scene_path, json);
    free(json);
    char project[512];
    snprintf(project, sizeof project, "{\"version\":1,\"name\":\"%s\",\"content\":\"content\",\"start_scene\":\"scenes/Main.json\"}\n", name);
    if (!result) {
        result = write_text_file_atomic(manifest, project);
    }
    if (result) {
        unlink(scene_path);
        unlink(manifest);
        rmdir(scenes);
        rmdir(content);
        rmdir(root);
        return fail(error, size, "Could not save project");
    }
    snprintf(error, size, "Created %s; open with --project-root %s", root, root);
    return 0;
#endif
}

static const char *folder(const char *kind)
{
    static const char *kinds[] = { "prototype", "scene", "behavior",
                                   "library",   "event", "material",
                                   "shape",     "field", "shader-library" };
    static const char *folders[] = { "prototypes", "scenes", "behaviors",
                                     "libraries",  "events", "materials",
                                     "shapes",     "fields", "shader-libraries" };
    for (int i = 0; i < (int)(sizeof kinds / sizeof *kinds); i++) {
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

static int create_asset(app_scene_authoring *scene, yyjson_val *op, bool dry, char *error, int size)
{
    const char *name = command_string(op, "name"), *kind = command_string(op, "kind");
    const char *directory = folder(kind);
    if (!valid_name(name) || !directory) {
        return fail(error, size, "Choose a supported asset kind and a valid unique name");
    }
    char filename[128];
    snprintf(filename, sizeof filename, "%s.json", name);
    char *dir = content_path_join(scene->content_root, directory);
    char *path = content_path_join(dir, filename);
    bool duplicate = exists(path) || find_prototype(scene, name) ||
                     game_live_find_definition(scene->live, name);
    if (duplicate) {
        free(dir);
        free(path);
        return fail(error, size, "Asset already exists");
    }
    yyjson_val *definition = yyjson_obj_get(op, "definition");
    char *json = NULL;
    int result = 0;
    if (!strcmp(kind, "prototype") || !strcmp(kind, "scene")) {
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *root = yyjson_is_obj(definition)
                                 ? yyjson_val_mut_copy(doc, definition)
                                 : yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_remove_key(root, "name");
        yyjson_mut_obj_add_str(doc, root, "name", name);
        if (!strcmp(kind, "scene")) {
            yyjson_mut_obj_add_int(doc, root, "version", 1);
            if (!yyjson_mut_obj_get(root, "entities")) {
                yyjson_mut_obj_add_arr(doc, root, "entities");
            }
        } else if (!yyjson_mut_obj_get(root, "components")) {
            yyjson_mut_obj_add_obj(doc, root, "components");
        }
        json = yyjson_mut_write(doc, 0, NULL);
        yyjson_mut_doc_free(doc);
        if (!strcmp(kind, "prototype")) {
            game_prototype *prototype = game_prototype_load(json, &scene->registry, error, size);
            if (!prototype) {
                result = -1;
            } else if (!behaviors_known(scene, prototype->graphs)) {
                result = fail(error, size, "Create and validate attached behaviors first");
            } else {
                free(json);
                json = game_prototype_save(prototype);
            }
            game_prototype_free(prototype);
        } else {
            game_scene_document *document = game_scene_document_load(json, error, size);
            if (!document) {
                result = -1;
            }
            for (int i = 0; document && !result && i < acount(document->entities); i++) {
                if (validate_scene_entity(scene, &document->entities[i])) {
                    result = fail(error, size, scene->message);
                }
            }
            game_scene_document_free(document);
        }
    } else {
        result = graph_document(scene, NULL, kind, name, definition, &json, error, size);
    }
    if (!result && !dry) {
        result = ensure_directory(dir) || write_text_file_atomic(path, json);
        if (result) {
            fail(error, size, "Could not save new asset");
        } else if (!strcmp(kind, "prototype")) {
            result = app_scene_authoring_register_prototype(scene, json, path);
            if (!result) {
                result = app_scene_authoring_open_prototype(scene, name);
            }
        } else if (!strcmp(kind, "scene")) {
            result = scene_tabs_open_scene(scene, path);
        } else {
            behavior_asset *asset = behavior_asset_load_json(json);
            assert(asset);
            game_live_definition *live = game_live_definition_new(name, asset);
            game_live_definition_set_path(live, path);
            result = game_live_add_definition(scene->live, live);
            if (result) {
                game_live_definition_free(live);
            } else {
                result = game_prototype_preview_register_behavior(scene->prototype_preview, name, asset);
                if (!result) {
                    result = app_scene_authoring_open_behavior(scene, name);
                }
            }
        }
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
        const char *path = command_string(operation, "path");
        if (!*path || strlen(path) > 1024) {
            return fail(error, size, "Capture requires a PNG path");
        }
        return dry ? 0 : app_save_screenshot(path);
    }
    if (!strcmp(op, "input")) {
        return command_input(scene, operation, dry, error, size);
    }
    if (scene->runtime_snapshot_valid || scene->preview_playing) {
        return fail(error, size, "Stop playback before authoring");
    }
    if (!strcmp(op, "set_start_scene")) {
        yyjson_val *target = command_find(
            yyjson_obj_get(catalog, "objects"),
            command_string(operation, "target")
        );
        if (strcmp(command_string(target, "kind"), "scene")) {
            return fail(error, size, "Choose an observed scene asset");
        }
        char *path = content_path_join(scene->content_root, "../project.json");
        char *json = read_scene_file(path);
        yyjson_doc *read = json ? yyjson_read(json, strlen(json), 0) : NULL;
        if (!read) {
            free(path);
            free(json);
            return fail(error, size, "Project manifest unavailable");
        }
        yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
        yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
        const char *asset_path = command_string(target, "path");
        const char *relative = asset_path + strlen(scene->content_root) + 1;
        yyjson_mut_obj_remove_key(root, "start_scene");
        yyjson_mut_obj_add_str(doc, root, "start_scene", relative);
        char *updated = yyjson_mut_write(doc, 0, NULL);
        int result = dry ? 0 : write_text_file_atomic(path, updated);
        free(updated);
        yyjson_mut_doc_free(doc);
        yyjson_doc_free(read);
        free(json);
        free(path);
        return result;
    }
    if (!strcmp(op, "create_project")) {
        return create_project(operation, dry, error, size);
    }
    if (!strcmp(op, "create_asset")) {
        return create_asset(scene, operation, dry, error, size);
    }
    return edit_graph(scene, catalog, operation, dry, error, size);
}
