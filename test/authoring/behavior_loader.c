#include "authoring/behavior_loader.h"

#include "authoring/image_prepare.h"
#include "authoring/preview_source.h"
#include "authoring/source_collection.h"
#include "authoring/source_prepare.h"
#include "runtime/component_catalog.h"
#include "runtime/images/catalog.h"
#include "runtime/images/sky.h"
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
 * a preview prepares what a scene's overrides name, and it releases what a
 * failed scene launch spawned. */

static char root[] = "/tmp/chibi-loader-XXXXXX";
static ecs_world *ecs;
static preview_world *live;
static prototype_preview *preview;
static prototype *test_prototype;
static av_value *components;

static void write_source(const char *directory, const char *name, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s/%s.chibi", root, directory, name);
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
}

static void write_behavior(const char *name, const char *text)
{
    write_source("behaviors", name, text);
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
    const char *directories[] = { "behaviors", "relationships", "systems" };
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", root, directories[i]);
        assert(!mkdir(path, 0700));
    }
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
    render_skies_clear();
    render_image_catalog_clear();
    char path[512];
    snprintf(path, sizeof path, "%s/skies/override.hdr", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/skies", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/behaviors", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/relationships/Targets.chibi", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/relationships", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/systems/MoveTargets.chibi", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/systems", root);
    rmdir(path);
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
    write_behavior(
        "Counter",
        "Counter :: behavior {\n"
        "    steps := 0\n"
        "    on Tick :: () {\n"
        "        steps += 1\n"
        "    }\n"
        "}\n"
    );
    char messages[4096];
    REQUIRE(load(messages, sizeof messages) == 1);
    REQUIRE(applied(live) == 1 && applied(preview_live) == 1);
    char path[512];
    snprintf(path, sizeof path, "%s/behaviors/Counter.chibi", root);
    const authoring_source *counter = preview_source_find(live, path, true);
    REQUIRE(counter && counter->valid);

    /* One broken source refuses the whole collection; both worlds keep the
     * generation they had, and the failure names the root and stage. */
    write_behavior("Broken", "Broken :: behavior { on Tick(\n");
    REQUIRE(load(messages, sizeof messages) == -1);
    REQUIRE(strstr(messages, "Could not load graph asset") && strstr(messages, root));
    REQUIRE(strstr(messages, "(source preparation)"));
    REQUIRE(applied(live) == 1 && applied(preview_live) == 1);
    return true;
}

TEST_CASE(required_relationship_queries_publish_before_scenes)
{
    prototype_registry prototypes = registry();
    ecs = ecs_world_new();
    live = preview_world_new(ecs);
    preview = prototype_preview_new(&prototypes);
    preview_world *worlds[] = { live, prototype_preview_live(preview) };
    ecs_component_id targets = ecs_component_marker_id("relationship.Targets");
    REQUIRE(!ecs_world_relationship(ecs, targets));
    REQUIRE(!ecs_world_relationship(prototype_preview_world(preview), targets));
    write_source("relationships", "Targets", "Targets :: relationship { exclusive: true, acyclic: true, on_delete_target: Remove }\n");
    write_source(
        "systems",
        "MoveTargets",
        "MoveTargets :: system(target: entity in self.Targets, position: "
        "^Transform from target) {\n"
        "    position.X += 1\n"
        "}\n"
    );
    char messages[4096];
    REQUIRE(load(messages, sizeof messages) == 1);
    for (int i = 0; i < 2; i++) {
        ecs_world *world = worlds[i]->runtime->ecs;
        const ecs_relationship_desc *relationship = ecs_world_relationship(world, targets);
        REQUIRE(relationship);
        REQUIRE(relationship->traits == (ECS_REL_EXCLUSIVE | ECS_REL_ACYCLIC));
        REQUIRE(relationship->on_delete_target == ECS_CLEANUP_REMOVE);
        REQUIRE(applied(worlds[i]) == 1);
        REQUIRE(source_is_system(worlds[i]->runtime->sources, "MoveTargets"));
        const char *systems[] = { "MoveTargets" };
        REQUIRE(!source_select_systems(worlds[i]->runtime->sources, systems, 1));
        ecs_entity owner = ecs_world_create_entity(world);
        ecs_entity target = ecs_world_create_entity(world);
        ecs_entity unrelated = ecs_world_create_entity(world);
        REQUIRE(!Transform_add(world, target, 0));
        REQUIRE(!Transform_add(world, unrelated, 0));
        REQUIRE(!ecs_world_record_add_pair(world, owner, targets, target, 0));
        REQUIRE(!ecs_world_apply_commands(world));
        Transform_get(world, target)->x = 2;
        Transform_get(world, unrelated)->x = 10;
        int status = source_run_system(worlds[i]->runtime->sources, "MoveTargets", 0, .1);
        if (status < 0) {
            fprintf(
                stderr,
                "MoveTargets execution: %s\n",
                source_error(worlds[i]->runtime->sources)
            );
        }
        REQUIRE(status >= 0);
        REQUIRE(Transform_get_const(world, target)->x == 3);
        REQUIRE(Transform_get_const(world, unrelated)->x == 10);
    }
    /* Reloading the same declaration reuses its world schema. A changed
     * cleanup policy must refuse publication and keep both generations. */
    REQUIRE(load(messages, sizeof messages) == 1);
    const authoring_sources *before[] = { preview_sources_applied(worlds[0]),
                                          preview_sources_applied(worlds[1]) };
    write_source("relationships", "Targets", "Targets :: relationship { exclusive: true, acyclic: true, on_delete_target: Delete }\n");
    REQUIRE(load(messages, sizeof messages) == -1);
    REQUIRE(strstr(messages, "Relationship Targets schema differs from the world"));
    for (int i = 0; i < 2; i++) {
        REQUIRE(preview_sources_applied(worlds[i]) == before[i]);
        REQUIRE(
            ecs_world_relationship(worlds[i]->runtime->ecs, targets)->on_delete_target == ECS_CLEANUP_REMOVE
        );
    }
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

TEST_CASE(scene_launch_prepares_what_overrides_name)
{
    /* A 2 x 1 sky in flat rows: the prototype names none, the scene does. */
    char path[512], error[256] = "";
    snprintf(path, sizeof path, "%s/skies", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/skies/override.hdr", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    fputs("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n", file);
    fwrite((unsigned char[8]){ 128, 128, 128, 129, 64, 64, 64, 129 }, 1, 8, file);
    REQUIRE(!fclose(file));
    REQUIRE(render_image_mount_content(root, error, sizeof error));
    prototype_registry prototypes = registry();
    test_prototype = prototype_load(
        "name \"Sky\"\ncomponents {\n    Environment\n}\n",
        &prototypes,
        error,
        sizeof error
    );
    REQUIRE(test_prototype);
    preview = prototype_preview_new(&prototypes);
    av_doc *doc = av_doc_read("Environment sky=\"content/skies/override.hdr\"\n", error, sizeof error);
    REQUIRE(doc);
    components = av_value_copy(av_doc_root(doc));
    av_doc_free(doc);
    const prototype_preview_scene_entity entity = { .prototype = test_prototype,
                                                    .parent = -1,
                                                    .components = components };
    REQUIRE(prototype_preview_launch_scene(preview, &entity, 1, error, sizeof error) == ECS_OK);
    REQUIRE(render_sky_find("content/skies/override.hdr"));
    return true;
}

void test_behavior_loader()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(sources_load_into_both_worlds_or_neither);
    RUN_TEST_CASE(required_relationship_queries_publish_before_scenes);
    RUN_TEST_CASE(scene_launch_prepares_what_overrides_name);
    RUN_TEST_CASE(failed_scene_launch_destroys_entities_before_their_strings);
    pu_clear_setup();
}
