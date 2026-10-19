#include "assets/project.h"
#include "host/export.h"
#include "host/process.h"
#include "support/file_io.h"
#include "support/xalloc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>
#ifdef _WIN32
#include <signal.h>
#include <sys/utime.h>
#endif

/* chibi version: prints the SDK, build, compiler and artifact-format identity
 * from the build identity document.
 * chibi build [PROJECT]: prepares the project when its sources or the compiler
 * changed since the last preparation.
 * chibi check [PROJECT]: prepares the project as build does, without
 * migrating it or writing anything.
 * chibi run [PROJECT] [PLAYER OPTIONS]: prepares, then plays the project.
 * chibi [PROJECT] [PLAYER OPTIONS]: shorthand for `chibi run`.
 * chibi export [PROJECT] --output DIR [--web PLAYER]: builds a runnable game
 * folder, or a static site for browsers. */

#define CHIBI_BUILD_DIR ".chibi/build"
#define CHIBI_ARTIFACT CHIBI_BUILD_DIR "/game.chibip"
#define CHIBI_COMPILER_STAMP CHIBI_BUILD_DIR "/compiler"

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
#if defined(_WIN32)
    /* The Windows runtime stats whole seconds. */
    struct timespec time = { .tv_sec = info.st_mtime };
#elif defined(__APPLE__)
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
#ifdef _WIN32
    /* The Windows runtime reports no inodes, so links back up the walk path
     * are not detected there. */
    return false;
#else
    for (; path; path = path->parent) {
        if (path->device == info->st_dev && path->inode == info->st_ino) {
            return true;
        }
    }
    return false;
#endif
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

static int usage(const char *program)
{
    printf(
        "Usage: %s [PROJECT] [PLAYER OPTIONS]\n"
        "       %s version\n"
        "       %s build [PROJECT]\n"
        "       %s check [PROJECT]\n"
        "       %s run [PROJECT] [--] [PLAYER OPTIONS]\n"
        "       %s export [PROJECT] --output DIR [--web PLAYER] [--archive]\n"
        "PROJECT is a project directory or its project.kdl. Without one, the\n"
        "nearest project.kdl at or above the working directory is used.\n"
        "version prints the SDK, build, compiler and artifact-format "
        "identity.\n"
        "build prepares .chibi/build/game.chibip without starting the player.\n"
        "check parses, resolves and validates the project as build does, but\n"
        "migrates and writes nothing; an older project fails until migrated.\n"
        "run prepares and starts the player; the unnamed form is equivalent.\n"
        "Player options are passed to chibi-player; see chibi-player --help.\n"
        "export compiles the project into DIR with the player, its resources\n"
        "and engine files, ready to run and ship: a DIR.app on macOS, a\n"
        "folder with an .exe on Windows, and a folder with a .desktop entry\n"
        "elsewhere. --web PLAYER writes a static site instead, with the\n"
        "browser player from the PLAYER folder (make package-web-player).\n"
        "--archive also writes DIR.tar.gz.\n",
        program,
        program,
        program,
        program,
        program,
        program
    );
    return 0;
}

/* Opening migrates an older project; reading leaves it as it is. */
static bool open_project(chibi_project *project, const char *requested, bool migrate)
{
    char *found = requested ? NULL : chibi_project_find(".");
    if (!requested && !found) {
        fprintf(stderr, "No project.kdl in this directory or any parent\n");
        return false;
    }
    char error[512];
    const char *path = requested ? requested : found;
    bool opened = !(
        migrate ? chibi_project_open(project, path, error, sizeof error)
                : chibi_project_read(project, path, error, sizeof error)
    );
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
    char *compiler = join(bin, "chibi-compile" CHIBI_EXECUTABLE_SUFFIX);
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
        /* Date the artifact to when compilation started, so a source saved
         * while the compiler runs is newer and rebuilds next time. */
        struct timespec started[2];
        clock_gettime(CLOCK_REALTIME, &started[0]);
        started[1] = started[0];
        result = chibi_process_run(argv);
        if (!result) {
#ifdef _WIN32
            /* A second early, as modified() truncates, so a source saved in
             * the same second is newer. */
            time_t second = started[0].tv_sec - 1;
            utime(artifact, &(struct utimbuf){ .actime = second, .modtime = second });
#else
            utimensat(AT_FDCWD, artifact, started, 0);
#endif
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

static int export_game(int argc, char **argv)
{
    const char *requested = NULL, *output = NULL, *web = NULL;
    bool archive = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            return usage(argv[0]);
        }
        if (!strcmp(argv[i], "--archive")) {
            archive = true;
        } else if (!strcmp(argv[i], "--output") && i + 1 < argc && !output) {
            output = argv[++i];
        } else if (!strcmp(argv[i], "--web") && i + 1 < argc && !web) {
            web = argv[++i];
        } else if (argv[i][0] != '-' && !requested) {
            requested = argv[i];
        } else {
            fprintf(stderr, "Invalid export option: %s; use --help.\n", argv[i]);
            return 2;
        }
    }
    if (!output || !output[0]) {
        fprintf(stderr, "Export needs --output DIR\n");
        return 2;
    }
    chibi_project project = { 0 };
    if (!open_project(&project, requested, true)) {
        return 2;
    }
    char *bin = file_executable_dir();
    chibi_export_spec spec = { .project = project.root,
                               .name = project.name,
                               .title = project.application.title,
                               .icon = project.application.icon,
                               .bin = bin,
                               .output = output,
#if defined(__APPLE__)
                               .layout = CHIBI_EXPORT_APP,
#elif defined(_WIN32)
                               .layout = CHIBI_EXPORT_WINDOWS,
#endif
                               .archive = archive };
    if (web) {
        spec.layout = CHIBI_EXPORT_WEB;
        spec.web = web;
    }
    bool exported = chibi_export(&spec);
    free(bin);
    chibi_project_free(&project);
    return exported ? 0 : 1;
}

static int project_command(const char *requested, int player_argc, char **player_argv, bool play)
{
    chibi_project project = { 0 };
    if (!open_project(&project, requested, true)) {
        return 2;
    }
    char *bin = file_executable_dir();
    char *artifact = join(project.root, CHIBI_ARTIFACT);
    int result = prepare(bin, &project, artifact);
    if (result || !play) {
        free(artifact);
        free(bin);
        chibi_project_free(&project);
        return result;
    }
    char **player = xmalloc((size_t)(player_argc + 5) * sizeof *player);
    player[0] = join(bin, "chibi-player" CHIBI_EXECUTABLE_SUFFIX);
    player[1] = artifact;
    player[2] = "--resource-root";
    player[3] = project.root;
    memcpy(player + 4, player_argv, (size_t)player_argc * sizeof *player);
    player[player_argc + 4] = NULL;
#ifdef _WIN32
    /* Windows has no exec that keeps the process; wait for the player, which
     * handles Ctrl+C itself. */
    signal(SIGINT, SIG_IGN);
    result = chibi_process_run(player);
#else
    execv(player[0], player);
    fprintf(stderr, "Cannot run %s: %s\n", player[0], strerror(errno));
    result = 1;
#endif
    free(player[0]);
    free(player);
    free(artifact);
    free(bin);
    chibi_project_free(&project);
    return result;
}

/* Whether there are artifact formats and each records an integer version. */
static bool formats_valid(yyjson_val *formats)
{
    if (!yyjson_is_obj(formats) || !yyjson_obj_size(formats)) {
        return false;
    }
    size_t index, count;
    yyjson_val *name, *format;
    bool valid = true;
    yyjson_obj_foreach(formats, index, count, name, format)
    {
        valid = valid && yyjson_is_int(yyjson_obj_get(format, "version"));
    }
    return valid;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(yyjson_get_str(*(yyjson_val *const *)a), yyjson_get_str(*(yyjson_val *const *)b));
}

/* Each artifact format and its version, by name, whatever the document's order. */
static void print_formats(yyjson_val *formats)
{
    size_t index, count;
    yyjson_val *name, *format;
    yyjson_val **names = xmalloc(yyjson_obj_size(formats) * sizeof *names);
    yyjson_obj_foreach(formats, index, count, name, format)
    {
        names[index] = name;
    }
    qsort(names, count, sizeof *names, by_name);
    printf("artifact formats:");
    for (size_t i = 0; i < count; i++) {
        yyjson_val *version = yyjson_obj_get(
            yyjson_obj_get(formats, yyjson_get_str(names[i])),
            "version"
        );
        printf("%s %s %d", i ? "," : "", yyjson_get_str(names[i]), yyjson_get_int(version));
    }
    printf("\n");
    free(names);
}

static int print_version(int argc, char **argv)
{
    if (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h"))) {
        return usage(argv[0]);
    }
    if (argc > 2) {
        fprintf(stderr, "Invalid version option; use --help.\n");
        return 2;
    }
    /* The build identity document configuring writes and the build tree and
     * installed SDKs keep beside their shaders; packaging adds a build ID. */
    char *bin = file_executable_dir();
    char *path = join(bin, "../share/chibi/engine.json");
    yyjson_read_err error = { 0 };
    yyjson_doc *document = yyjson_read_file(path, 0, NULL, &error);
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *sdk = yyjson_obj_get(root, "sdk_version");
    yyjson_val *project = yyjson_obj_get(root, "project_version");
    yyjson_val *build = yyjson_obj_get(root, "build_id");
    yyjson_val *formats = yyjson_obj_get(root, "artifact_formats");
    bool valid = yyjson_is_str(sdk) && yyjson_is_int(project) &&
                 formats_valid(formats) && (!build || yyjson_is_str(build));
    if (valid) {
        char *compiler = join(bin, "chibi-compile" CHIBI_EXECUTABLE_SUFFIX);
        printf("chibi %s\n", yyjson_get_str(sdk));
        printf("build: %s\n", build ? yyjson_get_str(build) : "unpackaged");
        printf("compiler: %s\n", compiler);
        printf("project format: %d\n", yyjson_get_int(project));
        print_formats(formats);
        free(compiler);
    } else {
        fprintf(
            stderr,
            "Invalid build identity %s%s%s\n",
            path,
            document ? "" : ": ",
            document ? "" : error.msg
        );
    }
    yyjson_doc_free(document);
    free(path);
    free(bin);
    return valid ? 0 : 1;
}

/* Reads `chibi COMMAND [PROJECT]`: -1 with `requested` set, or the status to
 * exit with after help or an invalid argument. */
static int project_argument(int argc, char **argv, const char **requested)
{
    if (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h"))) {
        return usage(argv[0]);
    }
    if (argc > 3 || (argc == 3 && argv[2][0] == '-')) {
        fprintf(stderr, "Invalid %s option; use --help.\n", argv[1]);
        return 2;
    }
    *requested = argc == 3 ? argv[2] : NULL;
    return -1;
}

static int build_game(int argc, char **argv)
{
    const char *requested = NULL;
    int status = project_argument(argc, argv, &requested);
    return status >= 0 ? status : project_command(requested, 0, NULL, false);
}

static int check_game(int argc, char **argv)
{
    const char *requested = NULL;
    int status = project_argument(argc, argv, &requested);
    if (status >= 0) {
        return status;
    }
    chibi_project project = { 0 };
    if (!open_project(&project, requested, false)) {
        return 2;
    }
    char *bin = file_executable_dir();
    char *compiler = join(bin, "chibi-compile" CHIBI_EXECUTABLE_SUFFIX);
    char *args[] = { compiler, "--project-root", project.root, "--check", NULL };
    int result = chibi_process_run(args);
    free(compiler);
    free(bin);
    chibi_project_free(&project);
    return result;
}

static int run_game(int argc, char **argv)
{
    if (argc == 3 && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h"))) {
        return usage(argv[0]);
    }
    int first = 2;
    const char *requested = NULL;
    if (first < argc && strcmp(argv[first], "--") && argv[first][0] != '-') {
        requested = argv[first++];
    }
    if (first < argc && !strcmp(argv[first], "--")) {
        first++;
    }
    return project_command(requested, argc - first, argv + first, true);
}

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        return usage(argv[0]);
    }
    if (argc > 1 && !strcmp(argv[1], "version")) {
        return print_version(argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "export")) {
        return export_game(argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "build")) {
        return build_game(argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "check")) {
        return check_game(argc, argv);
    }
    if (argc > 1 && !strcmp(argv[1], "run")) {
        return run_game(argc, argv);
    }
    int first = argc > 1 && argv[1][0] != '-' ? 2 : 1;
    return project_command(first == 2 ? argv[1] : NULL, argc - first, argv + first, true);
}
