#ifndef CHIBI_ASSETS_PROJECT_H
#define CHIBI_ASSETS_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#define CHIBI_PROJECT_MANIFEST "project.kdl"
#define CHIBI_APPLICATION_SIZE_MAX 16384
#define CHIBI_APPLICATION_TEXT_MAX 1024

/* The manifest version this build opens. Bump it only when an existing
   project (in this repository or chibi-studio) would load wrong or fail to
   load and a step in project_migrate.c would rewrite it; see the first rule
   in AGENTS.md for what needs no bump. */
#define CHIBI_PROJECT_VERSION 39
/* Opening a project from this version on upgrades it in place first; an
   older one needs an older SDK's chibi_ctl --migrate. */
#define CHIBI_PROJECT_OLDEST_VERSION 38

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
 * This document-only API runs structural migrations, not catalog-dependent
 * executable normalization. Hosts that load source must use
 * authoring_project_open with their explicit native table instead. Metadata
 * discovery should use chibi_project_inspect without advancing the manifest. */
int chibi_project_open(chibi_project *project, const char *path, char *error, size_t error_size);
/* An authoring host may finish catalog-dependent source migration after every
 * document step succeeds and before the manifest is replaced. Failure leaves
 * the manifest at its old version so the idempotent upgrade can be retried.
 * Inputs and user data are borrowed only during this call. The low document
 * package has no compiler/native registry dependency. */
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
/* The same migration and manifest/content validation for explicitly selected
 * assets. Unlike opening a launchable project, no start scene is required. */
int chibi_project_open_content_migrating(
    chibi_project *,
    const char *,
    chibi_project_migration_fn,
    void *user,
    char *,
    size_t
);
/* chibi_project_open without upgrading: a project saved by an earlier build
 * fails, naming --migrate. */
int chibi_project_read(chibi_project *project, const char *path, char *error, size_t error_size);
/* Manifest-only inspection for the compiler launcher. Accepts supported older
 * manifests without upgrading their sources; never use it to load runtime data. */
int chibi_project_inspect(chibi_project *, const char *, char *, size_t);
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
/* Structural document-only upgrade; executable hosts use authoring_project_migrate.
 * Upgrades the project at `path` to CHIBI_PROJECT_VERSION in place. Returns
 * the number of version steps applied, or -1 with `error` set. */
int chibi_project_migrate(const char *path, char *error, size_t error_size);
int chibi_project_migrate_finalized(const char *, chibi_project_migration_fn, void *user, char *, size_t);

/* The owned path of the folder the project at directory `root` keeps its
 * content in: the one its manifest names, joined to `root`, else
 * `root`/content. Reads the manifest at any version without upgrading it;
 * NULL with `error` set when it is malformed. */
char *chibi_project_content(const char *root, char *error, size_t error_size);
/* Returns an owned scene path inside the selected project; a path below
 * content/ names a file in its content folder. */
char *chibi_project_scene_path(const chibi_project *project, const char *requested);

#endif
