#include "support/resources.h"

#include "assets/project.h"
#include "support/file_io.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yyjson.h>

#define RESOURCE_PATH_SIZE 4096

static bool initialized;
static bool packaged;
#ifndef __EMSCRIPTEN__
static char installed_root[RESOURCE_PATH_SIZE];
#endif
static char root[RESOURCE_PATH_SIZE];
static char shaders[RESOURCE_PATH_SIZE];
static char ui[RESOURCE_PATH_SIZE];
static char studio[RESOURCE_PATH_SIZE];
static char studio_content[RESOURCE_PATH_SIZE];

static void join(char *out, const char *base, const char *suffix)
{
    int length = snprintf(out, RESOURCE_PATH_SIZE, "%s%s", base, suffix);
    if (length < 0 || length >= RESOURCE_PATH_SIZE) {
        fprintf(stderr, "Engine resource path is too long\n");
        abort();
    }
}

const char *resource_install_root()
{
#ifdef __EMSCRIPTEN__
    return NULL;
#else
    if (!installed_root[0]) {
        char *directory = file_executable_dir();
        join(installed_root, directory, "/../share/chibi");
        free(directory);
    }
    return installed_root;
#endif
}

void resource_configure(const char *engine_path, const char *ui_path, const char *studio_path)
{
    assert(!initialized);
    assert(engine_path && engine_path[0]);
    assert(ui_path && ui_path[0]);
    assert(studio_path && studio_path[0]);
    join(root, engine_path, "");
    join(shaders, engine_path, "/shaders");
    join(ui, ui_path, "");
    join(studio, studio_path, "");
    join(studio_content, studio_path, "/content");
    packaged = true;
    initialized = true;
}

static void initialize()
{
    if (initialized) {
        return;
    }
#ifdef __EMSCRIPTEN__
    join(root, CHIBI_SOURCE_DIR, "");
#ifdef CHIBI_DISTRIBUTION
    packaged = true;
#endif
#else
    const char *override = getenv("CHIBI_RESOURCE_DIR");
    if (override && override[0]) {
        join(root, override, "");
        packaged = true;
    } else {
        join(root, resource_install_root(), "");
#ifdef CHIBI_DISTRIBUTION
        packaged = true;
#else
        struct stat info;
        packaged = stat(root, &info) == 0 && S_ISDIR(info.st_mode);
        if (!packaged) {
            join(root, CHIBI_SOURCE_DIR, "");
        }
#endif
    }
#endif
    join(shaders, root, packaged ? "/shaders" : "/src/render/shaders");
    join(ui, root, packaged ? "/ui" : "/src/editor/rmlui/resources");
#ifdef __EMSCRIPTEN__
    join(studio, "", "/projects/studio");
#else
    join(studio, root, packaged ? "/studio" : "/projects/studio");
#endif
    join(studio_content, studio, "/content");
    initialized = true;
}

const char *resource_root()
{
    initialize();
    return root;
}

const char *resource_shaders()
{
    initialize();
    return shaders;
}

const char *resource_ui()
{
    initialize();
    return ui;
}

const char *resource_studio()
{
    initialize();
    return studio;
}

const char *resource_studio_content()
{
    initialize();
    return studio_content;
}

const char *resource_workspace()
{
    initialize();
#ifdef __EMSCRIPTEN__
    return "";
#else
    return packaged ? NULL : root;
#endif
}

char *resource_info()
{
    initialize();
    yyjson_doc *manifest = NULL;
    const char *build_id = NULL;
    if (packaged) {
        char path[RESOURCE_PATH_SIZE];
        join(path, root, "/engine.json");
        yyjson_read_err error;
        manifest = yyjson_read_file(path, 0, NULL, &error);
        yyjson_val *object = yyjson_doc_get_root(manifest);
        yyjson_val *version = yyjson_obj_get(object, "project_version");
        build_id = yyjson_get_str(yyjson_obj_get(object, "build_id"));
        bool valid = build_id && build_id[0] && yyjson_is_int(version) &&
                     yyjson_get_int(version) == CHIBI_PROJECT_VERSION;
#ifdef CHIBI_DISTRIBUTION
        if (!valid) {
            fprintf(
                stderr,
                "Invalid engine manifest %s: %s\n",
                path,
                manifest ? "expected a build_id and matching project_version"
                         : error.msg
            );
            yyjson_doc_free(manifest);
            return NULL;
        }
#else
        if (!valid) {
            build_id = NULL;
        }
#endif
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, object);
    yyjson_mut_obj_add_str(doc, object, "resource_root", root);
    yyjson_mut_obj_add_str(doc, object, "shaders", shaders);
    yyjson_mut_obj_add_str(doc, object, "ui", ui);
    yyjson_mut_obj_add_str(doc, object, "studio", studio);
    yyjson_mut_obj_add_int(doc, object, "project_version", CHIBI_PROJECT_VERSION);
    if (build_id) {
        yyjson_mut_obj_add_str(doc, object, "build_id", build_id);
    } else {
        yyjson_mut_obj_add_null(doc, object, "build_id");
    }
#ifdef CHIBI_DISTRIBUTION
    yyjson_mut_obj_add_bool(doc, object, "distribution", true);
#else
    yyjson_mut_obj_add_bool(doc, object, "distribution", false);
#endif
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(manifest);
    return json;
}
