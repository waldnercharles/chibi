#ifndef CHIBI_HOST_AUTHORING_PLATFORM_INTERNAL_H
#define CHIBI_HOST_AUTHORING_PLATFORM_INTERNAL_H
#include "host/authoring_platform.h"
#ifdef _WIN32
#include "host/process.h"
#include "support/rooted_files.h"
#endif

#include <SDL3/SDL_atomic.h>
#include <stdint.h>
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <sys/types.h>
#endif

typedef struct
{
    SDL_SpinLock lock;
    int references;
    bool ready;
    authoring_platform_state state;
    char value[4096], message[512];
} platform_mailbox;
typedef struct platform_request
{
    int handle, token, source;
    bool external;
    authoring_platform_state state;
    char operation[32], status[48], message[512], value[4096], input[4096];
    char *text;
    platform_mailbox *mailbox;
    uint64_t started, stop_started;
    int exit_code;
    double progress;
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
    pid_t pid;
    int output;
    size_t log_bytes;
    bool output_eof, process_finished;
    int import_source, import_output, import_parent;
    char import_temporary[96], import_name[256];
    size_t import_size, import_done;
    int64_t import_mtime, import_ctime, import_mtime_ns, import_ctime_ns;
#elif defined(_WIN32)
    chibi_process process;
    rooted_files *job_files, *import_files;
    rooted_stream *input_stream, *import_stream;
    rooted_file_info import_info;
    size_t log_bytes, import_done;
    char import_temporary[4096], import_destination[4096];
#endif
} platform_request;
struct authoring_platform
{
    platform_request requests[AUTHORING_PLATFORM_REQUESTS_MAX];
    int next_handle;
    char *root, *compiler, *launcher;
    char *project;
    int project_request, close_request;
    void *window;
    bool quit, text_input, frame_focus_known, frame_focus;
    void *adapter;
    bool (*request)(void *, authoring_platform *, int, const char *, const char *, double, double, int);
    void (*cancel)(void *, int);
    double scale;
};
void authoring_platform_complete(
    platform_request *,
    authoring_platform_state,
    const char *status,
    const char *message,
    const char *value
);
void authoring_platform_mailbox_release(platform_mailbox *);
void authoring_platform_start_job(authoring_platform *, platform_request *);
void authoring_platform_poll_job(platform_request *);
void authoring_platform_cancel_job(platform_request *);
void authoring_platform_free_job(platform_request *);
#endif
