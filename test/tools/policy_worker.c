#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
    fprintf(file, "%d\n", getpid());
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

/* Readiness, not elapsed runtime, proves concurrent selector execution. */
static void wait_marker(const char *directory, const char *name)
{
    char path[4096];
    worker_path(path, directory, name);
    for (int i = 0; i < 5000 && access(path, F_OK); i++) {
        pause_seconds(.001);
    }
    assert(!access(path, F_OK));
}

static int parallel_worker(int argc, char **argv)
{
    assert(argc == 5);
    const char *root = argv[2], *name = argv[3], *mode = argv[4];
    char path[4096];
    worker_path(path, root, name);
    int record = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(record >= 0 && !close(record));
    if (!strcmp(mode, "barrier")) {
        const char *temporary = getenv("TMPDIR");
        assert(temporary && *temporary);
        worker_path(path, temporary, "same-name");
        int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(file >= 0 && write(file, name, strlen(name)) == (ssize_t)strlen(name));
        assert(!close(file));
        wait_marker(root, !strcmp(name, "first") ? "second" : "first");
        file = open(path, O_RDONLY);
        char text[32] = { 0 };
        assert(file >= 0 && read(file, text, sizeof text - 1) == (ssize_t)strlen(name));
        assert(!close(file) && !strcmp(text, name));
        printf("scratch-%s=%s\n", name, temporary);
        marker(root, !strcmp(name, "first") ? "first-overlapped" : "second-overlapped");
    }
    if (!strcmp(mode, "hold") || !strcmp(mode, "conflict")) {
        worker_path(path, root, "resource-lock");
        int lock = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(lock >= 0 && !close(lock));
        if (!strcmp(mode, "hold")) {
            marker(root, "resource-ready");
            wait_marker(root, "resource-release");
        }
        if (!strcmp(mode, "conflict")) {
            char done[4096];
            worker_path(done, root, "resource-done");
            assert(!access(done, F_OK));
        }
        assert(!unlink(path));
        marker(root, "resource-done");
    }
    if (!strcmp(mode, "barrier")) {
        if (!strcmp(name, "second")) {
            printf("completed-%s\n", name);
            assert(!fflush(stdout));
            marker(root, "second-output");
            return 0;
        }
        /* The actual streams finish in reverse order; the runner must still
         * publish complete selector logs in manifest order. */
        wait_marker(root, "second-output");
    }
    if (!strcmp(mode, "fail-active")) {
        wait_marker(root, "b/started");
    }
    printf("completed-%s\n", name);
    return !strcmp(mode, "fail") || !strcmp(mode, "fail-active") ? 7 : 0;
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

/* A stand-in for `gh release download TAG --repo R --pattern ASSET --dir D`:
 * records the command and its GH_TOKEN in gh.trace and copies gh-asset from the working
 * directory to D/ASSET; without gh-asset the download fails. */
static int release_download(int argc, char **argv)
{
    assert(
        argc == 10 && !strcmp(argv[4], "--repo") &&
        !strcmp(argv[6], "--pattern") && !strcmp(argv[8], "--dir")
    );
    FILE *trace = fopen("gh.trace", "a");
    assert(trace);
    for (int i = 0; i < argc; i++) {
        fprintf(trace, "%s%s", i ? " " : "gh", i ? argv[i] : "");
    }
    fprintf(trace, " token=%s\n", getenv("GH_TOKEN") ? getenv("GH_TOKEN") : "");
    assert(!fclose(trace));
    FILE *source = fopen("gh-asset", "rb");
    if (!source) {
        return 1;
    }
    char path[4096];
    worker_path(path, argv[9], argv[7]);
    FILE *target = fopen(path, "wb");
    assert(target);
    char buffer[4096];
    for (size_t n; (n = fread(buffer, 1, sizeof buffer, source)) > 0;) {
        assert(fwrite(buffer, 1, n, target) == n);
    }
    assert(!fclose(source) && !fclose(target));
    return 0;
}

/* Prints a working-directory file to stream, escaped as -### quotes it when
 * asked; a missing file prints nothing. */
static void print_file(const char *name, FILE *stream, bool escape)
{
    FILE *file = fopen(name, "r");
    if (!file) {
        return;
    }
    for (int c; (c = fgetc(file)) != EOF;) {
        if (escape && (c == '"' || c == '\\' || c == '$')) {
            fputc('\\', stream);
        }
        fputc(c, stream);
    }
    assert(!fclose(file));
}

/* A compiler driver's -### report as Apple clang prints it: each argument
 * double-quoted with ", \ and $ escaped, more than 4 KiB in all. The sysroot
 * comes from compiler-sysroot in the working directory; without that file
 * the command names none. driver.trace records the driver's argv. */
static int compiler_driver(int argc, char **argv)
{
    FILE *trace = fopen("driver.trace", "a");
    assert(trace);
    for (int i = 0; i < argc; i++) {
        fprintf(trace, "arg=%s\n", argv[i]);
    }
    assert(!fclose(trace));
    fputs(
        "Apple clang version 21.0.0 (fixture)\nTarget: arm64-apple-darwin\n"
        "Thread model: posix\nInstalledDir: /fixture/bin\n "
        "\"/fixture/bin/clang\" \"-cc1\"",
        stderr
    );
    for (int i = 0; i < 160; i++) {
        fprintf(stderr, " \"-target-feature\" \"+fixture%d\"", i);
    }
    if (!access("compiler-sysroot", F_OK)) {
        fputs(" \"-isysroot\" \"", stderr);
        print_file("compiler-sysroot", stderr, true);
        fputc('"', stderr);
    }
    fputs(" \"-x\" \"c\" \"-\"\n", stderr);
    return 0;
}

int main(int argc, char **argv)
{
    /* Emulate earlier private hosts: the M1 host does not recognize this
     * query, and one named for protocol N answers N but lacks today's
     * capabilities. */
    if (argc == 2 && !strcmp(argv[1], "--protocol-version")) {
        const char *named = strstr(argv[0], "protocol-");
        if (!named) {
            return 2;
        }
        printf("%d\n", atoi(named + strlen("protocol-")));
        return 0;
    }
    /* xcrun prints the SDK named by xcrun-sdk in the working directory. */
    if (argc == 2 && !strcmp(argv[1], "--show-sdk-path")) {
        print_file("xcrun-sdk", stdout, false);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-###")) {
            return compiler_driver(argc, argv);
        }
    }
    assert(argc >= 3);
    if (!strcmp(argv[1], "policy-ps")) {
        assert(argc == 4);
        marker(argv[2], "started");
        pause_seconds(strtod(argv[3], NULL));
        char *const inspect[] = { "ps", "-A", "-o", "pid=,ppid=", NULL };
        execvp(inspect[0], inspect);
        perror("ps");
        return 127;
    }
    if (!strcmp(argv[1], "build-blocked-signals")) {
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGINT);
        sigaddset(&blocked, SIGTERM);
        sigaddset(&blocked, SIGALRM);
        assert(!sigprocmask(SIG_BLOCK, &blocked, NULL));
        execv(argv[2], argv + 2);
        perror(argv[2]);
        return 127;
    }
    if (!strcmp(argv[1], "release") && !strcmp(argv[2], "download")) {
        return release_download(argc, argv);
    }
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
        char out[4096], err[4096];
        memset(out, 'o', sizeof out);
        memset(err, 'e', sizeof err);
        for (size_t size = (size_t)atoi(argv[2]), part; size; size -= part) {
            part = size < sizeof out ? size : sizeof out;
            assert(fwrite(out, 1, part, stdout) == part && fwrite(err, 1, part, stderr) == part);
        }
        return 0;
    }
    if (!strcmp(argv[1], "build-signal")) {
        raise(SIGTERM);
        return 99;
    }
    if (!strcmp(argv[1], "parallel")) {
        return parallel_worker(argc, argv);
    }
    if (!strcmp(argv[1], "policy-signal")) {
        assert(argc == 5);
        wait_marker(argv[2], "a/started");
        wait_marker(argv[2], "b/started");
        assert(!kill(atoi(argv[3]), atoi(argv[4])));
        return 0;
    }
    if (!strcmp(argv[1], "cleanup-probe")) {
        return cleanup_probe(argv[2]);
    }
    if (!strcmp(argv[1], "cat")) {
        assert(argc == 3);
        FILE *file = fopen(argv[2], "rb");
        assert(file);
        char bytes[4096];
        size_t size = fread(bytes, 1, sizeof bytes, file);
        assert(fwrite(bytes, 1, size, stdout) == size && !fclose(file));
        return 0;
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
