#include "assets/project.h"

#include "support/xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yyjson.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error_size) {
        snprintf(error, error_size, "%s", message);
    }
}

static void set_errorf(char *error, size_t error_size, const char *format, const char *path)
{
    if (error_size) {
        snprintf(error, error_size, format, path);
    }
}

static bool is_directory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static char *join_path(const char *base, const char *name)
{
    if (name[0] == '/') {
        return xstrdup(name);
    }
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

static char *absolute_path(const char *path)
{
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) {
        return NULL;
    }
    return xstrdup(resolved);
}

static char *parent_path(const char *path)
{
    char *parent = xstrdup(path);
    char *slash = strrchr(parent, '/');
    if (!slash) {
        free(parent);
        return xstrdup(".");
    }
    if (slash == parent) {
        slash[1] = '\0';
        return parent;
    }
    *slash = '\0';
    return parent;
}

static char *basename_copy(const char *path)
{
    const char *slash = strrchr(path, '/');
    return xstrdup(slash ? slash + 1 : path);
}

static char *read_file(const char *path, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error_size) {
            snprintf(error, error_size, "Cannot read project manifest %s: %s", path, strerror(errno));
        }
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_errorf(error, error_size, "Cannot seek project manifest %s", path);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_errorf(error, error_size, "Cannot seek project manifest %s", path);
        return NULL;
    }
    char *json = xmalloc((size_t)length + 1);
    size_t read = fread(json, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        free(json);
        set_errorf(error, error_size, "Cannot read project manifest %s", path);
        return NULL;
    }
    json[read] = '\0';
    return json;
}

static int read_manifest(
    const char *path,
    char **name,
    char **content,
    char **start_scene,
    char *error,
    size_t error_size
)
{
    char *json = read_file(path, error, error_size);
    if (!json) {
        return -1;
    }
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    free(json);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(document);
        set_errorf(error, error_size, "Project manifest is not a JSON object: %s", path);
        return -1;
    }
    yyjson_val *version = yyjson_obj_get(root, "version");
    if (version && (!yyjson_is_int(version) || yyjson_get_int(version) != 1)) {
        yyjson_doc_free(document);
        set_errorf(error, error_size, "Unsupported project manifest version: %s", path);
        return -1;
    }
    yyjson_val *name_value = yyjson_obj_get(root, "name");
    yyjson_val *content_value = yyjson_obj_get(root, "content");
    yyjson_val *scene_value = yyjson_obj_get(root, "start_scene");
    if ((name_value && !yyjson_is_str(name_value)) ||
        (content_value && !yyjson_is_str(content_value)) ||
        (scene_value && !yyjson_is_str(scene_value))) {
        yyjson_doc_free(document);
        set_errorf(error, error_size, "Project manifest fields must be strings: %s", path);
        return -1;
    }
    const char *manifest_name = yyjson_get_str(name_value);
    const char *manifest_content = yyjson_get_str(content_value);
    const char *manifest_scene = yyjson_get_str(scene_value);
    if ((manifest_name && !manifest_name[0]) ||
        (manifest_content && !manifest_content[0]) ||
        (manifest_scene && !manifest_scene[0])) {
        yyjson_doc_free(document);
        set_errorf(error, error_size, "Project manifest contains an empty field: %s", path);
        return -1;
    }
    *name = manifest_name ? xstrdup(manifest_name) : NULL;
    *content = manifest_content ? xstrdup(manifest_content) : NULL;
    *start_scene = manifest_scene ? xstrdup(manifest_scene) : NULL;
    yyjson_doc_free(document);
    return 0;
}

static void choose_start_scene(chibi_project *project)
{
    if (project->start_scene) {
        return;
    }
    char *arena_2d = join_path(project->content, "scenes/Arena2D.json");
    if (is_file(arena_2d)) {
        project->start_scene = xstrdup("scenes/Arena2D.json");
        free(arena_2d);
        return;
    }
    free(arena_2d);
    char *arena_3d = join_path(project->content, "scenes/Arena3D.json");
    if (is_file(arena_3d)) {
        project->start_scene = xstrdup("scenes/Arena3D.json");
    }
    free(arena_3d);
}

void chibi_project_free(chibi_project *project)
{
    if (!project) {
        return;
    }
    free(project->root);
    free(project->content);
    free(project->name);
    free(project->start_scene);
    *project = (chibi_project){ 0 };
}

int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size)
{
    if (!project) {
        set_error(error, error_size, "Project output is required");
        return -1;
    }
    chibi_project_free(project);
    const char *requested = path && path[0] ? path : ".";
    char *input = absolute_path(requested);
    if (!input) {
        set_errorf(error, error_size, "Project path does not exist: %s", requested);
        return -1;
    }
    bool input_is_manifest = is_file(input);
    bool input_is_content = false;
    char *root = NULL;
    char *manifest = NULL;
    if (input_is_manifest) {
        const char *base = strrchr(input, '/');
        if (!base || strcmp(base + 1, "project.json")) {
            set_errorf(error, error_size, "Project file must be named project.json: %s", requested);
            free(input);
            return -1;
        }
        root = parent_path(input);
        manifest = input;
        input = NULL;
    } else if (is_directory(input)) {
        char *candidate = join_path(input, "project.json");
        if (is_file(candidate)) {
            root = xstrdup(input);
            manifest = candidate;
        } else {
            free(candidate);
            char *content = join_path(input, "content");
            char *scenes = join_path(input, "scenes");
            input_is_content = !is_directory(content) && is_directory(scenes);
            free(content);
            free(scenes);
            root = input_is_content ? parent_path(input) : xstrdup(input);
        }
    }
    if (!root) {
        free(input);
        free(manifest);
        set_errorf(error, error_size, "Project path is not a directory: %s", requested);
        return -1;
    }
    project->root = root;
    if (manifest &&
        read_manifest(manifest, &project->name, &project->content, &project->start_scene, error, error_size) !=
            0) {
        chibi_project_free(project);
        free(input);
        free(manifest);
        return -1;
    }
    free(manifest);
    if (input_is_content) {
        free(project->content);
        project->content = xstrdup(input);
    } else if (!project->content) {
        project->content = xstrdup("content");
    }
    char *content_path = project->content[0] == '/'
                           ? xstrdup(project->content)
                           : join_path(project->root, project->content);
    free(input);
    char *absolute_content = absolute_path(content_path);
    free(content_path);
    free(project->content);
    project->content = absolute_content;
    if (!project->content) {
        set_errorf(error, error_size, "Content directory does not exist: %s", requested);
        chibi_project_free(project);
        return -1;
    }
    if (!is_directory(project->content)) {
        set_errorf(
            error,
            error_size,
            "Project content directory does not exist: %s",
            project->content
        );
        chibi_project_free(project);
        return -1;
    }
    if (!project->name) {
        project->name = basename_copy(project->root);
    }
    choose_start_scene(project);
    if (!project->start_scene) {
        set_errorf(error, error_size, "Project has no start scene: %s", project->root);
        chibi_project_free(project);
        return -1;
    }
    if (error_size) {
        error[0] = '\0';
    }
    return 0;
}

char *chibi_project_scene_path(const chibi_project *project, const char *requested)
{
    if (!project || !project->root || !project->content) {
        return NULL;
    }
    const char *scene = requested && requested[0] ? requested : project->start_scene;
    if (!scene || !scene[0]) {
        return NULL;
    }
    if (scene[0] == '/') {
        return xstrdup(scene);
    }
    if (!strncmp(scene, "content/", strlen("content/"))) {
        return join_path(project->root, scene);
    }
    return join_path(project->content, scene);
}
