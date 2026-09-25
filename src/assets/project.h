#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.yaml"

/* The manifest version this build opens. Bump it when an asset contract
   change stops projects saved by earlier builds from loading. */
#define CHIBI_PROJECT_VERSION 1

typedef struct
{
    char *root;
    char *content;
    char *name;
    char *start_scene;
} chibi_project;

/* Resolve a project directory, its project.yaml, or a legacy content directory. */
int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size);
void chibi_project_free(chibi_project *project);

/* Returns an owned scene path inside the selected project. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
