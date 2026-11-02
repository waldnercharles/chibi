#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include "authoring/asset_compiler_native.h"

#ifndef __EMSCRIPTEN__
#include "host/process.h"
#include "support/file_io.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
extern char **environ;
#endif

#define ASSET_COMPILER_TIMEOUT_SECONDS 600
#define ASSET_COMPILER_DEFAULT_BUDGET UINT64_C(4000000000)

static void failure(asset_compiler_diagnostic *diagnostic, const char *message)
{
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
}

/* Explicit paths win. The packaged service sits beside its consumer in a
 * build, or in the SDK's libexec directory after relocation. */
static char *compiler_program(const asset_compiler_options *options)
{
    const char *explicit = options && options->program
                             ? options->program
                             : getenv("CHIBI_ASSET_COMPILER");
    if (explicit) {
#ifdef _WIN32
        char resolved[4096];
        DWORD size = *explicit
                       ? GetFullPathNameA(explicit, sizeof resolved, resolved, NULL)
                       : 0;
        return size > 0 && size < sizeof resolved ? xstrdup(resolved) : NULL;
#else
        return *explicit ? realpath(explicit, NULL) : NULL;
#endif
    }
    char *directory = file_executable_dir();
    char candidate[4096];
    char *found = NULL;
    for (int index = 0; index < 3; index++) {
        const char *relative[] = { "/chibi-asset-compiler",
                                   "/sdk/chibi-asset-compiler",
                                   "/../libexec/chibi/chibi-asset-compiler" };
        int size = snprintf(candidate, sizeof candidate, "%s%s%s", directory, relative[index], CHIBI_EXECUTABLE_SUFFIX);
        if (size <= 0 || (size_t)size >= sizeof candidate) {
            continue;
        }
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(candidate);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            found = xstrdup(candidate);
            break;
        }
#else
        if (access(candidate, X_OK) == 0) {
            found = realpath(candidate, NULL);
            if (found) {
                break;
            }
        }
#endif
    }
    free(directory);
    return found;
}

/* A process invocation owns exactly these files and this private directory. */
static bool temporary_directory(char directory[4096])
{
#ifdef _WIN32
    char base[4096];
    DWORD size = GetTempPathA(sizeof base, base);
    if (!size || size >= sizeof base) {
        return false;
    }
    for (unsigned attempt = 0; attempt < 128; attempt++) {
        int length = snprintf(
            directory,
            4096,
            "%schibi-asset-%lu-%" PRIu64 "-%u",
            base,
            GetCurrentProcessId(),
            (uint64_t)GetTickCount64(),
            attempt
        );
        if (length <= 0 || length >= 4096) {
            return false;
        }
        if (CreateDirectoryA(directory, NULL)) {
            return true;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return false;
#else
    const char *base = getenv("TMPDIR");
    int size = snprintf(directory, 4096, "%s/chibi-asset.XXXXXX", base && *base ? base : "/tmp");
    if (size <= 0 || size >= 4096 || !mkdtemp(directory)) {
        return false;
    }
    char *absolute = realpath(directory, NULL);
    if (!absolute || strlen(absolute) >= 4096) {
        free(absolute);
        rmdir(directory);
        return false;
    }
    memcpy(directory, absolute, strlen(absolute) + 1);
    free(absolute);
    return true;
#endif
}
static bool path(char output[4096], const char *directory, const char *name)
{
    int size = snprintf(output, 4096, "%s/%s", directory, name);
    return size > 0 && size < 4096;
}
static bool write_request(const char *path, const char *request, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool written = fwrite(request, 1, length, file) == length;
    return fclose(file) == 0 && written;
}
static unsigned char *read_output(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    bool positioned = fseek(file, 0, SEEK_END) == 0;
    long size = positioned ? ftell(file) : -1;
    if (size <= 0 || (uint64_t)size > ASSET_COMPILER_MAX_OUTPUT_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *bytes = xmalloc((size_t)size);
    bool read = fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0 || !read) {
        free(bytes);
        return NULL;
    }
    *length = (size_t)size;
    return bytes;
}
static void rejection(const char *path, asset_compiler_diagnostic *diagnostic)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        failure(diagnostic, "The native package compiler rejected its input without a diagnostic");
        return;
    }
    int *fields[] = { &diagnostic->document, &diagnostic->line, &diagnostic->column };
    bool valid = true;
    for (size_t index = 0; index < sizeof fields / sizeof *fields; index++) {
        char line[32];
        char *end = NULL;
        if (!fgets(line, sizeof line, file)) {
            valid = false;
            break;
        }
        errno = 0;
        long value = strtol(line, &end, 10);
        if (errno || value < INT_MIN || value > INT_MAX || end == line ||
            strcmp(end, "\n")) {
            valid = false;
            break;
        }
        *fields[index] = (int)value;
    }
    if (valid) {
        size_t size = fread(diagnostic->message, 1, sizeof diagnostic->message - 1, file);
        diagnostic->message[size] = '\0';
        valid = size != 0 && !ferror(file);
    }
    fclose(file);
    if (!valid) {
        *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
        failure(diagnostic, "The native package compiler returned an invalid diagnostic");
    }
}

#ifndef _WIN32
static int compile(
    const char *program,
    const char *const *arguments,
    const char *directory,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    sig_atomic_t cancelled = options && options->cancelled &&
                             options->cancelled(options->user);
    char log[512];
    chibi_process_output errors = { .data = log, .capacity = sizeof log };
    chibi_process_output discarded = { 0 };
    chibi_process_job job = { 0 };
    double deadline = chibi_process_now() + ASSET_COMPILER_TIMEOUT_SECONDS;
    chibi_process_capture_result result = chibi_process_begin(
        &job,
        program,
        arguments,
        directory,
        (const char *const *)environ,
        deadline,
        &cancelled,
        &discarded,
        &errors
    );
    if (result.error == CHIBI_PROCESS_OK) {
        while (chibi_process_running(&job)) {
            if (options && options->cancelled && options->cancelled(options->user)) {
                cancelled = 1;
            }
            chibi_process_service(&job, 1, deadline, &cancelled);
        }
        result = chibi_process_end(&job);
    }
    switch (result.error) {
        case CHIBI_PROCESS_OK:
            if (result.status >= 0) {
                return result.status;
            }
            failure(diagnostic, "The native package compiler was terminated by a signal");
            break;
        case CHIBI_PROCESS_CANCELLED:
            failure(diagnostic, "The package compile was cancelled");
            break;
        case CHIBI_PROCESS_TIMEOUT:
            failure(diagnostic, "The native package compile timed out");
            break;
        default:
            failure(diagnostic, "The native package compiler could not run");
            break;
    }
    return -1;
}
#else
static int compile(
    const char *program,
    const char *const *arguments,
    const char *directory,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    (void)directory;
    size_t size = 1;
    for (size_t index = 0; arguments[index]; index++) {
        size += strlen(arguments[index]) + 3;
    }
    char *line = xmalloc(size);
    char *at = line;
    for (size_t index = 0; arguments[index]; index++) {
        if (index) {
            *at++ = ' ';
        }
        *at++ = '"';
        size_t count = strlen(arguments[index]);
        memcpy(at, arguments[index], count);
        at += count;
        *at++ = '"';
    }
    *at = '\0';
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { 0 };
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job ||
        !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        if (job) {
            CloseHandle(job);
        }
        free(line);
        failure(diagnostic, "The native package compiler job could not be created");
        return -1;
    }
    STARTUPINFOA startup = { .cb = sizeof startup };
    PROCESS_INFORMATION process;
    bool started = CreateProcessA(
        program,
        line,
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL,
        NULL,
        &startup,
        &process
    );
    free(line);
    if (!started) {
        CloseHandle(job);
        failure(diagnostic, "The native package compiler could not run");
        return -1;
    }
    bool owned = AssignProcessToJobObject(job, process.hProcess) &&
                 ResumeThread(process.hThread) != (DWORD)-1;
    if (!owned) {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hThread);
    ULONGLONG started_at = GetTickCount64();
    bool cancelled = false;
    bool timed_out = false;
    bool failed = !owned;
    while (owned) {
        DWORD waited = WaitForSingleObject(process.hProcess, 10);
        if (waited == WAIT_OBJECT_0) {
            break;
        }
        cancelled = options && options->cancelled && options->cancelled(options->user);
        timed_out = GetTickCount64() - started_at >=
                    ASSET_COMPILER_TIMEOUT_SECONDS * UINT64_C(1000);
        failed = waited != WAIT_TIMEOUT;
        if (cancelled || timed_out || failed) {
            TerminateJobObject(job, 1);
            break;
        }
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 1;
    failed = !GetExitCodeProcess(process.hProcess, &status) || failed;
    CloseHandle(process.hProcess);
    CloseHandle(job);
    if (cancelled) {
        failure(diagnostic, "The package compile was cancelled");
    } else if (timed_out) {
        failure(diagnostic, "The native package compile timed out");
    } else if (failed) {
        failure(diagnostic, "The native package compiler could not run");
    } else {
        if (status <= INT_MAX) {
            return (int)status;
        }
        failure(diagnostic, "The native package compiler was terminated abnormally");
    }
    return -1;
}
#endif

unsigned char *asset_compiler_compile_closure(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    *length = 0;
    if (!request) {
        failure(diagnostic, "The package compiler request is missing");
        return NULL;
    }
    if (request_length > ASSET_COMPILER_MAX_REQUEST_BYTES) {
        failure(diagnostic, "The package compiler request exceeds its byte limit");
        return NULL;
    }
    if (options && options->cancelled && options->cancelled(options->user)) {
        failure(diagnostic, "The package compile was cancelled");
        return NULL;
    }
    char *program = compiler_program(options);
    if (!program) {
        failure(diagnostic, "The native Chibi package compiler is unavailable");
        return NULL;
    }
    char directory[4096];
    char input[4096], output[4096], rejected[4096], partial[4096];
    unsigned char *bytes = NULL;
    if (!temporary_directory(directory)) {
        failure(diagnostic, "The native package compiler temporary directory could not be created");
        free(program);
        return NULL;
    }
    bool paths = path(input, directory, "request.json") &&
                 path(output, directory, "closure.chibip") &&
                 path(rejected, directory, "diagnostic.txt") &&
                 path(partial, directory, "closure.chibip.partial");
    if (!paths || !write_request(input, request, request_length)) {
        failure(diagnostic, "The native package compiler request could not be written");
    } else {
        char budget[32];
        snprintf(
            budget,
            sizeof budget,
            "%" PRIu64,
            options && options->compile_budget ? options->compile_budget : ASSET_COMPILER_DEFAULT_BUDGET
        );
        const char *arguments[] = { program,  input,  output,
                                    rejected, budget, NULL };
        int status = compile(program, arguments, directory, options, diagnostic);
        if (status == 0) {
            bytes = read_output(output, length);
            if (!bytes) {
                failure(diagnostic, "The native package compiler returned no valid closure");
            }
        } else if (status == 1) {
            rejection(rejected, diagnostic);
        } else if (status == 125) {
            failure(diagnostic, "The package compile exceeded its step budget");
        } else if (status >= 0 && !diagnostic->message[0]) {
            failure(diagnostic, "The native package compiler failed");
        }
    }
    if (paths) {
        remove(input);
        remove(output);
        remove(rejected);
        remove(partial);
    }
#ifdef _WIN32
    RemoveDirectoryA(directory);
#else
    rmdir(directory);
#endif
    free(program);
    return bytes;
}

bool asset_compiler_native_digest(const asset_compiler_options *options, char digest[65])
{
    char *program = compiler_program(options);
    if (!program) {
        return false;
    }
    uint64_t size = 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(program, GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        free(program);
        return false;
    }
    size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
#else
    struct stat info;
    if (stat(program, &info) || !S_ISREG(info.st_mode)) {
        free(program);
        return false;
    }
    size = (uint64_t)info.st_size;
#endif
    /* Beyond 256 MiB compilation still works, but cache identity is not worth
     * reading an arbitrary executable of that size before a small compile. */
    if (!size || size > UINT64_C(268435456)) {
        free(program);
        return false;
    }
    FILE *file = fopen(program, "rb");
    free(program);
    if (!file) {
        return false;
    }
    /* File timestamps can survive same-size rewrites on supported filesystems;
     * only the bytes identify the compiler used by a persistent cache entry. */
    sha256 hash;
    sha256_begin(&hash);
    unsigned char block[65536];
    uint64_t total = 0;
    while (total < size) {
        if (options && options->cancelled && options->cancelled(options->user)) {
            break;
        }
        size_t count = fread(block, 1, sizeof block, file);
        if (!count) {
            break;
        }
        total += count;
        sha256_add(&hash, block, count);
    }
    bool read = !ferror(file) && total == size;
    int closed = fclose(file);
    if (!read || closed) {
        return false;
    }
    sha256_end(&hash, digest);
    return true;
}
#endif
