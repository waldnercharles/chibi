#include "dev.h"

#include "process.h"
#include "state_file.h"
#include "watch.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>

struct chibi_dev
{
    char *config, *source, *cmake, *script, *result, *log, *checkpoint;
    char *initial, *resident, *digest, *restart_notice, *candidate;
    char *watch_config, *attempted, *pending_digest, *manifest;
    chibi_watch *watch;
    chibi_process process;
    uint64_t revision, build_revision;
    double changed_at;
    int jobs, lock;
    bool dirty, building, closing, force_rebuild;
};

static void *allocate(size_t size)
{
    void *memory = malloc(size);
    if (!memory) {
        abort();
    }
    return memory;
}

static char *copy(const char *text)
{
    char *result = strdup(text);
    if (!result) {
        abort();
    }
    return result;
}

static char *append(const char *prefix, const char *suffix)
{
    size_t size = strlen(prefix) + strlen(suffix) + 1;
    char *result = allocate(size);
    snprintf(result, size, "%s%s", prefix, suffix);
    return result;
}

static double now()
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static yyjson_doc *read_json(const char *path)
{
    char *text = chibi_read_file(path);
    if (!text) {
        return NULL;
    }
    yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
    free(text);
    return doc;
}

static const char *text(yyjson_val *root, const char *key)
{
    return yyjson_get_str(yyjson_obj_get(root, key));
}

static bool ready(yyjson_val *root)
{
    const char *status = text(root, "status");
    return status && !strcmp(status, "ready") && text(root, "application") &&
           text(root, "host") && text(root, "directory") && text(root, "resident") &&
           text(root, "source_digest") && text(root, "config");
}

int chibi_dev_bootstrap(int argc, char **argv, const char *generation)
{
    yyjson_doc *doc = read_json(generation);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!ready(root)) {
        fprintf(stderr, "Invalid development generation: %s\n", generation);
        yyjson_doc_free(doc);
        return 1;
    }
    char **args = allocate((size_t)(argc + 5) * sizeof *args);
    int count = 0;
    args[count++] = (char *)text(root, "host");
    args[count++] = "--dev-config";
    args[count++] = (char *)text(root, "config");
    args[count++] = "--generation";
    args[count++] = (char *)generation;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bootstrap")) {
            i++;
            continue;
        }
        args[count++] = argv[i];
    }
    args[count] = NULL;
#ifdef __APPLE__
    const char *variable = "DYLD_LIBRARY_PATH";
#else
    const char *variable = "LD_LIBRARY_PATH";
#endif
    /* Every native dependency must resolve from the immutable snapshot. */
    if (setenv(variable, text(root, "directory"), 1) != 0 ||
        setenv("MallocNanoZone", "0", 1) != 0) {
        fprintf(stderr, "Could not set staged host environment: %s\n", strerror(errno));
        free(args);
        yyjson_doc_free(doc);
        return 1;
    }
    execv(args[0], args);
    fprintf(stderr, "Could not start staged host: %s\n", strerror(errno));
    free(args);
    yyjson_doc_free(doc);
    return 1;
}

static bool update_watch(chibi_dev *dev, yyjson_val *root)
{
    yyjson_val *roots = yyjson_obj_get(root, "roots");
    yyjson_val *suffixes = yyjson_obj_get(root, "suffixes");
    if (!yyjson_is_arr(roots) || !yyjson_is_arr(suffixes)) {
        return false;
    }
    char *paths_json = yyjson_val_write(roots, 0, NULL);
    char *suffix_json = yyjson_val_write(suffixes, 0, NULL);
    char *signature = append(paths_json, suffix_json);
    free(paths_json);
    free(suffix_json);
    if (dev->watch_config && !strcmp(dev->watch_config, signature)) {
        free(signature);
        return true;
    }
    int count = (int)yyjson_arr_size(roots),
        suffix_count = (int)yyjson_arr_size(suffixes);
    const char **paths = allocate((size_t)(count + 1) * sizeof *paths);
    const char **extensions = allocate((size_t)(suffix_count + 1) * sizeof *extensions);
    bool valid = count > 0;
    for (int i = 0; i < count; i++) {
        paths[i] = yyjson_get_str(yyjson_arr_get(roots, i));
        valid &= paths[i] != NULL;
    }
    for (int i = 0; i < suffix_count; i++) {
        extensions[i] = yyjson_get_str(yyjson_arr_get(suffixes, i));
        valid &= extensions[i] != NULL;
    }
    if (!valid) {
        errno = EINVAL;
    }
    chibi_watch *watch = valid ? chibi_watch_create(paths, count, extensions, suffix_count)
                               : NULL;
    free(paths);
    free(extensions);
    if (!watch) {
        fprintf(stderr, "Could not watch development sources: %s\n", strerror(errno));
        free(signature);
        return false;
    }
    bool replacing = dev->watch != NULL;
    chibi_watch_free(dev->watch);
    free(dev->watch_config);
    dev->watch = watch;
    dev->watch_config = signature;
    dev->revision = chibi_watch_revision(watch);
    if (replacing) {
        /* Reconcile once after replacing the subscriptions: edits can occur
         * between the completed build and installation of the new roots. */
        dev->dirty = true;
        dev->changed_at = now();
    }
    return true;
}

chibi_dev *chibi_dev_create(const char *config, const char *generation, int jobs)
{
    yyjson_doc *settings = read_json(config),
               *built = generation ? read_json(generation) : NULL;
    yyjson_val *root = yyjson_doc_get_root(settings),
               *initial = yyjson_doc_get_root(built);
    chibi_dev *dev = NULL;
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 ||
        !text(root, "source_dir") || !text(root, "cmake") || !ready(initial)) {
        fprintf(stderr, "Development mode requires valid --dev-config and --generation files\n");
        goto done;
    }
    dev = allocate(sizeof *dev);
    *dev = (chibi_dev){ .lock = -1, .jobs = jobs > 0 ? jobs : 4 };
    dev->source = copy(text(root, "source_dir"));
    dev->config = copy(config);
    dev->cmake = copy(text(root, "cmake"));
    dev->script = append(dev->source, "/cmake/dev_build.cmake");
    dev->log = append(dev->source, "/.chibi/build.log");
    dev->checkpoint = append(dev->source, "/.chibi/workspace.json");
    dev->manifest = append(dev->source, "/.chibi/application.current");
    char filename[80];
    snprintf(filename, sizeof filename, "/.chibi/build-result-%ld.json", (long)getpid());
    dev->result = append(dev->source, filename);
    dev->initial = copy(text(initial, "application"));
    dev->resident = copy(text(initial, "resident"));
    dev->digest = copy(text(initial, "source_digest"));
    dev->pending_digest = copy(dev->digest);
    char *lock = append(dev->source, "/.chibi/dev.lock");
    dev->lock = open(lock, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    free(lock);
    if (dev->lock < 0 || flock(dev->lock, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "Could not own development workspace (another host may be running): %s\n", strerror(errno));
        chibi_dev_free(dev);
        dev = NULL;
        goto done;
    }
    if (!update_watch(dev, root)) {
        chibi_dev_free(dev);
        dev = NULL;
        goto done;
    }
    /* Close the build-to-launch gap after installing the watcher. */
    dev->dirty = true;
    dev->changed_at = now();
done:
    yyjson_doc_free(settings);
    yyjson_doc_free(built);
    return dev;
}

const char *chibi_dev_initial_application(const chibi_dev *dev)
{
    return dev->initial;
}
const char *chibi_dev_checkpoint_path(const chibi_dev *dev)
{
    return dev->checkpoint;
}

static void start_build(chibi_dev *dev)
{
    char *config = append("-DDEV_CONFIG=", dev->config);
    char *result = append("-DRESULT=", dev->result);
    char jobs[32];
    snprintf(jobs, sizeof jobs, "-DJOBS=%d", dev->jobs);
    const char *args[] = { dev->cmake,
                           config,
                           result,
                           jobs,
                           dev->force_rebuild ? "-DFORCE_REBUILD=ON" : "-DFORCE_REBUILD=OFF",
                           "-P",
                           dev->script,
                           NULL };
    unlink(dev->result);
    dev->build_revision = dev->revision;
    dev->dirty = false;
    printf("Building application; diagnostics: %s\n", dev->log);
    fflush(stdout);
    dev->building = chibi_process_start(&dev->process, args, dev->source, dev->log);
    if (!dev->building) {
        fprintf(stderr, "Could not start build: %s\n", strerror(errno));
    }
    free(config);
    free(result);
}

static const char *complete_build(chibi_dev *dev)
{
    yyjson_doc *doc = read_json(dev->result);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *status = text(root, "status");
    const char *candidate = NULL;
    if (dev->revision != dev->build_revision) {
        dev->force_rebuild = true;
    }
    if (dev->process.status != 0 || !status || !strcmp(status, "failed")) {
        fprintf(stderr, "Build rejected; keeping the current editor. See %s\n", dev->log);
        goto done;
    }
    if (!strcmp(status, "stale") || dev->revision != dev->build_revision) {
        dev->force_rebuild = true;
        dev->dirty = true;
        dev->changed_at = now();
        goto done;
    }
    if (!ready(root)) {
        fprintf(stderr, "Invalid build result; keeping the current editor\n");
        goto done;
    }
    dev->force_rebuild = false;
    yyjson_doc *settings = read_json(dev->config);
    bool watched = settings && update_watch(dev, yyjson_doc_get_root(settings));
    yyjson_doc_free(settings);
    if (!watched) {
        goto done;
    }
    const char *resident = text(root, "resident");
    if (strcmp(resident, dev->resident)) {
        if (!dev->restart_notice || strcmp(dev->restart_notice, resident)) {
            free(dev->restart_notice);
            dev->restart_notice = copy(resident);
            fprintf(stderr, "Restart required: host, framework, or resident font/shader setup changed. Close the editor and run make dev again.\n");
        }
        goto done;
    }
    free(dev->restart_notice);
    dev->restart_notice = NULL;
    const char *digest = text(root, "source_digest");
    if (!strcmp(digest, dev->digest)) {
        free(dev->attempted);
        dev->attempted = NULL;
        goto done;
    }
    if (dev->attempted && !strcmp(digest, dev->attempted)) {
        goto done;
    }
    free(dev->attempted);
    dev->attempted = copy(digest);
    free(dev->pending_digest);
    dev->pending_digest = copy(digest);
    free(dev->candidate);
    dev->candidate = copy(text(root, "application"));
    candidate = dev->candidate;
done:
    yyjson_doc_free(doc);
    return candidate;
}

void chibi_dev_accept(chibi_dev *dev, bool loaded)
{
    assert(dev->pending_digest);
    if (loaded) {
        free(dev->digest);
        dev->digest = dev->pending_digest;
        dev->pending_digest = NULL;
        char *contents = append(dev->candidate ? dev->candidate : dev->initial, "\n");
        if (!chibi_write_file(dev->manifest, contents)) {
            fprintf(stderr, "Could not publish accepted application: %s\n", strerror(errno));
        }
        free(contents);
    }
    free(dev->pending_digest);
    dev->pending_digest = NULL;
}

const char *chibi_dev_poll(chibi_dev *dev)
{
    if (dev->closing) {
        return NULL;
    }
    uint64_t revision = chibi_watch_revision(dev->watch);
    if (revision != dev->revision) {
        dev->revision = revision;
        dev->dirty = true;
        dev->changed_at = now();
    }
    if (dev->building && chibi_process_poll(&dev->process)) {
        dev->building = false;
        return complete_build(dev);
    }
    if (!dev->building && dev->dirty && now() - dev->changed_at >= 0.35) {
        start_build(dev);
    }
    return NULL;
}

void chibi_dev_stop(chibi_dev *dev)
{
    dev->closing = true;
    if (dev->building) {
        chibi_process_stop(&dev->process);
    }
}

bool chibi_dev_stopped(chibi_dev *dev)
{
    if (dev->building && chibi_process_poll(&dev->process)) {
        dev->building = false;
    }
    return !dev->building;
}

void chibi_dev_free(chibi_dev *dev)
{
    if (!dev) {
        return;
    }
    assert(!dev->building);
    chibi_watch_free(dev->watch);
    if (dev->lock >= 0) {
        close(dev->lock);
    }
    free(dev->config);
    free(dev->source);
    free(dev->cmake);
    free(dev->script);
    free(dev->result);
    free(dev->log);
    free(dev->checkpoint);
    free(dev->initial);
    free(dev->resident);
    free(dev->digest);
    free(dev->restart_notice);
    free(dev->candidate);
    free(dev->watch_config);
    free(dev->attempted);
    free(dev->pending_digest);
    free(dev->manifest);
    free(dev);
}
