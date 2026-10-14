#ifndef CHIBI_BROWSER_COMPILER_H
#define CHIBI_BROWSER_COMPILER_H
#include <stdbool.h>
#include <stddef.h>
typedef struct workspace_documents workspace_documents;
typedef struct browser_compiler browser_compiler;
typedef enum
{
    BROWSER_COMPILER_IDLE,
    BROWSER_COMPILER_RUNNING,
    BROWSER_COMPILER_SUCCEEDED,
    BROWSER_COMPILER_FAILED,
    BROWSER_COMPILER_CANCELLED
} browser_compiler_state;
typedef struct
{
    browser_compiler_state state;
    const char *root, *artifact, *log, *error; /* borrowed until mutation/free */
} browser_compiler_result;
/* One worker/job per service. Documents are borrowed and must outlive it.
 * Each job freezes regular workspace bytes plus exact named open-buffer
 * overlays. Native hosts explicitly report unavailable. */
browser_compiler *browser_compiler_new(const char *root, workspace_documents *, char *error, size_t);
void browser_compiler_free(browser_compiler *);
bool browser_compiler_available(const browser_compiler *);
bool browser_compiler_start(browser_compiler *, const char *scene, bool authoring, char *error, size_t);
/* Appends one create-exclusive virtual source only to the frozen input overlay. */
bool browser_compiler_start_scene_source(
    browser_compiler *,
    const char *scene,
    const char *source,
    bool authoring,
    char *error,
    size_t
);
/* Append a bounded create-exclusive private source set to frozen inputs. */
bool browser_compiler_start_sources(
    browser_compiler *,
    const char *scene,
    const char *const *paths,
    const char *const *sources,
    int count,
    bool authoring,
    char *error,
    size_t
);
browser_compiler_result browser_compiler_poll(const browser_compiler *);
void browser_compiler_cancel(browser_compiler *);
/* Success-only ownership transfer. The runtime keeps this frozen root alive
 * for all external resource reads and calls snapshot_free when retired. */
char *browser_compiler_take_root(browser_compiler *);
void browser_compiler_snapshot_free(char *owned_root);
#endif
