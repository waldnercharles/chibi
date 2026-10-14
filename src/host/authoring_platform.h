#ifndef CHIBI_HOST_AUTHORING_PLATFORM_H
#define CHIBI_HOST_AUTHORING_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>

typedef struct authoring_platform authoring_platform;
typedef enum
{
    AUTHORING_PLATFORM_PENDING,
    AUTHORING_PLATFORM_RUNNING,
    AUTHORING_PLATFORM_SUCCEEDED,
    AUTHORING_PLATFORM_CANCELLED,
    AUTHORING_PLATFORM_FAILED,
    AUTHORING_PLATFORM_UNAVAILABLE,
} authoring_platform_state;
typedef struct
{
    bool ok;
    int request;
    const char *state, *status, *message, *value, *log;
    double progress;
    int exit_code;
} authoring_platform_result;
typedef struct
{
    bool native, web, clipboard, file_picker, folder_picker, compile, export, focused;
    const char *platform, *theme;
    int width, height, close_request;
    double scale;
} authoring_platform_environment;
typedef struct
{
    int slot, buttons; /* Buttons use the engine PadControl mask. */
    bool connected;
    double left_x, left_y, right_x, right_y, left_trigger, right_trigger;
} authoring_physical_controller;
typedef struct
{
    const char *root; /* Canonical workspace root, or NULL before a workspace is open. */
    const char *compiler, *launcher; /* Explicit executable paths, no PATH search. */
    void *window; /* Borrowed SDL_Window or NULL; host owns its lifetime. */
    /* Optional non-SDL platform adapter. Borrows arguments only for the call;
     * completion is delivered on the frame thread through resolve. Returning
     * true takes responsibility for completion/cancellation. No script can
     * register adapters or executable paths. */
    void *adapter;
    bool (*request)(void *, authoring_platform *, int, const char *, const char *, double, double, int);
    void (*cancel)(void *, int);
} authoring_platform_config;
enum
{
    AUTHORING_PLATFORM_REQUESTS_MAX = 8,
    AUTHORING_PLATFORM_TEXT_MAX = 16384,
    AUTHORING_PLATFORM_FILE_MAX = 16 * 1024 * 1024,
    AUTHORING_PLATFORM_TOTAL_MAX = 64 * 1024 * 1024,
    AUTHORING_PLATFORM_LOG_MAX = 32768,
};
/* Frame-thread confined service with bounded asynchronous native/browser
 * adapters. No application commands, panels or selection state. Result strings
 * borrow service storage until request release/free. Native dialogs may remain
 * visible after cancellation; their late callbacks cannot access freed services.
 * All jobs use host-selected tools with fixed argv and fresh output locations.
 * compile accepts an optional workspace-relative scene in Value; empty uses
 * the manifest default. Other compiler flags are not script-controlled.
 * Tick while a request is running; 60 s job deadline, 2 s cancellation escalation.
 * Browser compiler/export require an actual backend and report Unavailable here.
 * Native file import copies a selected regular file exclusively into the workspace;
 * it does not decode, convert or automatically publish the asset.
 * create-workspace accepts JSON {Name,Files:[{Path,Text}]} in Value. Native
 * creation consumes a completed choose-folder Source and creates Parent/Name
 * exclusively; browser creation stages under a fresh owned /tmp directory.
 * It does not activate the result or change the current workspace. */
authoring_platform *authoring_platform_new(const authoring_platform_config *, char *error, size_t);
void authoring_platform_free(authoring_platform *);
authoring_platform_result authoring_platform_request(
    authoring_platform *,
    const char *operation,
    const char *value,
    double number,
    double other,
    int source
);
authoring_platform_result authoring_platform_poll(authoring_platform *, int request);
authoring_platform_result authoring_platform_cancel(authoring_platform *, int request);
bool authoring_platform_release(authoring_platform *, int request);
void authoring_platform_tick(authoring_platform *);
bool authoring_platform_resolve(
    authoring_platform *,
    int request,
    authoring_platform_state,
    const char *status,
    const char *message,
    const char *value
);
authoring_platform_environment authoring_platform_environment_get(authoring_platform *);
/* Host consumes these generic requests. UI scale is a host rendering preference;
 * this service does not mutate document styles or application preference storage. */
bool authoring_platform_quit_requested(const authoring_platform *);
/* Target IME request combined by host with its own UI focus. */
bool authoring_platform_text_input_requested(const authoring_platform *);
/* Trusted host frame snapshot, including deterministic replay input. Before
 * the first snapshot, environment queries use the native/browser window. */
void authoring_platform_set_frame_focus(authoring_platform *, bool focused);
/* Host close gestures are requests. Authored application policy decides when
 * to issue quit after handling pending edits. Environment exposes a counter. */
void authoring_platform_close_request(authoring_platform *);
double authoring_platform_ui_scale(const authoring_platform *);
/* activate-project queues a validated canonical root. Host prepares replacement
 * services/world without destroying the old application, then acknowledges the
 * request between frames. Getter borrows until completion/cancel/free. Failure
 * leaves the current root unchanged. The host copies it before freeing service. */
const char *authoring_platform_pending_project(const authoring_platform *);
void authoring_platform_project_complete(authoring_platform *, bool success, const char *error);
#endif
