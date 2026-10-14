#ifdef _WIN32
#include "host/authoring_platform_internal.h"
#include "support/xalloc.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static _Atomic unsigned sequence;
static void failed(platform_request *r, const char *status, const char *message)
{
    authoring_platform_complete(r, AUTHORING_PLATFORM_FAILED, status, message, NULL);
}
static bool folder(authoring_platform *p, platform_request *r)
{
    rooted_files *root = rooted_files_open(p->root);
    rooted_files *jobs = root
                           ? rooted_files_directory(root, ".chibi/platform-jobs", true)
                           : NULL;
    if (root) {
        rooted_files_close(root);
    }
    if (!jobs) {
        return false;
    }
    for (int retry = 0; retry < 64; retry++) {
        char name[96];
        snprintf(name, sizeof name, "job-%lu-%u", GetCurrentProcessId(), atomic_fetch_add(&sequence, 1));
        r->job_files = rooted_files_directory_new(jobs, name);
        if (r->job_files || errno != EEXIST) {
            break;
        }
    }
    rooted_files_close(jobs);
    return r->job_files != NULL;
}
static void start_import(authoring_platform *p, platform_request *r)
{
    platform_request *selected = NULL;
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        platform_request *candidate = &p->requests[i];
        if (candidate->handle == r->source &&
            candidate->state == AUTHORING_PLATFORM_SUCCEEDED &&
            !strcmp(candidate->operation, "choose-file")) {
            selected = candidate;
            break;
        }
    }
    if (!selected || !rooted_files_path_valid(r->value)) {
        failed(r, "invalid", "Import requires a completed selected-file request and workspace-relative destination");
        return;
    }
    r->import_files = rooted_files_open(p->root);
    r->input_stream = rooted_stream_external(
        selected->text ? selected->text : selected->value
    );
    if (!r->import_files || !r->input_stream ||
        !rooted_stream_info(r->input_stream, &r->import_info) ||
        !r->import_info.regular || r->import_info.bytes > AUTHORING_PLATFORM_FILE_MAX) {
        failed(r, "unsafe-file", "Selected source must be a bounded regular file without reparse traversal");
        return;
    }
    snprintf(r->import_destination, sizeof r->import_destination, "%s", r->value);
    const char *slash = strrchr(r->value, '/');
    size_t parent = slash ? (size_t)(slash - r->value) + 1 : 0;
    if (parent + 96 >= sizeof r->import_temporary) {
        failed(r, "limit", "Import destination exceeds path limit");
        return;
    }
    memcpy(r->import_temporary, r->value, parent);
    for (int retry = 0; retry < 64; retry++) {
        snprintf(
            r->import_temporary + parent,
            sizeof r->import_temporary - parent,
            ".platform-import-%lu-%u.tmp",
            GetCurrentProcessId(),
            atomic_fetch_add(&sequence, 1)
        );
        r->import_stream = rooted_files_stream(r->import_files, r->import_temporary, ROOTED_STREAM_CREATE);
        if (r->import_stream || errno != EEXIST) {
            break;
        }
    }
    if (!r->import_stream) {
        r->import_temporary[0] = 0;
        failed(r, "io", "Cannot create exclusive import stage");
    }
}
void authoring_platform_start_job(authoring_platform *p, platform_request *r)
{
    if (!strcmp(r->operation, "migrate-project")) {
        if (!p->compiler[0]) {
            failed(r, "unavailable", "Configured compiler is unavailable");
            return;
        }
        const char *args[] = { p->compiler,      "--project-root", r->input,
                               "--migrate-copy", r->value,         NULL };
        if (!chibi_process_start_capture(&r->process, args, NULL)) {
            failed(r, "spawn", "Could not start project migration");
            return;
        }
        r->text = xcalloc(AUTHORING_PLATFORM_LOG_MAX + 1, 1);
        return;
    }
    if (!strcmp(r->operation, "import-file")) {
        start_import(p, r);
        return;
    }
    bool compile = !strcmp(r->operation, "compile");
    const char *tool = compile ? p->compiler : p->launcher;
    if (!*tool || !(tool[0] == '/' || (tool[0] && tool[1] == ':' &&
                                       (tool[2] == '/' || tool[2] == '\\')))) {
        authoring_platform_complete(
            r,
            AUTHORING_PLATFORM_UNAVAILABLE,
            "unavailable",
            "Host has no explicit compiler/export executable",
            NULL
        );
        return;
    }
    if (compile && r->input[0]) {
        rooted_files *root = rooted_files_open(p->root);
        rooted_file_info info;
        bool valid = root && rooted_files_stat(root, r->input, &info) &&
                     info.regular && info.bytes <= 1024 * 1024;
        if (root) {
            rooted_files_close(root);
        }
        if (!valid) {
            failed(r, "unsafe-scene", "Compile scene must be a bounded regular workspace file");
            return;
        }
    }
    char scene[8194] = "";
    if (compile && r->input[0]) {
        int length = snprintf(scene, sizeof scene, "%s/%s", p->root, r->input);
        if (length < 0 || (size_t)length >= sizeof scene) {
            failed(r, "limit", "Compile scene host path exceeds limit");
            return;
        }
    }
    if (!folder(p, r)) {
        failed(r, "io", "Cannot create exclusive job output folder");
        return;
    }
    char *path = rooted_files_host_path(r->job_files);
    int output_count = path
                         ? snprintf(r->value, sizeof r->value, "%s/%s", path, compile ? "game.chibip" : "export")
                         : -1;
    free(path);
    if (output_count < 0 || (size_t)output_count >= sizeof r->value) {
        failed(r, "limit", "Job output path exceeds limit");
        return;
    }
    const char *compiler[] = { tool,
                               "--project-root",
                               p->root,
                               "--output",
                               r->value,
                               r->input[0] ? "--scene" : NULL,
                               r->input[0] ? scene : NULL,
                               NULL };
    const char *exporter[] = { tool,       "export", p->root,
                               "--output", r->value, NULL };
    if (!chibi_process_start_capture(&r->process, compile ? compiler : exporter, p->root)) {
        failed(r, "spawn", "Could not start the configured authoring executable");
        return;
    }
    r->text = xcalloc(AUTHORING_PLATFORM_LOG_MAX + 1, 1);
}
static void poll_import(platform_request *r)
{
    if (!r->import_stream || r->state != AUTHORING_PLATFORM_RUNNING) {
        return;
    }
    unsigned char block[65536];
    size_t remaining = (size_t)r->import_info.bytes - r->import_done;
    if (remaining) {
        size_t take = remaining < sizeof block ? remaining : sizeof block;
        int64_t got = rooted_stream_read(r->input_stream, block, take);
        if (got <= 0 ||
            rooted_stream_write(r->import_stream, block, (size_t)got) != got) {
            failed(r, "io", "Import source or staging write failed");
            return;
        }
        r->import_done += (size_t)got;
        r->progress = (double)r->import_done / (double)r->import_info.bytes;
        return;
    }
    rooted_file_info current;
    unsigned char extra;
    bool ok = rooted_stream_info(r->input_stream, &current) &&
              rooted_file_info_equal(&current, &r->import_info) &&
              rooted_stream_read(r->input_stream, &extra, 1) == 0 &&
              rooted_stream_flush(r->import_stream);
    bool closed = rooted_stream_finish(r->import_stream);
    r->import_stream = NULL;
    ok = ok && closed;
    if (ok) {
        ok = rooted_files_move(r->import_files, r->import_temporary, r->import_destination, false);
    }
    if (!ok) {
        failed(r, errno == EEXIST ? "exists" : "io", "Import destination exists or source/publication failed");
        return;
    }
    r->import_temporary[0] = 0;
    authoring_platform_complete(r, AUTHORING_PLATFORM_SUCCEEDED, "ok", "Selected bytes copied exclusively; no decoding or conversion performed", NULL);
}
void authoring_platform_poll_job(platform_request *r)
{
    poll_import(r);
    if (!r->process.process && !r->process.finished) {
        return;
    }
    bool finished = r->process.finished || chibi_process_poll(&r->process);
    bool eof = !r->process.capturing;
    for (int turn = 0; r->process.capturing && turn < 8; turn++) {
        char bytes[4096];
        int64_t count = chibi_process_read(&r->process, bytes, sizeof bytes);
        if (count < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                failed(r, "io", "Diagnostic output read failed");
                chibi_process_stop(&r->process);
            }
            break;
        }
        if (!count) {
            eof = true;
            chibi_process_close_output(&r->process);
            break;
        }
        size_t room = AUTHORING_PLATFORM_LOG_MAX - r->log_bytes;
        size_t keep = (size_t)count < room ? (size_t)count : room;
        memcpy(r->text + r->log_bytes, bytes, keep);
        r->log_bytes += keep;
        r->text[r->log_bytes] = 0;
        if ((size_t)count > room) {
            failed(r, "log-limit", "Job diagnostics exceeded retained log budget");
            chibi_process_stop(&r->process);
            break;
        }
    }
    if (finished && eof && r->state == AUTHORING_PLATFORM_RUNNING) {
        r->exit_code = r->process.status;
        authoring_platform_complete(
            r,
            r->exit_code ? AUTHORING_PLATFORM_FAILED : AUTHORING_PLATFORM_SUCCEEDED,
            r->exit_code ? "process-error" : "ok",
            r->exit_code ? "Authoring job failed; inspect Log" : "Authoring job completed",
            NULL
        );
    }
}
void authoring_platform_cancel_job(platform_request *r)
{
    if (r->process.process) {
        chibi_process_stop(&r->process);
    }
}
void authoring_platform_free_job(platform_request *r)
{
    if (r->process.process) {
        chibi_process_stop(&r->process);
        /* Windows handles release independently of process exit; closing the
         * kill-on-close job also ends descendants if the request is retired. */
        CloseHandle((HANDLE)r->process.process);
        CloseHandle((HANDLE)r->process.job);
        r->process.process = r->process.job = 0;
    }
    chibi_process_close_output(&r->process);
    if (r->input_stream) {
        rooted_stream_close(r->input_stream);
        r->input_stream = NULL;
    }
    if (r->import_stream) {
        rooted_stream_close(r->import_stream);
        r->import_stream = NULL;
    }
    if (r->import_files) {
        if (r->import_temporary[0]) {
            rooted_files_remove(r->import_files, r->import_temporary);
        }
        rooted_files_close(r->import_files);
        r->import_files = NULL;
    }
    if (r->job_files) {
        rooted_files_close(r->job_files);
        r->job_files = NULL;
    }
}
#endif
