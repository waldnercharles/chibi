#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host/application_io.h"

#include "execution/module.h"
#include "host/application_tools.h"
#include "host/export.h"
#include "host/process.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#include <windows.h>
#else
#include "host/serve.h"

#include <sys/utsname.h>
extern char **environ;
#endif

#define APPLICATION_TEXT_LIMIT (16 * 1024 * 1024)
#define APPLICATION_ARGUMENT_LIMIT 1023
#define APPLICATION_ARGUMENT_BYTES (1024 * 1024)
#define APPLICATION_OUTPUT_LIMIT (64 * 1024)
#define IO_ID(N) (UINT64_C(21000) + (N))

typedef struct
{
    int id, socket, port;
} listener;

struct application_io
{
    char *bin;
    application_arguments_fn arguments;
    void *user;
    /* Native result payloads are copied into the caller's runtime before its
     * next host call, so this arena may be reused at each callback. */
    arena scratch;
    listener *listeners;
    char **temporary;
    int next_listener;
#ifdef _WIN32
    void (*interrupt)(int), (*terminate)(int);
#else
    struct sigaction previous[4];
#endif
};

static application_io *signal_owner;
/* Pure test workers may poll cancellation. Lock-free atomics are also safe in
 * the signal handler. Legacy capture/serve run only on the controlling thread
 * after worker submissions have finished, and keep their signal flag type. */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Application cancellation requires lock-free int atomics");
static _Atomic int stop_signal;
static volatile sig_atomic_t capture_stop;
#ifndef _WIN32
static const int signal_numbers[] = { SIGINT, SIGTERM, SIGHUP, SIGPIPE };
#endif

static void stop_application(int signal)
{
    atomic_store_explicit(&stop_signal, signal, memory_order_relaxed);
    capture_stop = signal;
}

application_io *application_io_new(const char *bin, application_arguments_fn arguments, void *user)
{
    assert(bin && arguments);
    if (signal_owner) {
        abort();
    }
    application_io *io = xcalloc(1, sizeof *io);
    io->bin = xstrdup(bin);
    io->arguments = arguments;
    io->user = user;
    io->next_listener = 1;
    atomic_store_explicit(&stop_signal, 0, memory_order_relaxed);
    capture_stop = 0;
    signal_owner = io;
#ifdef _WIN32
    io->interrupt = signal(SIGINT, stop_application);
    io->terminate = signal(SIGTERM, stop_application);
#else
    struct sigaction action = { .sa_handler = stop_application };
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 4; i++) {
        sigaction(signal_numbers[i], NULL, &io->previous[i]);
        if (signal_numbers[i] != SIGPIPE) {
            sigaction(signal_numbers[i], &action, NULL);
        }
    }
#endif
    return io;
}

void application_io_free(application_io *io)
{
    if (!io) {
        return;
    }
    if (signal_owner != io) {
        abort();
    }
#ifdef _WIN32
    if (io->interrupt != SIG_ERR) {
        signal(SIGINT, io->interrupt);
    }
    if (io->terminate != SIG_ERR) {
        signal(SIGTERM, io->terminate);
    }
#else
    for (int i = 0; i < 4; i++) {
        sigaction(signal_numbers[i], &io->previous[i], NULL);
    }
    for (int i = 0; i < acount(io->listeners); i++) {
        if (io->listeners[i].socket >= 0) {
            close(io->listeners[i].socket);
        }
    }
#endif
    signal_owner = NULL;
    for (int i = 0; i < acount(io->temporary); i++) {
        if (io->temporary[i]) {
            chibi_remove_tree(io->temporary[i]);
            free(io->temporary[i]);
        }
    }
    afree(io->temporary);
    afree(io->listeners);
    arena_free(&io->scratch);
    free(io->bin);
    free(io);
}

bool application_io_stopped(const application_io *io)
{
    if (io != signal_owner) {
        abort();
    }
    return atomic_load_explicit(&stop_signal, memory_order_relaxed) != 0;
}

int application_io_interrupt_status(const application_io *io)
{
    if (io != signal_owner) {
        abort();
    }
    int signal = atomic_load_explicit(&stop_signal, memory_order_relaxed);
    return signal ? 128 + signal : 0;
}

volatile sig_atomic_t *application_io_stop_flag(application_io *io)
{
    if (io != signal_owner) {
        abort();
    }
    return &capture_stop;
}

void application_io_sync_stop(application_io *io)
{
    if (io != signal_owner) {
        abort();
    }
    if (capture_stop) {
        int expected = 0;
        atomic_compare_exchange_strong_explicit(&stop_signal, &expected, capture_stop, memory_order_relaxed, memory_order_relaxed);
    }
}

#define IO()                                                                   \
    application_io *io = service;                                              \
    arena_reset(&io->scratch)

static const char *text(application_io *io, pin_value *out, const char *bytes)
{
    return pin_value_text(&io->scratch, out, bytes, strlen(bytes));
}

static const char *string(application_io *io, pin_value value, const char **out)
{
    return pin_text_cstring(&io->scratch, value, out, NULL);
}

#define STRING(INDEX, NAME)                                                    \
    const char *NAME;                                                          \
    {                                                                          \
        const char *error = string(io, arguments[INDEX], &NAME);               \
        if (error) {                                                           \
            return error;                                                      \
        }                                                                      \
    }

static const char *app_arguments(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *results
)
{
    IO();
    const pin_array *arguments = io->arguments(io->user, context->program);
    if (!arguments) {
        return "Application arguments are unavailable for this program";
    }
    results[0].ptr = (void *)arguments;
    return NULL;
}

static const char *app_bin(void *service, const pin_value *, pin_value *results)
{
    IO();
    return text(io, results, io->bin);
}

static const char *output(FILE *stream, const pin_value *arguments)
{
    pin_string value = pin_text(arguments[0]);
    if (value.length && fwrite(value.bytes, 1, value.length, stream) != value.length) {
        return "Cannot write application output";
    }
    return fflush(stream) ? "Cannot flush application output" : NULL;
}

static const char *app_stdout(void *, const pin_value *arguments, pin_value *)
{
    return output(stdout, arguments);
}

static const char *app_stderr(void *, const pin_value *arguments, pin_value *)
{
    return output(stderr, arguments);
}

static const char *app_environment(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, name);
    const char *value = getenv(name);
    return text(io, results, value ? value : "");
}

static const char *app_platform(void *service, const pin_value *, pin_value *results)
{
    IO();
#ifdef _WIN32
    return text(io, results, "Windows");
#elif defined(__APPLE__)
    return text(io, results, "Darwin");
#else
    return text(io, results, "Linux");
#endif
}

static const char *app_machine(void *service, const pin_value *, pin_value *results)
{
    IO();
#ifdef _WIN32
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    const char *name = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
                         ? "x86_64"
                     : info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                         ? "aarch64"
                         : "unknown";
    return text(io, results, name);
#else
    struct utsname info;
    return text(io, results, uname(&info) ? "unknown" : info.machine);
#endif
}

static const char *app_now(void *, const pin_value *, pin_value *results)
{
#ifdef _WIN32
    FILETIME value;
    GetSystemTimeAsFileTime(&value);
    ULARGE_INTEGER ticks = { .LowPart = value.dwLowDateTime,
                             .HighPart = value.dwHighDateTime };
    results[0].i64 = (int64_t)(ticks.QuadPart - UINT64_C(116444736000000000)) * 100;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_REALTIME, &value)) {
        return "Cannot read the wall clock";
    }
    results[0].i64 = (int64_t)value.tv_sec * INT64_C(1000000000) + value.tv_nsec;
#endif
    return NULL;
}

static const char *app_stopped(void *service, const pin_value *, pin_value *results)
{
    results[0].b = application_io_stopped(service);
    return NULL;
}

static const char *app_interrupt_status(void *service, const pin_value *, pin_value *results)
{
    results[0].i = application_io_interrupt_status(service);
    return NULL;
}

static const char *app_wait(void *, const pin_value *arguments, pin_value *results)
{
    int milliseconds = arguments[0].i;
    if (milliseconds < 0) {
        return "Application wait requires nonnegative milliseconds";
    }
#ifdef _WIN32
    while (!atomic_load_explicit(&stop_signal, memory_order_relaxed) &&
           milliseconds > 0) {
        int step = milliseconds > 50 ? 50 : milliseconds;
        Sleep((DWORD)step);
        milliseconds -= step;
    }
#else
    struct timespec remaining = { .tv_sec = milliseconds / 1000,
                                  .tv_nsec = (long)(milliseconds % 1000) * 1000000 };
    while (!atomic_load_explicit(&stop_signal, memory_order_relaxed) &&
           nanosleep(&remaining, &remaining)) {
        if (errno != EINTR) {
            return "Application wait failed";
        }
    }
#endif
    results[0].b = !atomic_load_explicit(&stop_signal, memory_order_relaxed);
    return NULL;
}

static const char *file_kind(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    struct stat info;
    results[0].i = stat(path, &info)     ? 0
                 : S_ISDIR(info.st_mode) ? 2
                 : S_ISREG(info.st_mode) ? 1
                                         : 0;
    return NULL;
}

static const char *file_resolve(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    char *resolved = realpath(path, NULL);
    const char *error = text(io, results, resolved ? resolved : "");
    free(resolved);
    return error;
}

static const char *file_modified(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    struct stat info;
    results[0].i64 = -1;
    if (!stat(path, &info)) {
#if defined(__APPLE__)
        results[0].i64 = (int64_t)info.st_mtimespec.tv_sec * INT64_C(1000000000) +
                         info.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
        results[0].i64 = (int64_t)info.st_mtime * INT64_C(1000000000);
#else
        results[0].i64 = (int64_t)info.st_mtim.tv_sec * INT64_C(1000000000) +
                         info.st_mtim.tv_nsec;
#endif
    }
    return NULL;
}

static const char *file_read(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    FILE *file = fopen(path, "rb");
    struct stat info;
    if (!file || fstat(fileno(file), &info) || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || info.st_size > APPLICATION_TEXT_LIMIT) {
        if (file) {
            fclose(file);
        }
        return text(io, results, "");
    }
    size_t size = (size_t)info.st_size;
    char *bytes = arena_alloc(&io->scratch, size + 1);
    bool read = fread(bytes, 1, size, file) == size && !ferror(file);
    fclose(file);
    bytes[size] = 0;
    return pin_value_text(&io->scratch, results, bytes, read ? size : 0);
}

static const char *file_write(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    pin_string value = pin_text(arguments[1]);
    FILE *file = fopen(path, "wb");
    bool wrote = file && (!value.length ||
                          fwrite(value.bytes, 1, value.length, file) == value.length);
    if (file) {
        wrote = !fclose(file) && wrote;
    }
    results[0].b = wrote;
    return NULL;
}

static const char *file_mkdir(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
#ifdef _WIN32
    bool made = !_mkdir(path);
#else
    bool made = !mkdir(path, 0777);
#endif
    struct stat info;
    results[0].b = made || (!stat(path, &info) && S_ISDIR(info.st_mode));
    return NULL;
}

static const char *file_touch(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    int64_t stamp = arguments[1].i64;
    if (stamp < 0) {
        results[0].b = false;
        return NULL;
    }
#ifdef _WIN32
    time_t second = (time_t)(stamp / INT64_C(1000000000));
    struct _utimbuf times = { .actime = second, .modtime = second };
    results[0].b = !_utime(path, &times);
#else
    struct timespec times[2] = { { .tv_sec = (time_t)(stamp / INT64_C(1000000000)),
                                   .tv_nsec = (long)(stamp % INT64_C(1000000000)) } };
    times[1] = times[0];
    results[0].b = !utimensat(AT_FDCWD, path, times, 0);
#endif
    return NULL;
}

static const char *file_remove(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    results[0].b = chibi_remove_tree(path);
    if (results[0].b) {
        for (int i = 0; i < acount(io->temporary); i++) {
            if (io->temporary[i] && !strcmp(io->temporary[i], path)) {
                free(io->temporary[i]);
                io->temporary[i] = NULL;
            }
        }
    }
    return NULL;
}

static void own_temporary(application_io *io, const char *path)
{
    for (int i = 0; i < acount(io->temporary); i++) {
        if (!io->temporary[i]) {
            io->temporary[i] = xstrdup(path);
            return;
        }
    }
    apush(io->temporary, xstrdup(path));
}

static const char *file_temporary(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, prefix);
    if (!*prefix || strpbrk(prefix, "/\\")) {
        return "Temporary directory prefix must be a nonempty name";
    }
#ifdef _WIN32
    char base[MAX_PATH], path[MAX_PATH];
    DWORD length = GetTempPathA(sizeof base, base);
    if (!length || length >= sizeof base || !GetTempFileNameA(base, prefix, 0, path)) {
        return text(io, results, "");
    }
    DeleteFileA(path);
    bool made = CreateDirectoryA(path, NULL) != 0;
    if (made) {
        own_temporary(io, path);
    }
    return text(io, results, made ? path : "");
#else
    const char *base = getenv("TMPDIR");
    base = base && *base ? base : "/tmp";
    size_t size = strlen(base) + strlen(prefix) + sizeof "/XXXXXX";
    char *path = arena_alloc(&io->scratch, size);
    snprintf(path, size, "%s/%sXXXXXX", base, prefix);
    bool made = mkdtemp(path) != NULL;
    if (made) {
        own_temporary(io, path);
    }
    return text(io, results, made ? path : "");
#endif
}

static bool runnable(const char *path)
{
    struct stat info;
#ifdef _WIN32
    const int mode = F_OK;
#else
    const int mode = X_OK;
#endif
    return !stat(path, &info) && S_ISREG(info.st_mode) && !access(path, mode);
}

static const char *find_executable(application_io *io, const char *name)
{
    if (!*name) {
        return "";
    }
    if (strpbrk(name, "/\\")) {
        char *resolved = runnable(name) ? realpath(name, NULL) : NULL;
        size_t size = resolved ? strlen(resolved) + 1 : 1;
        char *copy = arena_alloc(&io->scratch, size);
        memcpy(copy, resolved ? resolved : "", size);
        free(resolved);
        return copy;
    }
    const char *path = getenv("PATH");
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    for (const char *at = path; at && *at;) {
        const char *end = strchr(at, separator);
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (length) {
            size_t size = length + strlen(name) + 2;
            char *candidate = arena_alloc(&io->scratch, size);
            snprintf(candidate, size, "%.*s/%s", (int)length, at, name);
            if (runnable(candidate)) {
                char *resolved = realpath(candidate, NULL);
                const char *found = resolved ? resolved : candidate;
                char *copy = arena_alloc(&io->scratch, strlen(found) + 1);
                strcpy(copy, found);
                free(resolved);
                return copy;
            }
        }
        at = end ? end + 1 : NULL;
    }
    return "";
}

static const char *file_find_executable(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, name);
    return text(io, results, find_executable(io, name));
}

static const char *file_changed_since(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(0, path);
    results[0].b = application_tools_changed_since(path, arguments[1].i64);
    return NULL;
}

static const char *process_arguments(application_io *io, pin_value value, char ***out)
{
    const pin_array *array = value.ptr;
    if (!array || array->count < 1 || array->count > APPLICATION_ARGUMENT_LIMIT) {
        return "A process needs between 1 and 1023 arguments";
    }
    char **strings = arena_alloc(&io->scratch, ((size_t)array->count + 1) * sizeof *strings);
    size_t bytes = 0;
    for (int i = 0; i < array->count; i++) {
        pin_value item = pin_array_get(array, i);
        const char *error = string(io, item, (const char **)&strings[i]);
        if (error) {
            return error;
        }
        size_t size = strlen(strings[i]);
        if (size > APPLICATION_ARGUMENT_BYTES - bytes) {
            return "Process argument bytes exceed 1 MiB";
        }
        bytes += size;
    }
    if (!strings[0][0]) {
        return "A process needs a nonempty executable name";
    }
    strings[array->count] = NULL;
    *out = strings;
    return NULL;
}

static const char *process_run(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    char **strings;
    const char *error = process_arguments(io, arguments[0], &strings);
    if (error) {
        return error;
    }
    results[0].i = chibi_process_run(strings, application_io_stop_flag(io));
    application_io_sync_stop(io);
    return NULL;
}

static const char *process_capture(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    char **strings;
    const char *error = process_arguments(io, arguments[0], &strings);
    if (error) {
        return error;
    }
#ifdef _WIN32
    results[0].i = 1;
    results[1].i = 1;
    error = text(io, &results[2], "");
    return error
             ? error
             : text(io, &results[3], "Process output capture is unsupported on Windows");
#else
    char *out = arena_alloc(&io->scratch, APPLICATION_OUTPUT_LIMIT);
    char *err = arena_alloc(&io->scratch, APPLICATION_OUTPUT_LIMIT);
    chibi_process_output output = { .data = out, .capacity = APPLICATION_OUTPUT_LIMIT };
    chibi_process_output errors = { .data = err, .capacity = APPLICATION_OUTPUT_LIMIT };
    const char *executable = find_executable(io, strings[0]);
    chibi_process_capture_result captured = *executable
                                              ? chibi_process_capture(
                                                    executable,
                                                    (const char *const *)strings,
                                                    "/",
                                                    (const char *const *)environ,
                                                    chibi_process_now() + 10,
                                                    &capture_stop,
                                                    &output,
                                                    &errors
                                                )
                                              : (
                                                    chibi_process_capture_result
                                                ){ .error = CHIBI_PROCESS_SPAWN_FAILED,
                                                   .status = 1,
                                                   .system_error = ENOENT };
    results[0].i = captured.error;
    results[1].i = captured.status;
    error = pin_value_text(&io->scratch, &results[2], out, output.size);
    return error ? error : pin_value_text(&io->scratch, &results[3], err, errors.size);
#endif
}

static listener *find_listener(application_io *io, int id)
{
    for (int i = 0; i < acount(io->listeners); i++) {
        if (io->listeners[i].id == id && io->listeners[i].socket >= 0) {
            return &io->listeners[i];
        }
    }
    return NULL;
}

static const char *network_listen(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    int port = arguments[0].i;
    results[0].i = -1;
    if (port < 0 || port > 65535) {
        return "A listener port must be between 0 and 65535";
    }
#ifndef _WIN32
    int bound = 0, socket = chibi_serve_listen(port, &bound);
    if (socket < 0) {
        fprintf(stderr, "Cannot listen on 127.0.0.1:%d: %s\n", port, strerror(errno));
        return NULL;
    }
    if (io->next_listener == INT_MAX) {
        close(socket);
        return "Application listener handles exhausted";
    }
    int id = io->next_listener++;
    for (int i = 0; i < acount(io->listeners); i++) {
        if (io->listeners[i].socket < 0) {
            io->listeners[i] = (listener){ id, socket, bound };
            results[0].i = id;
            return NULL;
        }
    }
    apush(io->listeners, ((listener){ id, socket, bound }));
    results[0].i = id;
#endif
    return NULL;
}

static const char *network_port(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    listener *server = find_listener(io, arguments[0].i);
    if (!server) {
        return "Unknown application listener";
    }
    results[0].i = server->port;
    return NULL;
}

static const char *network_serve(void *service, const pin_value *arguments, pin_value *results)
{
    IO();
    STRING(1, root);
    listener *server = find_listener(io, arguments[0].i);
    if (!server) {
        return "Unknown application listener";
    }
#ifdef _WIN32
    (void)root;
    results[0].i = 1;
#else
    int socket = dup(server->socket);
    if (socket < 0) {
        results[0].i = 1;
        return NULL;
    }
    fcntl(socket, F_SETFD, FD_CLOEXEC);
    chibi_serve(socket, root, &capture_stop);
    results[0].i = 0;
#endif
    return NULL;
}

static const char *network_close(void *service, const pin_value *arguments, pin_value *)
{
    IO();
    listener *server = find_listener(io, arguments[0].i);
    if (!server) {
        return "Unknown application listener";
    }
#ifndef _WIN32
    close(server->socket);
#endif
    server->socket = -1;
    return NULL;
}

#define PARAM(NAME, TYPE)                                                      \
    {                                                                          \
        .name = NAME, .type = {.storage = TYPE }                               \
    }
static const script_catalog_parameter text_input[] = { PARAM("text", PIN_STR) };
static const script_catalog_parameter integer_input[] = { PARAM("value", PIN_I32) };
static const script_catalog_parameter path_time_input[] = { PARAM("path", PIN_STR),
                                                            PARAM("stamp", PIN_I64) };
static const script_catalog_parameter path_text_input[] = { PARAM("path", PIN_STR),
                                                            PARAM("text", PIN_STR) };
static const script_catalog_parameter server_path_input[] = { PARAM("server", PIN_I32),
                                                              PARAM("path", PIN_STR) };
static const script_catalog_parameter argv_input[] = {
    { .name = "arguments", .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } }
};
static const script_catalog_parameter text_result[] = { PARAM("value", PIN_STR) };
static const script_catalog_parameter integer_result[] = { PARAM("value", PIN_I32) };
static const script_catalog_parameter time_result[] = { PARAM("value", PIN_I64) };
static const script_catalog_parameter bool_result[] = { PARAM("value", PIN_BOOL) };
static const script_catalog_parameter argv_result[] = {
    { .name = "value", .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY } }
};
static const script_catalog_parameter capture_result[] = {
    PARAM("error", PIN_I32),
    PARAM("status", PIN_I32),
    PARAM("output", PIN_STR),
    PARAM("error_output", PIN_STR)
};
#undef PARAM

#define IO_FUNCTIONS(X)                                                                          \
    X(1, "application.arguments", NULL, 0, argv_result, 1, context, app_arguments)               \
    X(2, "application.bin", NULL, 0, text_result, 1, invoke, app_bin)                            \
    X(3, "application.stdout", text_input, 1, NULL, 0, invoke, app_stdout)                       \
    X(4, "application.stderr", text_input, 1, NULL, 0, invoke, app_stderr)                       \
    X(5, "application.environment", text_input, 1, text_result, 1, invoke, app_environment)      \
    X(6, "application.platform", NULL, 0, text_result, 1, invoke, app_platform)                  \
    X(7, "application.machine", NULL, 0, text_result, 1, invoke, app_machine)                    \
    X(8, "application.now", NULL, 0, time_result, 1, invoke, app_now)                            \
    X(9, "application.stopped", NULL, 0, bool_result, 1, invoke, app_stopped)                    \
    X(10, "application.wait", integer_input, 1, bool_result, 1, invoke, app_wait)                \
    X(11, "files.kind", text_input, 1, integer_result, 1, invoke, file_kind)                     \
    X(12, "files.resolve", text_input, 1, text_result, 1, invoke, file_resolve)                  \
    X(13, "files.modified", text_input, 1, time_result, 1, invoke, file_modified)                \
    X(14, "files.read", text_input, 1, text_result, 1, invoke, file_read)                        \
    X(15, "files.write", path_text_input, 2, bool_result, 1, invoke, file_write)                 \
    X(16, "files.mkdir", text_input, 1, bool_result, 1, invoke, file_mkdir)                      \
    X(17, "files.touch", path_time_input, 2, bool_result, 1, invoke, file_touch)                 \
    X(18, "files.remove", text_input, 1, bool_result, 1, invoke, file_remove)                    \
    X(19, "files.temporary", text_input, 1, text_result, 1, invoke, file_temporary)              \
    X(20, "files.find_executable", text_input, 1, text_result, 1, invoke, file_find_executable)  \
    X(21, "files.changed_since", path_time_input, 2, bool_result, 1, invoke, file_changed_since) \
    X(22, "process.run", argv_input, 1, integer_result, 1, invoke, process_run)                  \
    X(23, "process.capture", argv_input, 1, capture_result, 4, invoke, process_capture)          \
    X(24, "network.listen", integer_input, 1, integer_result, 1, invoke, network_listen)         \
    X(25, "network.port", integer_input, 1, integer_result, 1, invoke, network_port)             \
    X(26, "network.serve", server_path_input, 2, integer_result, 1, invoke, network_serve)       \
    X(27, "network.close", integer_input, 1, NULL, 0, invoke, network_close)                     \
    X(28, "application.interrupt_status", NULL, 0, integer_result, 1, invoke, app_interrupt_status)

#define CAPABILITY(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = IO_ID(ID),                                                                 \
      .name = NAME,                                                                    \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                              \
      .parameters = PARAMETERS,                                                        \
      .parameter_count = COUNT,                                                        \
      .results = RESULTS,                                                              \
      .result_count = RESULT_COUNT,                                                    \
      .effects = SCRIPT_EFFECT_HOST },
static const script_catalog_capability capabilities[] = { IO_FUNCTIONS(CAPABILITY) };
#undef CAPABILITY
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = sizeof capabilities /
                                                            sizeof *capabilities };

#define CALL(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    static const node_host_call call_##ID = {                                    \
        .service = APPLICATION_IO_SERVICE,                                       \
        .KIND = FUNCTION,                                                        \
        .missing_service = "Application IO service is unavailable"               \
    };
IO_FUNCTIONS(CALL)
#undef CALL
static const node_access host_sink = { .flags = NODE_EFFECT_HOST_SINK };
#define BINDING(ID, NAME, PARAMETERS, COUNT, RESULTS, RESULT_COUNT, KIND, FUNCTION) \
    { .id = IO_ID(ID), .call = &call_##ID, .access = &host_sink },
static const native_capability_binding bindings[] = { IO_FUNCTIONS(BINDING) };
#undef BINDING
#undef IO_FUNCTIONS

const native_capability_pack *application_io_natives()
{
    static const native_capability_pack pack = { .catalog = &catalog,
                                                 .bindings = bindings,
                                                 .binding_count = sizeof bindings /
                                                                  sizeof *bindings };
    return &pack;
}
