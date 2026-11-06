#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.chibi"
#define CHIBI_APPLICATION_SIZE_MAX 16384
#define CHIBI_APPLICATION_TEXT_MAX 1024

/* The compiled-source manifest version this build opens. Existing projects
 * were converted together with their assets and references. */
#define CHIBI_PROJECT_VERSION 33
/* Earlier source formats require offline conversion. */
#define CHIBI_PROJECT_OLDEST_VERSION 33

/* The optional `application` block: how a shipped game presents its window. */
typedef struct
{
    char *title;       /* NULL shows the project name */
    int width, height; /* zero selects the default window size */
    bool fullscreen;
    char *icon; /* project-relative PNG path, or NULL */
} chibi_application;

typedef struct
{
    char *root;
    char *content;
    char *name;
    char *start_scene;
    char *ui; /* the root ui document scenes show their UI on by default, or NULL */
    chibi_application application;
} chibi_project;

/* Resolve a project directory, its project.chibi, or a legacy content directory.
 * This API validates the compiled manifest and content paths. Hosts that load source must use
 * authoring_project_open with their explicit native table instead. Metadata
 * discovery should use chibi_project_inspect without advancing the manifest. */
int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size);
/* Retained SDK signature for source compatibility. Current-baseline opens
 * never invoke historical migration callbacks. */
typedef int (*chibi_project_migration_fn)(
    const char *content,
    const char *start_scene,
    int from,
    void *user,
    char *error,
    size_t size
);
int chibi_project_open_migrating(
    chibi_project *,
    const char *,
    chibi_project_migration_fn,
    void *user,
    char *,
    size_t
);
/* Current manifest/content validation for explicitly selected
 * assets. Unlike opening a launchable project, no start scene is required. */
int chibi_project_open_content_migrating(
    chibi_project *,
    const char *,
    chibi_project_migration_fn,
    void *user,
    char *,
    size_t
);
/* Read a current compiled-source project. */
int chibi_project_read(chibi_project *project, const char *path, char *error, size_t error_size);
/* Manifest-only inspection for the compiler launcher. */
int chibi_project_inspect(chibi_project *, const char *, char *, size_t);
/* The owned path of the nearest directory at or above `start` that holds
 * project.chibi, or NULL when none does. */
char *chibi_project_find(const char *start);
/* Whether path is a directory without a project.chibi: a tree of packages
 * rather than a project. */
bool chibi_project_package_folder(const char *path);
/* Creates an empty project with content/scenes/Start.chibi as its start scene.
 * `root` must not already exist; its parent directory must exist. On failure
 * errno is EINVAL for a missing name, path or parent, EEXIST when `root`
 * exists, and otherwise the storage error. */
int chibi_project_create(const char *root, const char *name, char *error, size_t error_size);
void chibi_project_free(chibi_project *project);
/* Validate the current project baseline; return zero, or -1 with `error`.
 * Executable hosts use authoring_project_migrate. */
int chibi_project_migrate(const char *path, char *error, size_t error_size);
int chibi_project_migrate_finalized(const char *, chibi_project_migration_fn, void *user, char *, size_t);

/* The owned path of the folder the project at directory `root` keeps its
 * content in: the one its manifest names, joined to `root`, else
 * `root`/content. Reads a current manifest without modifying it;
 * NULL with `error` set when it is malformed. */
char *chibi_project_content(const char *root, char *error, size_t error_size);
/* Returns an owned scene path inside the selected project; a path below
 * content/ names a file in its content folder. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
