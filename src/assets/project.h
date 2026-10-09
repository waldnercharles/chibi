#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.kdl"
#define CHIBI_APPLICATION_SIZE_MAX 16384
#define CHIBI_APPLICATION_TEXT_MAX 1024

/* The manifest version this build opens. Bump it when an asset contract
   change stops projects saved by earlier builds from loading. */
#define CHIBI_PROJECT_VERSION 10

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

/* Resolve a project directory, its project.kdl, or a legacy content directory. */
int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size);
/* The owned path of the nearest directory at or above `start` that holds
 * project.kdl, or NULL when none does. */
char *chibi_project_find(const char *start);
/* Creates an empty project with content/scenes/Start.kdl as its start scene.
 * `root` must not already exist; its parent directory must exist. */
int chibi_project_create(const char *root, const char *name, char *error, size_t error_size);
void chibi_project_free(chibi_project *project);

/* Returns an owned scene path inside the selected project. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
