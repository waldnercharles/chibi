#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.kdl"

/* The manifest version this build opens. Bump it when an asset contract
   change alters what projects saved by earlier builds mean or stops them from
   loading, and add the step that upgrades them in project_migrate.c. */
#define CHIBI_PROJECT_VERSION 12
/* Opening a project from this version on upgrades it in place first. */
#define CHIBI_PROJECT_OLDEST_VERSION 10

typedef struct
{
    char *root;
    char *content;
    char *name;
    char *start_scene;
    char *ui; /* the root ui document scenes show their UI on by default, or NULL */
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
/* Upgrades the project at `path` to CHIBI_PROJECT_VERSION in place. Returns
 * the number of version steps applied, or -1 with `error` set. */
int chibi_project_migrate(const char *path, char *error, size_t error_size);

/* Returns an owned scene path inside the selected project. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
