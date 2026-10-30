#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.kdl"
#define CHIBI_APPLICATION_SIZE_MAX 16384
#define CHIBI_APPLICATION_TEXT_MAX 1024

/* The only manifest version this build opens. Bump it only when an existing
   project (in this repository or chibi-studio) would load wrong or fail to
   load, and edit those projects to the new format in the same change; see
   the first rule in AGENTS.md for what needs no bump. */
#define CHIBI_PROJECT_VERSION 39

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

/* Resolve a project directory, its project.kdl, or a legacy content directory.
 * A manifest at another version than CHIBI_PROJECT_VERSION fails. */
int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size);
/* chibi_project_open for explicitly selected assets: no start scene is
 * required. */
int chibi_project_open_content(chibi_project *project, const char *path, char *error, size_t error_size);
/* The owned path of the nearest directory at or above `start` that holds
 * project.kdl, or NULL when none does. */
char *chibi_project_find(const char *start);
/* Whether path is a directory without a project.kdl: a tree of packages
 * rather than a project. */
bool chibi_project_package_folder(const char *path);
/* Creates an empty project with content/scenes/Start.kdl as its start scene.
 * `root` must not already exist; its parent directory must exist. On failure
 * errno is EINVAL for a missing name, path or parent, EEXIST when `root`
 * exists, and otherwise the storage error. */
int chibi_project_create(const char *root, const char *name, char *error, size_t error_size);
void chibi_project_free(chibi_project *project);

/* The owned path of the folder the project at directory `root` keeps its
 * content in: the one its manifest names, joined to `root`, else
 * `root`/content. Reads the manifest at any version;
 * NULL with `error` set when it is malformed. */
char *chibi_project_content(const char *root, char *error, size_t error_size);
/* Returns an owned scene path inside the selected project; a path below
 * content/ names a file in its content folder. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
