#include "host/dev.h"
#include "host/state_file.h"
#include "test/support/report.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void generation(const char *path, const char *config, const char *digest, const char *directory)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(
        file,
        "{\"status\":\"ready\",\"application\":\"/%s.so\",\"host\":\"/host\","
        "\"directory\":\"%s\",\"resident\":\"resident\","
        "\"source_digest\":\"%s\",\"config\":\"%s\"}",
        digest,
        directory,
        digest,
        config
    );
    assert(fclose(file) == 0);
}

static void pause_poll()
{
    struct timespec delay = { .tv_nsec = 10000000 };
    nanosleep(&delay, NULL);
}

static const char *await_candidate(chibi_dev *dev)
{
    for (int i = 0; i < 500; i++) {
        const char *candidate = chibi_dev_poll(dev);
        if (candidate) {
            return candidate;
        }
        pause_poll();
    }
    assert(!"development build did not produce a candidate");
    return NULL;
}

static void assert_manifest(const char *path, const char *expected)
{
    char *actual = chibi_read_file(path);
    assert(actual && !strcmp(actual, expected));
    free(actual);
}

void test_dev_controller()
{
    char root[] = "/tmp/chibi-controller-XXXXXX";
    assert(mkdtemp(root));
    char state[512], source[512], code[512], config[512], initial[512],
        next[512], builder[512], manifest[512], attempts[512], generations[512],
        initial_directory[512], lease_path[512];
    snprintf(state, sizeof state, "%s/.chibi", root);
    snprintf(source, sizeof source, "%s/src", root);
    snprintf(code, sizeof code, "%s/src/code.c", root);
    snprintf(config, sizeof config, "%s/config.json", root);
    snprintf(initial, sizeof initial, "%s/initial.json", root);
    snprintf(next, sizeof next, "%s/next.json", root);
    snprintf(builder, sizeof builder, "%s/builder", root);
    snprintf(manifest, sizeof manifest, "%s/.chibi/application.current", root);
    snprintf(attempts, sizeof attempts, "%s/attempts", root);
    snprintf(generations, sizeof generations, "%s/generations", state);
    snprintf(initial_directory, sizeof initial_directory, "%s/20200101-000000-0000000000000000", generations);
    snprintf(lease_path, sizeof lease_path, "%s/.lease", initial_directory);
    assert(
        mkdir(state, 0700) == 0 && mkdir(source, 0700) == 0 &&
        mkdir(generations, 0700) == 0 && mkdir(initial_directory, 0700) == 0
    );
    assert(chibi_write_file(code, "initial\n"));
    assert(
        chibi_write_file(builder, "#!/bin/sh\nfor arg do\ncase \"$arg\" in -DRESULT=*) result=${arg#-DRESULT=};; esac\ndone\nprintf x >> attempts\ntouch src\nsleep 0.2\ncp next.json \"$result\"\n")
    );
    assert(chmod(builder, 0700) == 0);
    FILE *file = fopen(config, "w");
    assert(file);
    fprintf(
        file,
        "{\"version\":1,\"source_dir\":\"%s\",\"cmake\":\"%s\","
        "\"roots\":[\"%s\"],\"suffixes\":[\".c\"]}",
        root,
        builder,
        source
    );
    assert(fclose(file) == 0);
    generation(initial, config, "initial", initial_directory);
    generation(next, config, "rejected", "/tmp");
    chibi_dev *dev = chibi_dev_create(config, initial, 2);
    assert(dev && !strcmp(chibi_dev_initial_application(dev), "/initial.so"));
    int lease = open(lease_path, O_RDWR);
    assert(
        lease >= 0 && flock(lease, LOCK_EX | LOCK_NB) == -1 &&
        (errno == EWOULDBLOCK || errno == EAGAIN)
    );
    chibi_dev_accept(dev, true);
    assert_manifest(manifest, "/initial.so\n");
    assert(!strcmp(await_candidate(dev), "/rejected.so"));
    chibi_dev_accept(dev, false);
    assert_manifest(manifest, "/initial.so\n");
    /* Configure/build tools may update directory metadata without editing sources.
     * A completed attempt must settle instead of scheduling another build. */
    for (int i = 0; i < 120; i++) {
        assert(!chibi_dev_poll(dev));
        pause_poll();
    }
    assert_manifest(attempts, "x");
    assert(chibi_write_file(code, "retry rejected\n"));
    for (int i = 0; i < 120; i++) {
        assert(!chibi_dev_poll(dev));
        pause_poll();
    }
    assert_manifest(manifest, "/initial.so\n");
    assert_manifest(attempts, "xx");
    generation(next, config, "accepted", "/tmp");
    assert(chibi_write_file(code, "accepted\n"));
    assert(!strcmp(await_candidate(dev), "/accepted.so"));
    assert_manifest(attempts, "xxx");
    /* A completed build is still unpublished until the loader acknowledges it. */
    assert_manifest(manifest, "/initial.so\n");
    chibi_dev_accept(dev, true);
    assert_manifest(manifest, "/accepted.so\n");
    chibi_dev_stop(dev);
    while (!chibi_dev_stopped(dev)) {
        pause_poll();
    }
    chibi_dev_free(dev);
    assert(flock(lease, LOCK_EX | LOCK_NB) == 0);
    assert(flock(lease, LOCK_UN) == 0 && close(lease) == 0);
    assert(unlink(code) == 0 && rmdir(source) == 0);
    assert(
        unlink(config) == 0 && unlink(initial) == 0 && unlink(next) == 0 &&
        unlink(builder) == 0 && unlink(attempts) == 0
    );
    assert(unlink(manifest) == 0);
    char path[512];
    snprintf(path, sizeof path, "%s/.chibi/build.log", root);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/.chibi/dev.lock", root);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/.chibi/build-result-%ld.json", root, (long)getpid());
    assert(unlink(path) == 0);
    assert(
        unlink(lease_path) == 0 && rmdir(initial_directory) == 0 &&
        rmdir(generations) == 0 && rmdir(state) == 0 && rmdir(root) == 0
    );
    test_note("PASS: native development publishes only accepted candidates and retains prior state after rejection\n");
}
