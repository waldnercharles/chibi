#include "authoring/behavior_loader.h"

#include "authoring/preview_source.h"
#include "authoring/source_collection.h"
#include "authoring/source_prepare.h"
#include "runtime/component_catalog.h"
#include "support/asset_text.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The application loader prepares a content root's executable sources into
 * the authoring world and a prototype preview's live world as one collection,
 * and a preview releases what a failed scene launch spawned. */

static char root[] = "/tmp/chibi-loader-XXXXXX";
static ecs_world *ecs;
static preview_world *live;
static prototype_preview *preview;
static prototype *test_prototype;
static av_value *components;

static void write_behavior(const char *name, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/behaviors/%s.chibi", root, name);
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}

static void remove_behavior(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/behaviors/%s.chibi", root, name);
    unlink(path);
}

static prototype_registry registry()
{
    return (prototype_registry){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
}

static void setup()
{
    strcpy(root, "/tmp/chibi-loader-XXXXXX");
    assert(mkdtemp(root));
    char behaviors[512];
    snprintf(behaviors, sizeof behaviors, "%s/behaviors", root);
    assert(!mkdir(behaviors, 0700));
}

static void cleanup()
{
    prototype_preview_free(preview);
    preview = NULL;
    preview_world_free(live);
    live = NULL;
    ecs_world_free(ecs);
    ecs = NULL;
    av_value_free(components);
    components = NULL;
    prototype_free(test_prototype);
    test_prototype = NULL;
    remove_behavior("Counter");
    remove_behavior("Broken");
    char behaviors[512];
    snprintf(behaviors, sizeof behaviors, "%s/behaviors", root);
    rmdir(behaviors);
    rmdir(root);
}

/* Loads `root` with stderr captured into `messages`. */
static int load(char *messages, size_t size)
{
    FILE *capture = tmpfile();
    assert(capture);
    fflush(stderr);
    int previous = dup(STDERR_FILENO);
    assert(previous >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
    int loaded = app_behavior_fixtures_load(live, preview, root);
    fflush(stderr);
    assert(dup2(previous, STDERR_FILENO) >= 0);
    close(previous);
    rewind(capture);
    size_t count = fread(messages, 1, size - 1, capture);
    messages[count] = '\0';
    fclose(capture);
    return loaded;
}

static int applied(preview_world *w)
{
    const authoring_sources *sources = preview_sources_applied(w);
    return sources ? authoring_sources_count(sources) : 0;
}

TEST_CASE(sources_load_into_both_worlds_or_neither)
{
    prototype_registry prototypes = registry();
    ecs = ecs_world_new();
    live = preview_world_new(ecs);
    preview = prototype_preview_new(&prototypes);
    preview_world *preview_live = prototype_preview_live(preview);
    write_behavior("Counter", "behavior Counter {\n    steps := 0\n    on Tick {\n        steps += 1\n    }\n}\n");
    char messages[4096];
    REQUIRE(load(messages, sizeof messages) == 1);
    REQUIRE(applied(live) == 1 && applied(preview_live) == 1);
    char path[512];
    snprintf(path, sizeof path, "%s/behaviors/Counter.chibi", root);
    const authoring_source *counter = preview_source_find(live, path, true);
    REQUIRE(counter && counter->valid);

    /* One broken source refuses the whole collection; both worlds keep the
     * generation they had, and the failure names the root and stage. */
    write_behavior("Broken", "behavior Broken { on Tick(\n");
    REQUIRE(load(messages, sizeof messages) == -1);
    REQUIRE(strstr(messages, "Could not load graph asset") && strstr(messages, root));
    REQUIRE(strstr(messages, "(source preparation)"));
    REQUIRE(applied(live) == 1 && applied(preview_live) == 1);
    return true;
}

TEST_CASE(failed_scene_launch_destroys_entities_before_their_strings)
{
    prototype_registry prototypes = registry();
    char error[256] = "";
    test_prototype = prototype_load(
        "name \"StringOwner\"\ncomponents {\n    Ui "
        "document=\"prototype\"\n}\n",
        &prototypes,
        error,
        sizeof error
    );
    REQUIRE(test_prototype);
    preview = prototype_preview_new(&prototypes);
    av_doc *doc = av_doc_read("Ui document=\"scene override string\"\n", error, sizeof error);
    REQUIRE(doc);
    components = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    const char *missing[] = { "MissingPreviewBehavior" };
    const prototype_preview_scene_entity entity = { .prototype = test_prototype,
                                                    .parent = -1,
                                                    .components = components,
                                                    .behaviors = missing,
                                                    .behavior_count = 1,
                                                    .own_behaviors = true };
    REQUIRE(
        prototype_preview_launch_scene(preview, &entity, 1, error, sizeof error) == ECS_ERR_COMMAND
    );
    REQUIRE(ecs_world_live_count(prototype_preview_world(preview)) == 0);
    return true;
}

void test_behavior_loader()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(sources_load_into_both_worlds_or_neither);
    RUN_TEST_CASE(failed_scene_launch_destroys_entities_before_their_strings);
    pu_clear_setup();
}
