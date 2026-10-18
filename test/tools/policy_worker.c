#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void pause_seconds(double seconds)
{
    struct timespec delay = { .tv_sec = (time_t)seconds,
                              .tv_nsec = (long)((seconds - (time_t)seconds) * 1000000000) };
    while (nanosleep(&delay, &delay)) {
    }
}

static void marker(const char *directory, const char *name)
{
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/%s", directory, name);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "w");
    assert(file);
    const char *deadline = getenv("CHIBI_TEST_DEADLINE");
    fprintf(file, "%d %s\n", getpid(), deadline ? deadline : "0");
    assert(!fclose(file));
}

/* A killed descendant adopted by a Linux subreaper stays a zombie until that
 * subreaper's cleanup reaps it; it has exited all the same. */
static bool zombie(int pid)
{
#ifdef __linux__
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    char state = 0;
    bool parsed = fscanf(file, "%*d (%*[^)]) %c", &state) == 1;
    fclose(file);
    return parsed && state == 'Z';
#else
    (void)pid;
    return false;
#endif
}

/* Probe inside the driver's lifetime, before its caller performs group cleanup. */
static int cleanup_probe(const char *directory)
{
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/started", directory);
    assert(length > 0 && length < (int)sizeof path);
    FILE *record = fopen(path, "r");
    if (!record) {
        return 2;
    }
    int pid = 0;
    bool valid = fscanf(record, "%d", &pid) == 1 && pid > 0;
    fclose(record);
    if (!valid) {
        return 2;
    }
    struct timespec start, now;
    assert(!clock_gettime(CLOCK_MONOTONIC, &start));
    for (;;) {
        errno = 0;
        if (kill(pid, 0) && errno != ESRCH) {
            return 2;
        }
        if (errno == ESRCH || zombie(pid)) {
            snprintf(path, sizeof path, "%s/escaped", directory);
            if (!access(path, F_OK)) {
                return 1;
            }
            return errno == ENOENT ? 0 : 2;
        }
        assert(!clock_gettime(CLOCK_MONOTONIC, &now));
        double elapsed = (double)(now.tv_sec - start.tv_sec) +
                         (double)(now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= 3.1) {
            return 1;
        }
        pause_seconds(.001);
    }
}

static void worker_path(char *out, const char *base, const char *name)
{
    int size = snprintf(out, 4096, "%s/%s", base, name);
    assert(size > 0 && size < 4096);
}

static void cache_value(const char *build, const char *name, char value[4096])
{
    char path[4096], line[8192];
    worker_path(path, build, "CMakeCache.txt");
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    size_t length = strlen(name);
    while (fgets(line, sizeof line, file)) {
        if (strncmp(line, name, length) || line[length] != ':') {
            continue;
        }
        char *equal = strchr(line + length, '=');
        assert(equal);
        equal[strcspn(equal, "\r\n")] = 0;
        assert(strlen(equal + 1) < 4096);
        strcpy(value, equal + 1);
    }
    assert(!fclose(file));
}

/* A bounded C stand-in for CMake. It records raw argv and produces only the
 * tiny cache/dependency metadata the production input-signature reader uses. */
static int toolchain_cmake(int argc, char **argv)
{
    const char *source = NULL, *build = NULL, *generator = "Unix Makefiles";
    if (getenv("CMAKE_GENERATOR")) {
        generator = getenv("CMAKE_GENERATOR");
    }
    bool building = !strcmp(argv[1], "--build");
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "-S")) {
            source = argv[i + 1];
        }
        if (!strcmp(argv[i], "-B") || !strcmp(argv[i], "--build")) {
            build = argv[i + 1];
        }
        if (!strcmp(argv[i], "-G")) {
            generator = argv[i + 1];
        }
    }
    assert(build && (building || source));
    FILE *trace = fopen("cmake.trace", "a");
    assert(trace);
    fprintf(trace, "%s\n", building ? "BUILD" : "CONFIGURE");
    for (int i = 0; i < argc; i++) {
        fprintf(trace, "arg=%zu:%s\n", strlen(argv[i]), argv[i]);
    }
    assert(!fclose(trace));
    if (building) {
        return !access("fail-build", F_OK) ? 38 : 0;
    }
    if (!access("fail-configure", F_OK)) {
        return 37;
    }
    assert(!mkdir(build, 0700) || errno == EEXIST);
    char cc[4096] = "cc", cxx[4096] = "c++", sdk[4096] = "";
    if (getenv("CC")) {
        snprintf(cc, sizeof cc, "%s", getenv("CC"));
    }
    if (getenv("CXX")) {
        snprintf(cxx, sizeof cxx, "%s", getenv("CXX"));
    }
    if (getenv("SDKROOT")) {
        snprintf(sdk, sizeof sdk, "%s", getenv("SDKROOT"));
    }
    cache_value(build, "CMAKE_C_COMPILER", cc);
    cache_value(build, "CMAKE_CXX_COMPILER", cxx);
    cache_value(build, "CMAKE_OSX_SYSROOT", sdk);
    for (int i = 1; i < argc; i++) {
        const char *definition = argv[i];
        if (!strcmp(definition, "-D") && i + 1 < argc) {
            definition = argv[++i];
        } else if (!strncmp(definition, "-D", 2)) {
            definition += 2;
        } else {
            continue;
        }
        const char *equal = strchr(definition, '=');
        if (!equal) {
            continue;
        }
        const char *colon = strchr(definition, ':');
        size_t length = (size_t)((colon && colon < equal ? colon : equal) - definition);
        if (length == strlen("CMAKE_C_COMPILER") &&
            !strncmp(definition, "CMAKE_C_COMPILER", length)) {
            snprintf(cc, sizeof cc, "%s", equal + 1);
        }
        if (length == strlen("CMAKE_CXX_COMPILER") &&
            !strncmp(definition, "CMAKE_CXX_COMPILER", length)) {
            snprintf(cxx, sizeof cxx, "%s", equal + 1);
        }
        if (length == strlen("CMAKE_OSX_SYSROOT") &&
            !strncmp(definition, "CMAKE_OSX_SYSROOT", length)) {
            snprintf(sdk, sizeof sdk, "%s", equal + 1);
        }
    }
    char path[4096];
    worker_path(path, build, "CMakeCache.txt");
    FILE *cache = fopen(path, "w");
    assert(cache);
    fprintf(
        cache,
        "CMAKE_GENERATOR:INTERNAL=%s\nCMAKE_C_COMPILER:FILEPATH=%s\n"
        "CMAKE_CXX_COMPILER:FILEPATH=%s\nCMAKE_OSX_SYSROOT:PATH=%s\n",
        generator,
        cc,
        cxx,
        sdk
    );
    assert(!fclose(cache));
    worker_path(path, build, "CMakeFiles");
    assert(!mkdir(path, 0700) || errno == EEXIST);
    worker_path(path, build, "CMakeFiles/Makefile.cmake");
    FILE *rules = fopen(path, "w");
    assert(rules);
    fprintf(rules, "set(CMAKE_MAKEFILE_DEPENDS\n \"%s/CMakeLists.txt\"\n \"%s/external.cmake\"\n)\n", source, source);
    assert(!fclose(rules));
    worker_path(path, build, "build.ninja");
    rules = fopen(path, "w");
    assert(rules);
    fputs("build build.ninja: RERUN_CMAKE | ", rules);
    for (int item = 0; item < 2; item++) {
        for (const char *c = source; *c; c++) {
            if (*c == ' ' || *c == '$') {
                fputc('$', rules);
            }
            fputc(*c, rules);
        }
        fputs(item ? "/external.cmake\n" : "/CMakeLists.txt ", rules);
    }
    assert(!fclose(rules));
    return 0;
}

int main(int argc, char **argv)
{
    /* Emulate an earlier private host which does not recognize this query. */
    if (argc == 2 && !strcmp(argv[1], "--protocol-version")) {
        return 2;
    }
    assert(argc >= 3);
    if (!strcmp(argv[1], "-S") &&
        !(argc == 6 && !strcmp(argv[4], "build self-hosting"))) {
        return toolchain_cmake(argc, argv);
    }
    if (!strcmp(argv[1], "--build") && strcmp(argv[2], "build self-hosting")) {
        return toolchain_cmake(argc, argv);
    }
    if (!strcmp(argv[1], "-S")) {
        assert(argc == 6 && !strcmp(argv[0], "cmake"));
        assert(!strcmp(argv[2], ".") && !strcmp(argv[3], "-B"));
        assert(!strcmp(argv[4], "build self-hosting"));
        assert(!strcmp(argv[5], "-DCHIBI_NOTE=space \"quote\";$literal*"));
        puts("configure");
        return !access("fail-configure", F_OK) ? 37 : 0;
    }
    if (!strcmp(argv[1], "--build")) {
        assert(argc == 3 && !strcmp(argv[0], "cmake"));
        assert(!strcmp(argv[2], "build self-hosting"));
        puts("build");
        return !access("fail-build", F_OK) ? 38 : 0;
    }
    if (!strcmp(argv[1], "build-argv")) {
        char cwd[4096];
        assert(getcwd(cwd, sizeof cwd));
        printf("cwd=%s\n", cwd);
        printf("allowed=%s\n", getenv("CHIBI_BUILD_ALLOWED") ? getenv("CHIBI_BUILD_ALLOWED") : "");
        printf("secret=%s\n", getenv("CHIBI_BUILD_SECRET") ? getenv("CHIBI_BUILD_SECRET") : "");
        printf("deadline=%s\n", getenv("CHIBI_TEST_DEADLINE") ? getenv("CHIBI_TEST_DEADLINE") : "");
        for (int i = 2; i < argc; i++) {
            printf("arg=%zu:%s\n", strlen(argv[i]), argv[i]);
        }
        fputs("worker-stderr\n", stderr);
        return 23;
    }
    if (!strcmp(argv[1], "build-binary")) {
        assert(argc == 3);
        assert(!strcmp(argv[2], "stdout") || !strcmp(argv[2], "stderr"));
        const char bytes[] = { 'x', 0, 'y' };
        FILE *stream = !strcmp(argv[2], "stdout") ? stdout : stderr;
        assert(fwrite(bytes, 1, sizeof bytes, stream) == sizeof bytes);
        return 0;
    }
    if (!strcmp(argv[1], "build-output")) {
        for (int i = 0; i < 16384; i++) {
            fputc('o', stdout);
            fputc('e', stderr);
        }
        return 0;
    }
    if (!strcmp(argv[1], "build-signal")) {
        raise(SIGTERM);
        return 99;
    }
    if (!strcmp(argv[1], "cleanup-probe")) {
        return cleanup_probe(argv[2]);
    }
    if (!strcmp(argv[1], "exit")) {
        return atoi(argv[2]);
    }
    if (!strcmp(argv[1], "sleep")) {
        pause_seconds(strtod(argv[2], NULL));
        return 0;
    }
    if (!strcmp(argv[1], "mark")) {
        marker(argv[2], "last-command");
        return 0;
    }
    if (!strcmp(argv[1], "sibling")) {
        marker(argv[2], "sibling-ready");
        char path[4096];
        int length = snprintf(path, sizeof path, "%s/sibling-release", argv[2]);
        assert(length > 0 && length < (int)sizeof path);
        for (int i = 0; i < 6000 && access(path, F_OK); i++) {
            pause_seconds(.001);
        }
        assert(!access(path, F_OK));
        marker(argv[2], "sibling");
        return 0;
    }
    assert(!strcmp(argv[1], "descendant") && argc == 5);
    int child = fork();
    assert(child >= 0);
    if (!child) {
        if (atoi(argv[3])) {
            assert(setsid() >= 0);
        }
        marker(argv[2], "started");
        pause_seconds(3);
        marker(argv[2], "escaped");
        return 0;
    }
    if (atoi(argv[4])) {
        char path[4096];
        snprintf(path, sizeof path, "%s/started", argv[2]);
        for (int i = 0; i < 200 && access(path, F_OK); i++) {
            pause_seconds(.01);
        }
        assert(!access(path, F_OK));
        return 0;
    }
    pause_seconds(6);
    return 0;
}
