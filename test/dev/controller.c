#include "host/dev.h"
#include "host/state_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void generation(const char *path, const char *config, const char *digest)
{
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(
        file,
        "{\"status\":\"ready\",\"application\":\"/%s.so\",\"host\":\"/host\","
        "\"directory\":\"/tmp\",\"resident\":\"resident\","
        "\"source_digest\":\"%s\",\"config\":\"%s\"}",
        digest,
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

static const char *await_candidate(bp_dev *dev)
{
    for (int i = 0; i < 500; i++) {
        const char *candidate = bp_dev_poll(dev);
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
    char *actual = bp_read_file(path);
    assert(actual && !strcmp(actual, expected));
    free(actual);
}

void test_dev_controller()
{
    char root[] = "/tmp/blueprints-controller-XXXXXX";
    assert(mkdtemp(root));
    char state[512], source[512], code[512], config[512], initial[512],
        next[512], builder[512], manifest[512], attempts[512];
    snprintf(state, sizeof state, "%s/.blueprints", root);
    snprintf(source, sizeof source, "%s/src", root);
    snprintf(code, sizeof code, "%s/src/code.c", root);
    snprintf(config, sizeof config, "%s/config.json", root);
    snprintf(initial, sizeof initial, "%s/initial.json", root);
    snprintf(next, sizeof next, "%s/next.json", root);
    snprintf(builder, sizeof builder, "%s/builder", root);
    snprintf(manifest, sizeof manifest, "%s/.blueprints/application.current", root);
    snprintf(attempts, sizeof attempts, "%s/attempts", root);
    assert(mkdir(state, 0700) == 0 && mkdir(source, 0700) == 0);
    assert(bp_write_file(code, "initial\n"));
    assert(
        bp_write_file(builder, "#!/bin/sh\nfor arg do\ncase \"$arg\" in -DRESULT=*) result=${arg#-DRESULT=};; esac\ndone\nprintf x >> attempts\ntouch src\nsleep 0.2\ncp next.json \"$result\"\n")
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
    generation(initial, config, "initial");
    generation(next, config, "rejected");
    bp_dev *dev = bp_dev_create(config, initial, 2);
    assert(dev && !strcmp(bp_dev_initial_application(dev), "/initial.so"));
    bp_dev_accept(dev, true);
    assert_manifest(manifest, "/initial.so\n");
    assert(!strcmp(await_candidate(dev), "/rejected.so"));
    bp_dev_accept(dev, false);
    assert_manifest(manifest, "/initial.so\n");
    /* Configure/build tools may update directory metadata without editing sources.
     * A completed attempt must settle instead of scheduling another build. */
    for (int i = 0; i < 120; i++) {
        assert(!bp_dev_poll(dev));
        pause_poll();
    }
    assert_manifest(attempts, "x");
    assert(bp_write_file(code, "retry rejected\n"));
    for (int i = 0; i < 120; i++) {
        assert(!bp_dev_poll(dev));
        pause_poll();
    }
    assert_manifest(manifest, "/initial.so\n");
    assert_manifest(attempts, "xx");
    generation(next, config, "accepted");
    assert(bp_write_file(code, "accepted\n"));
    assert(!strcmp(await_candidate(dev), "/accepted.so"));
    assert_manifest(attempts, "xxx");
    /* A completed build is still unpublished until the loader acknowledges it. */
    assert_manifest(manifest, "/initial.so\n");
    bp_dev_accept(dev, true);
    assert_manifest(manifest, "/accepted.so\n");
    bp_dev_stop(dev);
    while (!bp_dev_stopped(dev)) {
        pause_poll();
    }
    bp_dev_free(dev);
    assert(unlink(code) == 0 && rmdir(source) == 0);
    assert(
        unlink(config) == 0 && unlink(initial) == 0 && unlink(next) == 0 &&
        unlink(builder) == 0 && unlink(attempts) == 0
    );
    assert(unlink(manifest) == 0);
    char path[512];
    snprintf(path, sizeof path, "%s/.blueprints/build.log", root);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/.blueprints/dev.lock", root);
    assert(unlink(path) == 0);
    snprintf(path, sizeof path, "%s/.blueprints/build-result-%ld.json", root, (long)getpid());
    assert(unlink(path) == 0);
    assert(rmdir(state) == 0 && rmdir(root) == 0);
    puts("PASS: native development publishes only accepted candidates and retains prior state after rejection");
}
