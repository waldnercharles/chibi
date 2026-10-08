#include "assets/project.h"
#include "support/file_io.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* chibi [PROJECT] [PLAYER OPTIONS]: prepares the project when its sources or
 * the compiler changed since the last preparation, then plays it. */

#define CHIBI_BUILD_DIR ".chibi/build"
#define CHIBI_ARTIFACT CHIBI_BUILD_DIR "/game.chibip"
#define CHIBI_COMPILER_STAMP CHIBI_BUILD_DIR "/compiler"

extern char **environ;

static char *join(const char *base, const char *name)
{
    size_t size = strlen(base) + strlen(name) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", base, name);
    return path;
}

/* Nanoseconds since the epoch, or -1 when the path does not exist. */
static int64_t modified(const char *path)
{
    struct stat info;
    if (stat(path, &info) != 0) {
        return -1;
    }
#if defined(__APPLE__)
    struct timespec time = info.st_mtimespec;
#else
    struct timespec time = info.st_mtim;
#endif
    return (int64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

/* Generated and version-control entries that never feed preparation. */
static bool ignored(const char *name)
{
    return !strcmp(name, ".") || !strcmp(name, "..") ||
           !strcmp(name, ".chibi") || !strcmp(name, ".git");
}

/* A directory on the current walk path, to stop at symlink cycles. */
typedef struct visit
{
    dev_t device;
    ino_t inode;
    const struct visit *parent;
} visit;

static bool visiting(const visit *path, const struct stat *info)
{
    for (; path; path = path->parent) {
        if (path->device == info->st_dev && path->inode == info->st_ino) {
            return true;
        }
    }
    return false;
}

/* Whether `directory` or anything under it, except ignored entries, changed
 * after `since`. A directory's own modification time covers entries added,
 * removed or renamed in it, and an entry that cannot be stated, such as a
 * dangling symlink, counts as changed. Symlinked directories are walked, and
 * a link back to a directory already on the path is not entered again. */
static bool walk_changed(const char *directory, int64_t since, const visit *parent)
{
    struct stat info;
    if (stat(directory, &info) != 0 || modified(directory) > since) {
        return true;
    }
    if (visiting(parent, &info)) {
        return false;
    }
    visit self = { info.st_dev, info.st_ino, parent };
    DIR *dir = opendir(directory);
    if (!dir) {
        return true;
    }
    bool changed = false;
    struct dirent *entry;
    while (!changed && (entry = readdir(dir))) {
        if (ignored(entry->d_name)) {
            continue;
        }
        char *path = join(directory, entry->d_name);
        struct stat child;
        if (stat(path, &child) == 0 && S_ISDIR(child.st_mode)) {
            changed = walk_changed(path, since, &self);
        } else {
            int64_t time = modified(path);
            changed = time < 0 || time > since;
        }
        free(path);
    }
    closedir(dir);
    return changed;
}

static bool changed_since(const char *directory, int64_t since)
{
    return walk_changed(directory, since, NULL);
}

static int run(char *const *argv)
{
    pid_t pid;
    int error = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
    if (error) {
        fprintf(stderr, "Cannot run %s: %s\n", argv[0], strerror(error));
        return 1;
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int usage(const char *program)
{
    printf(
        "Usage: %s [PROJECT] [PLAYER OPTIONS]\n"
        "PROJECT is a project directory or its project.kdl. Without one, the\n"
        "nearest project.kdl at or above the working directory is played.\n"
        "Player options are passed to chibi-player; see chibi-player --help.\n",
        program
    );
    return 0;
}

static bool open_project(chibi_project *project, const char *requested)
{
    char *found = requested ? NULL : chibi_project_find(".");
    if (!requested && !found) {
        fprintf(stderr, "No project.kdl in this directory or any parent\n");
        return false;
    }
    char error[512];
    bool opened = !chibi_project_open(project, requested ? requested : found, error, sizeof error);
    if (!opened) {
        fprintf(stderr, "%s\n", error);
    }
    free(found);
    return opened;
}

/* Whether the project's sources changed after `since`, including a content
 * directory and start scene that live outside the project root. */
static bool sources_changed(const chibi_project *project, int64_t since)
{
    size_t length = strlen(project->root);
    bool inside = !strncmp(project->content, project->root, length) &&
                  (project->content[length] == '/' || project->content[length] == '\0');
    if (changed_since(project->root, since) ||
        (!inside && changed_since(project->content, since))) {
        return true;
    }
    char *scene = chibi_project_scene_path(project, NULL);
    int64_t time = scene ? modified(scene) : 0;
    free(scene);
    return time < 0 || time > since;
}

/* Identifies the compiler that prepared an artifact: its resolved path and
 * modification time, so switching to another SDK also rebuilds. */
static char *compiler_identity(const char *compiler)
{
    char *resolved = realpath(compiler, NULL);
    if (!resolved) {
        return xstrdup("");
    }
    size_t size = strlen(resolved) + 32;
    char *identity = xmalloc(size);
    snprintf(identity, size, "%s %lld\n", resolved, (long long)modified(resolved));
    free(resolved);
    return identity;
}

static bool compiler_changed(const char *stamp, const char *identity)
{
    char *recorded = file_read_text(stamp);
    bool changed = !recorded || strcmp(recorded, identity);
    free(recorded);
    return changed;
}

static void record_compiler(const char *stamp, const char *identity)
{
    FILE *file = fopen(stamp, "wb");
    if (file) {
        fputs(identity, file);
        fclose(file);
    }
}

static int prepare(const char *bin, const chibi_project *project, const char *artifact)
{
    const char *root = project->root;
    char *compiler = join(bin, "chibi-compile");
    char *identity = compiler_identity(compiler);
    char *stamp = join(root, CHIBI_COMPILER_STAMP);
    int64_t prepared = modified(artifact);
    int result = 0;
    if (prepared < 0 || compiler_changed(stamp, identity) ||
        sources_changed(project, prepared)) {
        char *build = join(root, CHIBI_BUILD_DIR);
        char *dot = join(root, ".chibi");
        mkdir(dot, 0777);
        mkdir(build, 0777);
        char *argv[] = { compiler,   "--project-root", (char *)root,
                         "--output", (char *)artifact, NULL };
        result = run(argv);
        if (!result) {
            record_compiler(stamp, identity);
        }
        free(dot);
        free(build);
    }
    free(stamp);
    free(identity);
    free(compiler);
    return result;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        return usage(argv[0]);
    }
    int first = argc > 1 && argv[1][0] != '-' ? 2 : 1;
    chibi_project project = { 0 };
    if (!open_project(&project, first == 2 ? argv[1] : NULL)) {
        return 2;
    }
    char *root = project.root;
    char *bin = file_executable_dir();
    char *artifact = join(root, CHIBI_ARTIFACT);
    int prepared = prepare(bin, &project, artifact);
    if (prepared) {
        return prepared;
    }
    int count = argc - first;
    char **player = xmalloc((size_t)(count + 5) * sizeof *player);
    player[0] = join(bin, "chibi-player");
    player[1] = artifact;
    player[2] = "--resource-root";
    player[3] = root;
    memcpy(player + 4, argv + first, (size_t)count * sizeof *player);
    player[count + 4] = NULL;
    execv(player[0], player);
    fprintf(stderr, "Cannot run %s: %s\n", player[0], strerror(errno));
    return 1;
}
