#include "host/build_host.h"
#include "host/build_timing.h"
#include "host/process.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

/* No game/native type packs are admitted by this private tool host. */
int type_catalog_pack_count()
{
    return 0;
}
type_catalog_pack type_catalog_pack_at(int)
{
    assert(!"build host has no native type packs");
    return (type_catalog_pack){ 0 };
}

static volatile sig_atomic_t cancelled;
static volatile sig_atomic_t preparing = 1;
static _Atomic double preparation_deadline;

static void cancel(int signal)
{
    int saved_errno = errno;
    if (!preparing) {
        if (signal != SIGALRM) {
            cancelled = signal;
        }
        errno = saved_errno;
        return;
    }
    if (signal == SIGALRM) {
        double now = chibi_process_now();
        if (now >= 0 &&
            now < atomic_load_explicit(&preparation_deadline, memory_order_relaxed)) {
            errno = saved_errno;
            return;
        }
        if (now < 0) {
            write(STDERR_FILENO, "build process host failure\n", sizeof "build process host failure\n" - 1);
            _exit(1);
        }
        write(STDERR_FILENO, "build process timeout\n", sizeof "build process timeout\n" - 1);
        _exit(1);
    }
    write(STDERR_FILENO, "build process cancelled\n", sizeof "build process cancelled\n" - 1);
    _exit(1);
}

/* The interval only matters if the wall timer fires before the monotonic
 * deadline, for example across macOS sleep. No timer APIs run in a handler. */
static bool preparation_start(double deadline)
{
    if (!atomic_is_lock_free(&preparation_deadline)) {
        return false;
    }
    atomic_store_explicit(&preparation_deadline, deadline, memory_order_relaxed);
    struct sigaction action = { .sa_handler = cancel };
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    sigaddset(&action.sa_mask, SIGALRM);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL) ||
        sigaction(SIGALRM, &action, NULL) ||
        sigprocmask(SIG_UNBLOCK, &action.sa_mask, NULL)) {
        return false;
    }
    double now = chibi_process_now();
    if (now < 0) {
        return false;
    }
    double remaining = deadline > now ? deadline - now : 0;
    time_t seconds = (time_t)remaining;
    double fraction = (remaining - seconds) * 1000000;
    suseconds_t micros = (suseconds_t)fraction;
    if (fraction > micros) {
        micros++;
    }
    if (micros == 1000000) {
        seconds++;
        micros = 0;
    }
    if (!seconds && !micros) {
        micros = 1;
    }
    struct itimerval timer = { .it_value = { seconds, micros },
                               .it_interval = { 0, 1000 } };
    return setitimer(ITIMER_REAL, &timer, NULL) == 0;
}

/* No authored function or build child has run when this callback is called. */
static void preparation_end()
{
    struct itimerval timer = { 0 };
    if (setitimer(ITIMER_REAL, &timer, NULL)) {
        fprintf(stderr, "build process host failure\n");
        exit(1);
    }
    preparing = 0;
}
static int usage()
{
    fprintf(stderr, "usage: chibi-build-host --root DIR --source FILE [--core DIR] [--collection NAME=DIR]... [--compiler DIR] [--compiler-core DIR] --entry NAME --timeout-ms N [-- ARGUMENTS...]\n");
    return 1;
}
#define BUILD_SOURCE_LIMIT (1024 * 1024)
static char *read_source(const char *path, double deadline)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_size > BUILD_SOURCE_LIMIT) {
        close(fd);
        return NULL;
    }
    char *text = xmalloc(BUILD_SOURCE_LIMIT + 1);
    size_t size = 0;
    for (;;) {
        if (cancelled || chibi_build_now() >= deadline || size > BUILD_SOURCE_LIMIT) {
            free(text);
            close(fd);
            return NULL;
        }
        ssize_t count = read(fd, text + size, BUILD_SOURCE_LIMIT + 1 - size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 || (count > 0 && memchr(text + size, 0, count))) {
            free(text);
            close(fd);
            return NULL;
        }
        if (!count) {
            text[size] = 0;
            close(fd);
            return text;
        }
        size += (size_t)count;
    }
}
/* Where recovered compilers are kept: the directory CHIBI_COMPILER_CACHE
 * names, none when it is "off", else .chibi/compiler-cache below the root. */
static char *compiler_cache(const char *root)
{
    const char *named = getenv("CHIBI_COMPILER_CACHE");
    if (named && !strcmp(named, "off")) {
        return NULL;
    }
    if (named && *named) {
        return xstrdup(named);
    }
    static const char SUFFIX[] = "/.chibi/compiler-cache";
    char *base = realpath(root, NULL);
    if (!base) {
        return NULL;
    }
    size_t size = strlen(base) + sizeof SUFFIX;
    char *directory = xmalloc(size);
    snprintf(directory, size, "%s%s", base, SUFFIX);
    free(base);
    return directory;
}
/* path, from root unless it is absolute. */
static char *from_root(const char *root, const char *path)
{
    size_t size = strlen(root) + strlen(path) + 2;
    char *out = xmalloc(size);
    if (path[0] == '/') {
        snprintf(out, size, "%s", path);
    } else {
        snprintf(out, size, "%s/%s", root, path);
    }
    return out;
}
/* Build-tree and installed hosts carry the same compiler inputs beside their
 * SDK. Resolve from the executable so invocation never depends on the cwd. */
static char *shared(const char *name)
{
    char *bin = file_executable_dir();
    const char *parents[] = { "..", "../..", "." };
    char *folder = NULL;
    for (size_t i = 0; i < sizeof parents / sizeof *parents && !folder; i++) {
        size_t size = strlen(bin) + strlen(parents[i]) + strlen(name) +
                      sizeof "/share/chibi//";
        char *candidate = xmalloc(size);
        snprintf(candidate, size, "%s/%s/share/chibi/%s", bin, parents[i], name);
        folder = realpath(candidate, NULL);
        free(candidate);
    }
    free(bin);
    return folder;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--protocol-version")) {
        printf("%d\n", CHIBI_BUILD_HOST_PROTOCOL);
        return 0;
    }
    const char *root = NULL, *path = NULL, *core = NULL, *entry = NULL;
    const char *compiler_sources = NULL, *compiler_core = NULL;
    long timeout = 0;
    int arguments_start = argc;
    const char *collections[CHIBI_BUILD_MAX_COLLECTIONS];
    int collection_count = 0;
    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "--")) {
            arguments_start = i + 1;
            break;
        }
        if (i + 1 == argc) {
            return usage();
        }
        if (!strcmp(argv[i], "--root") && !root) {
            root = argv[i + 1];
        } else if (!strcmp(argv[i], "--source") && !path) {
            path = argv[i + 1];
        } else if (!strcmp(argv[i], "--core") && !core) {
            core = argv[i + 1];
        } else if (!strcmp(argv[i], "--compiler") && !compiler_sources) {
            compiler_sources = argv[i + 1];
        } else if (!strcmp(argv[i], "--compiler-core") && !compiler_core) {
            compiler_core = argv[i + 1];
        } else if (!strcmp(argv[i], "--collection") && collection_count < CHIBI_BUILD_MAX_COLLECTIONS) {
            const char *equals = strchr(argv[i + 1], '=');
            if (!equals || equals == argv[i + 1] || !equals[1]) {
                return usage();
            }
            collections[collection_count++] = argv[i + 1];
        } else if (!strcmp(argv[i], "--entry") && !entry) {
            entry = argv[i + 1];
        } else if (!strcmp(argv[i], "--timeout-ms") && !timeout) {
            char *end;
            errno = 0;
            timeout = strtol(argv[i + 1], &end, 10);
            if (errno || *end || timeout <= 0 || timeout > INT_MAX) {
                return usage();
            }
        } else {
            return usage();
        }
    }
    if (!root || !path || !entry || !timeout) {
        return usage();
    }
    /* Budget starts before file IO and preparation, and every child shares it. */
    double started = chibi_build_now();
    double deadline = started + timeout / 1000.0;
    if (!isfinite(deadline)) {
        fprintf(stderr, "build process host failure\n");
        return 1;
    }
    if (!preparation_start(deadline)) {
        fprintf(stderr, "build process host failure\n");
        return 1;
    }
    const char *timing_directory = getenv("CHIBI_BUILD_TIMINGS");
    FILE *timings = build_timing_open(timing_directory);
    if (timing_directory && timing_directory[0] && !timings) {
        fprintf(stderr, "build timing trace unavailable: %s\n", strerror(errno));
    }
    const char *name = build_timing_name(path);
    const char *kind = !strcmp(name, "Graph.chibi")         ? "graph-host"
                     : !strcmp(name, "Build.chibi")         ? "build-host"
                     : !strcmp(name, "AssetCompiler.chibi") ? "generator-host"
                                                            : "other-host";
    build_timing_write(timings, kind, 0, started, started, 0, 0);
    char *source_path = from_root(root, path);
    char *core_path = NULL;
    if (core) {
        char *joined = from_root(root, core);
        core_path = realpath(joined, NULL);
        free(joined);
        if (!core_path) {
            fprintf(stderr, "build core collection could not be found\n");
            free(source_path);
            return 1;
        }
    }
    chibi_build_collection named[CHIBI_BUILD_MAX_COLLECTIONS];
    char *roots[CHIBI_BUILD_MAX_COLLECTIONS] = { 0 };
    bool found = true;
    for (int i = 0; i < collection_count; i++) {
        const char *equals = strchr(collections[i], '=');
        char *joined = from_root(root, equals + 1);
        roots[i] = realpath(joined, NULL);
        free(joined);
        found &= roots[i] != NULL;
        size_t length = (size_t)(equals - collections[i]);
        char *name = xmalloc(length + 1);
        memcpy(name, collections[i], length);
        name[length] = 0;
        named[i] = (chibi_build_collection){ name, roots[i] };
    }
    char *text = found ? read_source(source_path, deadline) : NULL;
    if (!text) {
        fprintf(stderr, found ? "build source could not be read\n" : "build collection could not be found\n");
        free(source_path);
        free(core_path);
        for (int i = 0; i < collection_count; i++) {
            free((char *)named[i].name);
            free(roots[i]);
        }
        return 1;
    }
    char *sources = compiler_sources ? realpath(compiler_sources, NULL)
                                     : shared("compiler");
    char *compiler_collection = compiler_core ? realpath(compiler_core, NULL)
                                              : shared("core");
    char error[256] = "SDK compiler sources and core collection could not be "
                      "found";
    double loading = timings ? chibi_build_now() : 0;
    build_timing_write(timings, "startup", 0, started, loading, 0, 0);
    char *cache = compiler_cache(root);
    chibi_build_compiler *compiler = sources && compiler_collection
                                       ? chibi_build_compiler_load(
                                             sources,
                                             compiler_collection,
                                             cache,
                                             error,
                                             sizeof error
                                         )
                                       : NULL;
    free(cache);
    if (timings) {
        build_timing_write(timings, "compiler-load", 0, loading, chibi_build_now(), 0, compiler ? 0 : 1);
    }
    free(sources);
    free(compiler_collection);
    chibi_build_options options = {
        .compiler = compiler,
        .root = root,
        .source_path = source_path,
        .source_text = text,
        .core = core_path,
        .collections = named,
        .collection_count = collection_count,
        .entry = entry,
        .arguments = (const char *const *)&argv[arguments_start],
        .argument_count = argc - arguments_start,
        .backend = BEHAVIOR_BYTECODE,
        .deadline = deadline,
        .cancelled = &cancelled,
        .prepared = preparation_end,
        .timings = timings,
    };
    chibi_build_result result = { 0 };
    bool success = compiler && chibi_build_invoke(&options, &result);
    if (!compiler) {
        snprintf(result.diagnostic, sizeof result.diagnostic, "%s", error);
    }
    fwrite(result.stdout_text, 1, result.stdout_size, stdout);
    fwrite(result.stderr_text, 1, result.stderr_size, stderr);
    if (result.stdout_truncated) {
        fprintf(stderr, "build stdout truncated\n");
    }
    if (result.stderr_truncated) {
        fprintf(stderr, "build stderr truncated\n");
    }
    if (!success) {
        fprintf(stderr, "%s\n", result.diagnostic);
    }
    chibi_build_compiler_free(compiler);
    free(text);
    free(source_path);
    free(core_path);
    for (int i = 0; i < collection_count; i++) {
        free((char *)named[i].name);
        free(roots[i]);
    }
    if (timings) {
        build_timing_write(
            timings,
            "invocation",
            0,
            started,
            chibi_build_now(),
            result.value,
            success ? 0 : 1
        );
        bool failed = ferror(timings);
        if (fclose(timings) || failed) {
            fprintf(stderr, "build timing trace incomplete\n");
        }
    }
    /* Authored negative signal results retain their conventional CLI encoding. */
    return success ? (result.value < 0 && result.value >= -127 ? 128 - result.value
                                                               : result.value)
                   : 1;
}
