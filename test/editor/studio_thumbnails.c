/* Asset thumbnails: mini-maps fit their square; in Studio over Neon Swarm
 * every kind of asset renders, with its cost reported; large previews past
 * their cap release their textures; the grid's keys move the selection; a
 * grid of a thousand assets stays windowed; and a changed file renders
 * again. */
#include "editor/studio/studio_thumbnails.h"

#include "assets/new_asset.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define FRAME_SECONDS (1.0 / 60.0)
#define ARENA "projects/arena2d"
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)

/* ---------- Mini-maps ---------- */

static bool box_inside(const studio_minimap_box *b, float low, float high)
{
    const float slack = 0.01f;
    return b->x >= low - slack && b->y >= low - slack && b->x + b->w <= high + slack &&
           b->y + b->h <= high + slack && b->w > 0 && b->h > 0;
}

void test_studio_minimap()
{
    char *text = text_file_read(ARENA "/content/behaviors/Movement.kdl");
    behavior_asset *asset = text ? behavior_asset_load(text) : NULL;
    free(text);
    assert(asset);
    const float side = 128, margin = side * 0.1f;
    studio_minimap map;
    studio_minimap_build(asset->graph, side, &map);
    bool inside = acount(map.boxes) > 0;
    float left = side, right = 0, top = side, bottom = 0;
    for (int i = 0; i < acount(map.boxes); i++) {
        const studio_minimap_box *b = &map.boxes[i];
        inside &= box_inside(b, margin, side - margin);
        left = fminf(left, b->x);
        right = fmaxf(right, b->x + b->w);
        top = fminf(top, b->y);
        bottom = fmaxf(bottom, b->y + b->h);
    }
    for (int i = 0; i < acount(map.wires); i++) {
        const studio_minimap_wire *w = &map.wires[i];
        inside &= w->x0 >= margin - 0.01f && w->x1 <= side - margin + 0.01f &&
                  w->y0 >= margin - 0.01f && w->y1 <= side - margin + 0.01f;
    }
    /* The longer side spans the inner square; both are centered. */
    float span = fmaxf(right - left, bottom - top);
    bool fitted = fabsf(span - (side - 2 * margin)) < 0.5f &&
                  fabsf((left + right) / 2 - side / 2) < 0.5f &&
                  fabsf((top + bottom) / 2 - side / 2) < 0.5f;
    test_check(inside && fitted && acount(map.wires) > 0, "a graph's mini-map fits its boxes and wires centered in the thumbnail's square, inside its margin");
    studio_minimap_free(&map);
    graph empty;
    graph_init(&empty);
    studio_minimap_build(&empty, side, &map);
    test_check(!acount(map.boxes) && !acount(map.wires), "an empty graph's mini-map is empty, so its tile keeps the kind's icon");
    graph_free(&empty);
    behavior_asset_free(asset);
}

/* ---------- Studio ---------- */

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    studio *studio;
    double time;
} thumbs_fixture;

static thumbs_fixture fixture_open(const char *root, const char *scene)
{
    thumbs_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new("Editing", behavior_asset_new("Editing"));
    assert(definition && game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("Editing"));
    f.runtime = engine_new(f.world, f.live);
    f.viewport = engine_viewport_new(640, 360);
    f.scene = app_scene_authoring_new(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        NULL,
        NULL,
        f.runtime
    );
    char content[PATH_MAX], path[PATH_MAX + 64];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(path, sizeof path, "%s/%s", content, scene);
    assert(app_scene_authoring_set_content_root(f.scene, content) == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, path) == 0);
    f.studio = studio_new(f.scene, f.viewport, root);
    studio *s = f.studio;
    studio_prefs defaults = studio_prefs_default();
    s->prefs.ui_scale = 1;
    s->prefs.assets_tile = STUDIO_ASSETS_TILE;
    s->prefs.workspace = defaults.workspace;
    memcpy(s->prefs.layouts, defaults.layouts, sizeof defaults.layouts);
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_ASSETS;
    s->applied = s->prefs;
    s->maximized = STUDIO_AREA_TOP;
    return f;
}

static void fixture_free(thumbs_fixture *f)
{
    studio_free(f->studio);
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    engine_viewport_free(f->viewport);
}

static double now()
{
    return (double)cf_get_ticks() / (double)cf_get_tick_frequency();
}

/* One frame; returns its milliseconds. */
static double frame(thumbs_fixture *f, double seconds)
{
    double start = now();
    f->time += seconds;
    cf_app_update(NULL);
    app_input_frame();
    editor_tick(f->graph, f->time);
    assert(app_scene_authoring_frame(f->scene, f->time) == FLOW_OK);
    studio_frame(f->studio, seconds);
    cf_app_draw_onto_screen(true);
    f->studio->prefs_changed_at = 0;
    return (now() - start) * 1000;
}

static int rendered(const studio_thumb_stats *stats)
{
    int total = 0;
    for (int k = 0; k < REFERENCE_ASSET_KIND_COUNT; k++) {
        total += stats->count[k];
    }
    return total;
}

/* Frames until no thumbnail renders for half a second; the slowest frame
   that rendered one in *worst. */
static int settle(thumbs_fixture *f, double *worst)
{
    int frames = 0, quiet = 0, before = rendered(&(studio_thumb_stats){ 0 });
    for (; frames < 1200 && quiet < 30; frames++) {
        studio_thumb_stats stats = studio_thumbs_stats(f->studio);
        before = rendered(&stats);
        double ms = frame(f, FRAME_SECONDS);
        stats = studio_thumbs_stats(f->studio);
        bool busy = rendered(&stats) != before;
        quiet = busy ? 0 : quiet + 1;
        if (busy && worst && ms > *worst) {
            *worst = ms;
        }
    }
    return frames;
}

static bool has_kind(const studio *s, int kind)
{
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    for (int i = 0; i < reference_index_asset_count(index); i++) {
        const reference_asset *a = reference_index_asset_at(index, i);
        if ((int)a->kind == kind && a->path && !a->unresolved) {
            return true;
        }
    }
    return false;
}

/* Each kind through the kind filter, with its render times. */
static void every_kind_renders(thumbs_fixture *f)
{
    studio *s = f->studio;
    bool all = true;
    double worst = 0;
    double idle[3];
    for (int i = 0; i < 3; i++) {
        idle[i] = frame(f, FRAME_SECONDS);
    }
    for (int kind = 0; kind < REFERENCE_ASSET_KIND_COUNT; kind++) {
        s->assets_kind = kind;
        s->ui_schedule.wake = true;
        settle(f, &worst);
        studio_thumb_stats stats = studio_thumbs_stats(s);
        bool present = has_kind(s, kind);
        all &= !present || stats.count[kind] > 0;
        if (present) {
            printf(
                "  thumbnails %-10s %3d rendered, %7.2f ms mean, %7.2f ms "
                "max\n",
                studio_asset_kind_label(kind),
                stats.count[kind],
                stats.count[kind] ? stats.total_ms[kind] / stats.count[kind] : 0,
                stats.max_ms[kind]
            );
        }
    }
    studio_thumb_stats stats = studio_thumbs_stats(s);
    printf(
        "  thumbnails: %d without a picture, %d bound, slowest streaming frame "
        "%.1f ms (idle %.1f ms)\n",
        stats.blank,
        stats.bound,
        worst,
        fmin(idle[0], fmin(idle[1], idle[2]))
    );
    test_check(all && stats.bound > 0, "every kind of asset in Neon Swarm renders a thumbnail through the Assets kind filter");
}

/* Large previews past their cap of four give their textures back. */
static void large_previews_evict(thumbs_fixture *f)
{
    studio *s = f->studio;
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    int asked = 0;
    studio_thumb_stats before = studio_thumbs_stats(s);
    for (int i = 0; i < reference_index_asset_count(index) && asked < 7; i++) {
        const reference_asset *a = reference_index_asset_at(index, i);
        if (a->kind != REFERENCE_ASSET_BEHAVIOR) {
            continue;
        }
        studio_thumb(s, i, STUDIO_THUMB_LARGE);
        frame(f, FRAME_SECONDS);
        asked++;
    }
    studio_thumb_stats after = studio_thumbs_stats(s);
    bool evicted = after.evicted - before.evicted == asked - 4;
    bool released = after.bound - before.bound == 4;
    if (!(evicted && released)) {
        printf(
            "  evicted %d, bound %d -> %d\n",
            after.evicted - before.evicted,
            before.bound,
            after.bound
        );
    }
    test_check(asked == 7 && evicted && released, "large previews past their cap evict the least recent and release its texture");
}

/* The grid's keys: End selects the last asset, Left the one before, Up the
   one a row above, and Home the first. */
static void grid_keys_move_selection(thumbs_fixture *f)
{
    studio *s = f->studio;
    s->assets_kind = -1;
    s->assets_filter[0] = '\0';
    s->ui_schedule.wake = true;
    frame(f, FRAME_SECONDS);
    frame(f, FRAME_SECONDS);
    const reference_index *index = app_scene_authoring_asset_index(s->scene);
    char last[128];
    assert(studio_assets_key(s, SDLK_END, SDL_KMOD_NONE));
    snprintf(last, sizeof last, "%s", s->assets_selected);
    assert(studio_assets_key(s, SDLK_LEFT, SDL_KMOD_NONE));
    bool before = strcmp(last, s->assets_selected) != 0;
    assert(studio_assets_key(s, SDLK_HOME, SDL_KMOD_NONE));
    int first = reference_index_find_name(index, (reference_asset_kind)s->assets_selected_kind, s->assets_selected);
    bool scene = first >= 0 && reference_index_asset_at(index, first)->kind ==
                                   REFERENCE_ASSET_SCENE;
    test_check(last[0] && before && scene && !strcmp(last, "PauseMenu"), "the grid's End, Left and Home keys move the selection through the assets in their order");
}

static int grid_tiles(studio *s)
{
    rml_context *context = rml_backend_context(s->backend);
    rml_element *grid = NULL;
    for (int i = 0; !grid && i < rml_context_document_count(context); i++) {
        grid = rml_element_query_selector(rml_context_document(context, i), ".assets-grid .vlist-content");
    }
    int tiles = 0;
    for (int r = 0; grid && r < rml_element_child_count(grid); r++) {
        tiles += rml_element_child_count(rml_element_child(grid, r));
    }
    return tiles;
}

/* A grid of more than a thousand assets makes only the tiles in view. */
static void large_grid_stays_windowed(thumbs_fixture *f)
{
    studio *s = f->studio;
    for (int i = 0; i < 1200; i++) {
        char name[32];
        snprintf(name, sizeof name, "QaGrid%04d", i);
        reference_index_add_asset(f->scene->asset_index, REFERENCE_ASSET_BEHAVIOR, name, NULL, name);
    }
    /* Inserted directly: start the cached model afresh, as a rebuild would. */
    studio_assets_free(s);
    s->assets_kind = REFERENCE_ASSET_BEHAVIOR;
    s->ui_schedule.wake = true;
    frame(f, FRAME_SECONDS);
    double ms = 0;
    for (int i = 0; i < 4; i++) {
        s->ui_schedule.wake = true;
        ms = fmax(ms, frame(f, FRAME_SECONDS));
    }
    int tiles = grid_tiles(s);
    printf("  a grid of %d behaviors makes %d tiles; slowest frame %.1f ms\n", 1200 + 20, tiles, ms);
    test_check(tiles > 0 && tiles < 200, "a grid of more than a thousand assets makes only the tiles in view and overscan");
}

static void arena_thumbnails()
{
    static char root[PATH_MAX];
    assert(realpath(ARENA, root));
    thumbs_fixture f = fixture_open(root, "scenes/Sandbox.kdl");
    frame(&f, FRAME_SECONDS);
    every_kind_renders(&f);
    large_previews_evict(&f);
    grid_keys_move_selection(&f);
    large_grid_stays_windowed(&f);
    fixture_free(&f);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

static char *disk_text(new_asset_kind kind, const char *name)
{
    char error[128];
    char *json = new_asset_text(kind, name);
    char *disk = asset_text_to_disk(json, error, sizeof error);
    free(json);
    return disk;
}

/* A behavior whose file changes renders again; its old picture shows
   meanwhile. */
static void changed_file_renders_again()
{
    char root[64] = "/tmp/chibi-thumbs-XXXXXX", path[256];
    assert(mkdtemp(root));
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    write_text(path, "version " STRINGIFY(CHIBI_PROJECT_VERSION) "\nname \"Thumbs\"\ncontent \"content\"\nstart_scene \"scenes/Main.kdl\"\n");
    const char *folders[] = { "content", "content/scenes", "content/behaviors" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        assert(mkdir(path, 0755) == 0);
    }
    char *scene = disk_text(NEW_ASSET_SCENE, "Main"),
         *behavior = disk_text(NEW_ASSET_BEHAVIOR, "Spin");
    snprintf(path, sizeof path, "%s/content/scenes/Main.kdl", root);
    write_text(path, scene);
    char behavior_path[256];
    snprintf(behavior_path, sizeof behavior_path, "%s/content/behaviors/Spin.kdl", root);
    write_text(behavior_path, behavior);
    thumbs_fixture f = fixture_open(root, "scenes/Main.kdl");
    studio *s = f.studio;
    s->assets_kind = REFERENCE_ASSET_BEHAVIOR;
    settle(&f, NULL);
    int first = studio_thumbs_stats(s).count[REFERENCE_ASSET_BEHAVIOR];
    /* A later modification time, as an editor's save leaves. */
    struct timeval times[2];
    gettimeofday(&times[0], NULL);
    times[0].tv_sec += 5;
    times[1] = times[0];
    assert(utimes(behavior_path, times) == 0);
    frame(&f, 1.5);
    settle(&f, NULL);
    int second = studio_thumbs_stats(s).count[REFERENCE_ASSET_BEHAVIOR];
    test_check(first == 1 && second == 2, "a thumbnail renders again once its asset's file changes");
    fixture_free(&f);
    remove(behavior_path);
    remove(path);
    const char *paths[] = { "content/behaviors", "content/scenes", "content", CHIBI_PROJECT_MANIFEST };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", root, paths[i]);
        remove(path);
    }
    rmdir(root);
    free(scene);
    free(behavior);
}

int studio_thumbnails_run(const char *program)
{
    CF_Result result = cf_make_app(
        "Studio thumbnails",
        0,
        0,
        0,
        1280,
        800,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "Studio thumbnails app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    arena_thumbnails();
    changed_file_renders_again();
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
