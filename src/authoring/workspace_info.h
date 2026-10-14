#ifndef CHIBI_WORKSPACE_INFO_H
#define CHIBI_WORKSPACE_INFO_H
#include <stdbool.h>
#include <stddef.h>

typedef struct
{
    char *root, *name, *content, *start_scene;
    int version;
} workspace_manifest;
/* Pure manifest interpretation. Result owns strings; Content and StartScene
 * are normalized workspace-relative paths. No disk access or migration. */
bool workspace_manifest_parse(const char *root, const char *text, workspace_manifest *, char *error, size_t);
void workspace_manifest_free(workspace_manifest *);
typedef struct workspace_info workspace_info;
/* An idle session has no reader or project paths and reports the current
 * manifest version. It performs no filesystem access. */
workspace_info *workspace_info_idle();
bool workspace_info_active(const workspace_info *);
/* Session metadata set only after a host imports validated document state. */
void workspace_info_set_documents_restored(workspace_info *, bool);
bool workspace_info_documents_restored(const workspace_info *);
/* Explicit host-selected opening intent, copied only into an idle session.
 * No file is read or upgraded. Active sessions refuse pending metadata. */
bool workspace_info_set_pending_project(workspace_info *, const char *root, int version);
const char *workspace_info_pending_root(const workspace_info *);
int workspace_info_pending_version(const workspace_info *);
/* Canonicalizes root once, caches its bounded current-version regular manifest
 * through the no-follow workspace reader. Query performs no disk access. */
workspace_info *workspace_info_new(const char *root, char *error, size_t);
void workspace_info_free(workspace_info *);
bool workspace_info_refresh(workspace_info *, char *error, size_t);
const workspace_manifest *workspace_info_get(const workspace_info *);
#endif
