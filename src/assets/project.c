#include "assets/project.h"

#include "support/asset_text.h"
#include "support/asset_value.h"
#include "support/text_edit.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

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

static char *join_path(const char *base, const char *name);
static char *parent_path(const char *path);

/* `flags` adds open flags, such as O_EXCL | O_NOFOLLOW for a staging path. */
static int write_asset_file(const char *path, const char *text, int flags, char *error, size_t error_size)
{
    char *canonical = asset_text_to_disk(text, error, error_size);
    if (!canonical) {
        return -1;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | flags, 0666);
    FILE *file = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (fd >= 0 && !file) {
        close(fd);
    }
    if (!file) {
        if (error_size) {
            snprintf(error, error_size, "Cannot create %s: %s", path, strerror(errno));
        }
        free(canonical);
        return -1;
    }
    size_t size = strlen(canonical);
    bool written = fwrite(canonical, 1, size, file) == size;
    written = fclose(file) == 0 && written;
    if (!written) {
        if (error_size) {
            snprintf(error, error_size, "Cannot finish writing %s", path);
        }
        unlink(path);
    }
    free(canonical);
    return written ? 0 : -1;
}

static char *manifest_text(const char *name)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "version", CHIBI_PROJECT_VERSION);
    av_obj_add_strcpy(doc, root, "name", name);
    av_obj_add_str(doc, root, "content", "content");
    av_obj_add_str(doc, root, "start_scene", "scenes/Start.kdl");
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

static char *start_scene_text()
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "version", 1);
    av_obj_add_str(doc, root, "name", "Start");
    av_obj_add_arr(doc, root, "entities");
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

int chibi_project_create(const char *root, const char *name, char *error, size_t error_size)
{
    if (error_size) {
        error[0] = '\0';
    }
    if (!root || !root[0] || !name || !name[0]) {
        set_error(error, error_size, "Project path and name are required");
        errno = EINVAL;
        return -1;
    }
    bool named = false;
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        named |= *c > ' ';
    }
    if (!named) {
        set_error(error, error_size, "Enter a project name");
        errno = EINVAL;
        return -1;
    }
    if (is_directory(root) || is_file(root)) {
        set_errorf(error, error_size, "Project already exists: %s", root);
        errno = EEXIST;
        return -1;
    }
    char *parent = parent_path(root);
    struct stat info;
    /* mkdir reports a parent that exists but cannot be inspected. */
    bool missing = stat(parent, &info) ? errno == ENOENT || errno == ENOTDIR
                                       : !S_ISDIR(info.st_mode);
    if (missing) {
        set_errorf(error, error_size, "Project parent directory does not exist: %s", parent);
        free(parent);
        errno = EINVAL;
        return -1;
    }
    free(parent);
    errno = 0;
    if (mkdir(root, 0755) != 0) {
        if (error_size) {
            snprintf(error, error_size, "Cannot create project: %s", strerror(errno));
        }
        return -1;
    }
    char *content = join_path(root, "content");
    char *scenes = join_path(content, "scenes");
    bool folders = mkdir(content, 0755) == 0 && mkdir(scenes, 0755) == 0;
    char *manifest_path = join_path(root, CHIBI_PROJECT_MANIFEST);
    char *scene_path = join_path(scenes, "Start" DECLARATIVE_ASSET_EXTENSION);
    char *manifest = manifest_text(name);
    char *scene = start_scene_text();
    bool written = folders && manifest && scene &&
                   write_asset_file(manifest_path, manifest, 0, error, error_size) == 0 &&
                   write_asset_file(scene_path, scene, 0, error, error_size) == 0;
    if (!written) {
        int failure = errno ? errno : EIO;
        unlink(scene_path);
        unlink(manifest_path);
        rmdir(scenes);
        rmdir(content);
        rmdir(root);
        errno = failure;
        if (!error_size || !error[0]) {
            set_error(error, error_size, "Could not create project files");
        }
    } else if (error_size) {
        error[0] = '\0';
    }
    free(manifest);
    free(scene);
    free(scene_path);
    free(manifest_path);
    free(scenes);
    free(content);
    return written ? 0 : -1;
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
    if (length < 0 || length > 512 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_errorf(error, error_size, "Cannot read bounded project manifest %s", path);
        return NULL;
    }
    char *text = xmalloc((size_t)length + 1);
    size_t read = fread(text, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        free(text);
        set_errorf(error, error_size, "Cannot read project manifest %s", path);
        return NULL;
    }
    text[read] = '\0';
    return text;
}

/* A project-relative path that stays inside the project. */
static bool contained_path(const char *path)
{
    if (path[0] == '/' || strchr(path, '\\')) {
        return false;
    }
    for (const char *part = path;;) {
        size_t length = strcspn(part, "/");
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && !strncmp(part, "..", 2))) {
            return false;
        }
        if (!part[length]) {
            return true;
        }
        part += length + 1;
    }
}

static bool window_size(const av_value *value, int *out)
{
    if (!value) {
        return true;
    }
    if (!av_is_int(value) || av_get_int(value) < 1 ||
        av_get_int(value) > CHIBI_APPLICATION_SIZE_MAX) {
        return false;
    }
    *out = av_get_int(value);
    return true;
}

static bool application_text(const av_value *value)
{
    const char *text = av_get_str(value);
    return !value || (text && text[0] && strlen(text) <= CHIBI_APPLICATION_TEXT_MAX);
}

/* A .png path inside the project that CF's virtual file system can mount. */
static bool icon_path(const char *path)
{
    size_t length = strlen(path);
    return contained_path(path) && !strchr(path, ':') && length > 4 &&
           !strcasecmp(path + length - 4, ".png");
}

static int read_application(
    av_value *block,
    chibi_application *application,
    const char *path,
    char *error,
    size_t error_size
)
{
    if (!block) {
        return 0;
    }
    if (!av_is_obj(block)) {
        set_errorf(error, error_size, "Project application must be a block: %s", path);
        return -1;
    }
    av_value *title = av_obj_get(block, "title");
    av_value *fullscreen = av_obj_get(block, "fullscreen");
    av_value *icon = av_obj_get(block, "icon");
    if (!application_text(title) || !application_text(icon) ||
        (fullscreen && !av_is_bool(fullscreen))) {
        snprintf(
            error,
            error_size,
            "Project application title and icon must be text of 1 to %d bytes, "
            "and "
            "fullscreen a boolean: %s",
            CHIBI_APPLICATION_TEXT_MAX,
            path
        );
        return -1;
    }
    if (!window_size(av_obj_get(block, "width"), &application->width) ||
        !window_size(av_obj_get(block, "height"), &application->height)) {
        snprintf(
            error,
            error_size,
            "Project application width and height must be 1 to %d: %s",
            CHIBI_APPLICATION_SIZE_MAX,
            path
        );
        return -1;
    }
    if (icon && !icon_path(av_get_str(icon))) {
        set_errorf(error, error_size, "Project application icon must be a .png path inside the project: %s", path);
        return -1;
    }
    application->title = title ? xstrdup(av_get_str(title)) : NULL;
    application->fullscreen = fullscreen && av_get_bool(fullscreen);
    application->icon = icon ? xstrdup(av_get_str(icon)) : NULL;
    return 0;
}

/* The manifest at `path` parsed, or NULL with why in `error`. */
static av_doc *manifest_document(const char *path, char *error, size_t error_size)
{
    char *text = read_file(path, error, error_size);
    if (!text) {
        return NULL;
    }
    char parse_error[128];
    av_doc *document = asset_text_read(text, parse_error, sizeof parse_error);
    free(text);
    if (!document) {
        snprintf(error, error_size, "Invalid project manifest %s: %s", path, parse_error);
        return NULL;
    }
    if (!av_is_obj(av_doc_root(document))) {
        av_doc_free(document);
        set_errorf(error, error_size, "Project manifest must contain a code asset object: %s", path);
        return NULL;
    }
    return document;
}

static int read_manifest(const char *path, chibi_project *project, char *error, size_t error_size)
{
    av_doc *document = manifest_document(path, error, error_size);
    if (!document) {
        return -1;
    }
    av_value *root = av_doc_root(document);
    av_value *version = av_obj_get(root, "version");
    if (!av_is_int(version)) {
        av_doc_free(document);
        set_errorf(error, error_size, "Project manifest needs an integer version: %s", path);
        return -1;
    }
    int found = (int)av_get_int(version);
    /* Checked first: another version's fields may mean something else. */
    if (found < CHIBI_PROJECT_VERSION) {
        av_doc_free(document);
        snprintf(
            error,
            error_size,
            "Project version %d is older than version %d, the only one this "
            "build opens: %s; edit it to the current format, or open it with "
            "the older Chibi SDK that wrote it and upgrade it there",
            found,
            CHIBI_PROJECT_VERSION,
            path
        );
        return -1;
    }
    if (found > CHIBI_PROJECT_VERSION) {
        av_doc_free(document);
        snprintf(error, error_size, "Project version %d needs a newer build; this build opens version %d: %s", found, CHIBI_PROJECT_VERSION, path);
        return -1;
    }
    av_value *name_value = av_obj_get(root, "name");
    av_value *content_value = av_obj_get(root, "content");
    av_value *scene_value = av_obj_get(root, "start_scene");
    av_value *ui_value = av_obj_get(root, "ui");
    if ((name_value && !av_is_str(name_value)) ||
        (content_value && !av_is_str(content_value)) ||
        (scene_value && !av_is_str(scene_value)) ||
        (ui_value && !av_is_str(ui_value))) {
        av_doc_free(document);
        set_errorf(error, error_size, "Project manifest fields must be strings: %s", path);
        return -1;
    }
    const char *manifest_name = av_get_str(name_value);
    const char *manifest_content = av_get_str(content_value);
    const char *manifest_scene = av_get_str(scene_value);
    const char *manifest_ui = av_get_str(ui_value);
    if ((manifest_name && !manifest_name[0]) ||
        (manifest_content && !manifest_content[0]) ||
        (manifest_scene && !manifest_scene[0]) || (manifest_ui && !manifest_ui[0])) {
        av_doc_free(document);
        set_errorf(error, error_size, "Project manifest contains an empty field: %s", path);
        return -1;
    }
    project->name = manifest_name ? xstrdup(manifest_name) : NULL;
    project->content = manifest_content ? xstrdup(manifest_content) : NULL;
    project->start_scene = manifest_scene ? xstrdup(manifest_scene) : NULL;
    project->ui = manifest_ui ? xstrdup(manifest_ui) : NULL;
    int result = read_application(av_obj_get(root, "application"), &project->application, path, error, error_size);
    av_doc_free(document);
    return result;
}

static char *scene_path(const char *root, const char *content, const char *scene)
{
    if (scene[0] == '/') {
        return xstrdup(scene);
    }
    if (!strncmp(scene, "content/", strlen("content/"))) {
        return join_path(root, scene);
    }
    return join_path(content, scene);
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
    free(project->ui);
    free(project->application.title);
    free(project->application.icon);
    *project = (chibi_project){ 0 };
}

char *chibi_project_find(const char *start)
{
    char *directory = absolute_path(start);
    while (directory) {
        char *manifest = join_path(directory, CHIBI_PROJECT_MANIFEST);
        bool found = is_file(manifest);
        free(manifest);
        if (found) {
            return directory;
        }
        if (!strcmp(directory, "/")) {
            break;
        }
        char *parent = parent_path(directory);
        free(directory);
        directory = parent;
    }
    free(directory);
    return NULL;
}

bool chibi_project_package_folder(const char *path)
{
    char *manifest = join_path(path, CHIBI_PROJECT_MANIFEST);
    bool folder = is_directory(path) && !is_file(manifest);
    free(manifest);
    return folder;
}

/* A launch needs a start scene; opening selected content does not. */
typedef enum
{
    PROJECT_LAUNCH,
    PROJECT_CONTENT,
} project_open_mode;

static int open_project(
    chibi_project *project,
    const char *path,
    project_open_mode mode,
    char *error,
    size_t error_size
)
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
        if (!base || strcmp(base + 1, CHIBI_PROJECT_MANIFEST)) {
            set_errorf(error, error_size, "Project file must be named " CHIBI_PROJECT_MANIFEST ": %s", requested);
            free(input);
            return -1;
        }
        root = parent_path(input);
        manifest = input;
        input = NULL;
    } else if (is_directory(input)) {
        char *candidate = join_path(input, CHIBI_PROJECT_MANIFEST);
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
    int status = manifest ? read_manifest(manifest, project, error, error_size) : 0;
    free(manifest);
    if (status) {
        chibi_project_free(project);
        free(input);
        return -1;
    }
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
    free(project->content);
    project->content = absolute_path(content_path);
    free(content_path);
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
    if (mode == PROJECT_LAUNCH && !project->start_scene) {
        set_errorf(error, error_size, "Project has no start scene: %s", project->root);
        chibi_project_free(project);
        return -1;
    }
    if (error_size) {
        error[0] = '\0';
    }
    return 0;
}

int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size)
{
    return open_project(project, path, PROJECT_LAUNCH, error, error_size);
}

int chibi_project_open_content(chibi_project *project, const char *path, char *error, size_t error_size)
{
    return open_project(project, path, PROJECT_CONTENT, error, error_size);
}

char *chibi_project_content(const char *root, char *error, size_t error_size)
{
    char *manifest = join_path(root, CHIBI_PROJECT_MANIFEST);
    if (!is_file(manifest)) {
        free(manifest);
        return join_path(root, "content");
    }
    /* Only the content folder has to be readable here, so validate_manifest
     * can list the manifest's other problems alongside the project's. */
    av_doc *document = manifest_document(manifest, error, error_size);
    if (!document) {
        free(manifest);
        return NULL;
    }
    av_value *value = av_obj_get(av_doc_root(document), "content");
    const char *folder = value ? av_get_str(value) : "content";
    char *content = folder && folder[0] ? join_path(root, folder) : NULL;
    if (!content) {
        set_errorf(error, error_size, "Project manifest content must be a folder name: %s", manifest);
    }
    av_doc_free(document);
    free(manifest);
    return content;
}

char *chibi_project_scene_path(const chibi_project *project, const char *requested)
{
    if (!project || !project->root || !project->content) {
        return NULL;
    }
    const char *scene = requested && requested[0] ? requested : project->start_scene;
    return scene && scene[0] ? scene_path(project->root, project->content, scene)
                             : NULL;
}
