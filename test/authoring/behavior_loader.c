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
static game_preview_world *world;
static game_prototype_preview *preview;
static game_prototype *prototype;
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

static game_prototype_registry registry()
{
    return (game_prototype_registry){ GAME_REGISTRY_COMPONENTS,
                                      GAME_REGISTRY_COMPONENT_COUNT };
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
    game_prototype_preview_free(preview);
    preview = NULL;
    game_preview_world_free(world);
    world = NULL;
    ecs_world_free(ecs);
    ecs = NULL;
    av_value_free(components);
    components = NULL;
    game_prototype_free(prototype);
    prototype = NULL;
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
    int loaded = app_behavior_fixtures_load(world, preview, root);
    fflush(stderr);
    assert(dup2(previous, STDERR_FILENO) >= 0);
    close(previous);
    rewind(capture);
    size_t count = fread(messages, 1, size - 1, capture);
    messages[count] = '\0';
    fclose(capture);
    return loaded;
}

static int applied(game_preview_world *w)
{
    const authoring_sources *sources = game_preview_sources_applied(w);
    return sources ? authoring_sources_count(sources) : 0;
}

TEST_CASE(sources_load_into_both_worlds_or_neither)
{
    game_prototype_registry prototypes = registry();
    ecs = ecs_world_new();
    world = game_preview_world_new(ecs);
    preview = game_prototype_preview_new(&prototypes);
    game_preview_world *live = game_prototype_preview_live(preview);
    write_behavior("Counter", "behavior Counter {\n    steps := 0\n    on Tick {\n        steps += 1\n    }\n}\n");
    char messages[4096];
    REQUIRE(load(messages, sizeof messages) == 1);
    REQUIRE(applied(world) == 1 && applied(live) == 1);
    char path[512];
    snprintf(path, sizeof path, "%s/behaviors/Counter.chibi", root);
    const authoring_source *counter = game_preview_source_find(world, path, true);
    REQUIRE(counter && counter->valid);

    /* One broken source refuses the whole collection; both worlds keep the
     * generation they had, and the failure names the root and stage. */
    write_behavior("Broken", "behavior Broken { on Tick(\n");
    REQUIRE(load(messages, sizeof messages) == -1);
    REQUIRE(strstr(messages, "Could not load graph asset") && strstr(messages, root));
    REQUIRE(strstr(messages, "(source preparation)"));
    REQUIRE(applied(world) == 1 && applied(live) == 1);
    return true;
}

TEST_CASE(failed_scene_launch_destroys_entities_before_their_strings)
{
    game_prototype_registry prototypes = registry();
    char error[256] = "";
    prototype = game_prototype_load(
        "name \"StringOwner\"\ncomponents {\n    GameUi "
        "document=\"prototype\"\n}\n",
        &prototypes,
        error,
        sizeof error
    );
    REQUIRE(prototype);
    preview = game_prototype_preview_new(&prototypes);
    av_doc *doc = av_doc_read("GameUi document=\"scene override string\"\n", error, sizeof error);
    REQUIRE(doc);
    components = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    const char *missing[] = { "MissingPreviewBehavior" };
    const game_prototype_preview_scene_entity entity = { .prototype = prototype,
                                                         .parent = -1,
                                                         .components = components,
                                                         .behaviors = missing,
                                                         .behavior_count = 1,
                                                         .own_behaviors = true };
    REQUIRE(
        game_prototype_preview_launch_scene(preview, &entity, 1, error, sizeof error) == ECS_ERR_COMMAND
    );
    REQUIRE(ecs_world_live_count(game_prototype_preview_world(preview)) == 0);
    return true;
}

void test_behavior_loader()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(sources_load_into_both_worlds_or_neither);
    RUN_TEST_CASE(failed_scene_launch_destroys_entities_before_their_strings);
    pu_clear_setup();
}
