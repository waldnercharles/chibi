#ifndef CHIBI_WORKSPACE_HOST_H
#define CHIBI_WORKSPACE_HOST_H
#include "authoring/observations.h"
#include "runtime/behaviors/source.h"
#include "runtime/native_capabilities.h"
#include "runtime/player_prepared.h"

typedef struct workspace_host workspace_host;
struct render_ui;
/* Optional host-owned authoring services. The native catalog outlives these
 * sessions; the sessions outlive every source registry bound to them. Create
 * and free while the graphics context is alive. No application policy. */
typedef struct
{
    /* NULL root creates an inactive host with platform, messages, DOM,
     * observations and idle workspace info only. No user directory is read. */
    const char *root, *compiler, *launcher;
    /* Explicit old-project opening intent while root is NULL. Copied into idle
     * workspace info for authored migration review; no active services open. */
    const char *requested_root;
    int requested_version;
    /* Optional versioned document state, borrowed only during construction.
     * Restored into a fresh session before any dependent service is created. */
    const void *document_state;
    size_t document_state_bytes;
    native_capabilities *native;
    void *window;
    player_host player;
    behavior_backend backend;
} workspace_host_config;
workspace_host *workspace_host_new(const workspace_host_config *, char *error, size_t size);
bool workspace_host_bind(workspace_host *, source_registry *, char *error, size_t size);
void workspace_host_pump(workspace_host *, struct render_ui *);
/* Clear borrowed live DOM context before its UI owner retires. */
void workspace_host_detach_ui(workspace_host *);
bool workspace_host_quit_requested(const workspace_host *);
void workspace_host_close_request(workspace_host *);
double workspace_host_ui_scale(const workspace_host *);
/* Explicit text-focus request from a scoped authored target. */
bool workspace_host_text_input_requested(const workspace_host *);
/* The same trusted focus snapshot supplied to the host UI for this frame. */
void workspace_host_set_frame_focus(workspace_host *, bool focused);
const char *workspace_host_pending_project(const workspace_host *);
void workspace_host_project_complete(workspace_host *, bool success, const char *error);
void workspace_host_record_log(workspace_host *, double time, const char *program, const char *text);
void workspace_host_record_timing(workspace_host *, authoring_timing);
void workspace_host_free(workspace_host *);
#endif
