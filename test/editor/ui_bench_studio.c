/* The Studio cases of the S0 UI bench (ui_bench.h), native in a hidden
 * window: Studio's documents over the Sandbox at 1440x900, its
 * Properties showing the Player against the authored spike, and whether a
 * popover escapes an area's clipping. */
#include "editor/studio/studio_internal.h"
#include "editor/studio/studio_thumbnails.h"
#include "editor/ui/kit.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "render/scene_renderer.h"
#include "test/editor/ui_bench.h"
#include "test/suite_internal.h"
#include "ui/rml.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA "projects/arena2d"
#define WIDTH 1440
#define HEIGHT 900
#define FRAME (1.0 / 60.0)
#define RUNS 31
#define WARMUP 3
#define ARMS 2

typedef struct studio_bench
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    engine_viewport *viewport;
    app_scene_authoring *scene;
    studio *studio;
    double time;
} studio_bench;

/* Studio over the Sandbox, as studio_documents.c's fixture opens one. */
static studio_bench bench_open()
{
    studio_bench b = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        int registered = ecs_world_register_component(b.world, GAME_REGISTRY_COMPONENTS[i], NULL);
        assert(registered == ECS_OK);
    }
    b.live = game_live_world_new(b.world);
    game_live_definition *definition = game_live_definition_new("Editing", behavior_asset_new("Editing"));
    int added = game_live_add_definition(b.live, definition);
    assert(definition && added == 0);
    b.graph = editor_new(behavior_asset_new("Editing"));
    b.runtime = engine_new(b.world, b.live);
    b.viewport = engine_viewport_new(640, 360);
    b.scene = app_scene_authoring_new(
        b.world,
        b.live,
        definition,
        editor_behavior_asset(b.graph),
        b.graph,
        NULL,
        NULL,
        b.runtime
    );
    int loaded = app_scene_authoring_set_content_root(b.scene, ARENA "/content") |
                 app_scene_authoring_load_content(b.scene) |
                 app_scene_authoring_load(b.scene, ARENA "/content/scenes/Sandbox.kdl") |
                 app_scene_authoring_open_behavior(b.scene, "Movement");
    assert(loaded == 0);
    b.studio = studio_new(b.scene, b.viewport, ARENA);
    return b;
}

static void bench_close(studio_bench *b)
{
    studio_free(b->studio);
    app_scene_authoring_free(b->scene);
    engine_free(b->runtime);
    game_live_world_free(b->live);
    editor_free(b->graph);
    ecs_world_free(b->world);
    engine_viewport_free(b->viewport);
}

/* RmlUi warnings one frame may log before the bench stops: a defect that
   repeats them every frame floods the log by gigabytes a minute. */
#define WARNING_FLOOD 100

static void stop_on_flood(int before)
{
    int logged = rml_backend_warning_count() - before;
    if (logged > WARNING_FLOOD) {
        fprintf(stderr, "ui bench: RmlUi logged %d warnings in one frame; stopping\n", logged);
        exit(1);
    }
}

static void frames(studio_bench *b, int count)
{
    for (int i = 0; i < count; i++) {
        int warnings = rml_backend_warning_count();
        b->time += FRAME;
        cf_app_update(NULL);
        app_input_frame();
        editor_tick(b->graph, b->time);
        bool ran = app_scene_authoring_frame(b->scene, b->time) == FLOW_OK;
        assert(ran);
        studio_frame(b->studio, FRAME);
        cf_app_draw_onto_screen(true);
        b->studio->prefs_changed_at = 0;
        stop_on_flood(warnings);
    }
}

static rml_element *studio_query(studio *s, const char *selector)
{
    rml_context *context = rml_backend_context(s->backend);
    rml_element *found = NULL;
    for (int i = 0; !found && i < rml_context_document_count(context); i++) {
        found = rml_element_query_selector(rml_context_document(context, i), selector);
    }
    return found;
}

static int studio_count(studio *s, const char *selector)
{
    rml_context *context = rml_backend_context(s->backend);
    int count = 0;
    for (int i = 0; i < rml_context_document_count(context); i++) {
        count += rml_element_query_selector_all(rml_context_document(context, i), selector, NULL, 0);
    }
    return count;
}

/* ---------- Native UI work ---------- */

typedef enum
{
    PHASE_SYNC,   /* authored panels' data (studio_ui_frame) */
    PHASE_EMIT,   /* the kit re-emits the editor */
    PHASE_LAYOUT, /* RmlUi's update: data views, styles, layout */
    PHASE_CAPTURE,
    PHASE_COUNT,
} phase;

/* One frame's UI work, as studio_host.c's run does it; returns the total
 * and fills each phase's time. */
static double native_ui(studio *s, double *phases)
{
    int warnings = rml_backend_warning_count();
    double t[PHASE_COUNT + 1];
    t[0] = bench_now();
    studio_ui_frame(s);
    t[1] = bench_now();
    kit_begin(s->kit, &s->in);
    if (s->canvas) {
        s->canvas->frame = NULL;
    }
    studio_emit(s);
    studio_inspect_emit(s);
    kit_end(s->kit);
    t[2] = bench_now();
    rml_backend_update(s->backend, 0);
    t[3] = bench_now();
    rml_backend_render(s->backend);
    t[4] = bench_now();
    for (int p = 0; p < PHASE_COUNT; p++) {
        phases[p] = t[p + 1] - t[p];
    }
    stop_on_flood(warnings);
    return t[PHASE_COUNT] - t[0];
}

/* Layouts, and what the authored panels' data views read. */
static bench_counts studio_mark(studio *s)
{
    rml_data_reads reads = studio_ui_data_reads(s);
    return (bench_counts){ .values = reads.values,
                           .lookups = reads.lookups,
                           .layouts = bench_layouts() };
}

static bench_counts studio_counts(studio *s, bench_counts before)
{
    bench_counts now = studio_mark(s);
    return (bench_counts){ .values = now.values - before.values,
                           .lookups = now.lookups - before.lookups,
                           .layouts = now.layouts - before.layouts,
                           .elements = studio_count(s, "*") };
}

static void phase_row(const char *name, double phases[PHASE_COUNT][RUNS])
{
    bench_stat stat[PHASE_COUNT];
    for (int p = 0; p < PHASE_COUNT; p++) {
        stat[p] = bench_stat_of(phases[p], RUNS);
    }
    printf(
        "|  | %s: sync %.3f, emit %.3f, update %.3f, capture %.3f | | | | | | "
        "|\n",
        name,
        stat[PHASE_SYNC].median,
        stat[PHASE_EMIT].median,
        stat[PHASE_LAYOUT].median,
        stat[PHASE_CAPTURE].median
    );
}

/* ---------- 1: full layout ---------- */

static void probe_documents(studio *s)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int i = 0; i < rml_context_document_count(context); i++) {
        bench_probe(rml_context_document(context, i));
    }
}

/* Lays the whole document out by resizing it a pixel, as a window resize
 * or any layout change does. */
static void full_layout(studio_bench *b, bool comfortable)
{
    studio *s = b->studio;
    s->prefs.comfortable = comfortable;
    frames(b, 4);
    float scale = cf_app_get_pixel_scale(), ratio = studio_pixel_ratio(s);
    int width = (int)(WIDTH * scale), height = (int)(HEIGHT * scale);
    double ms[RUNS];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        rml_backend_resize_pixels(s->backend, width + (run & 1), height, ratio);
        bench_counts before = studio_mark(s);
        double t = bench_now();
        rml_backend_update(s->backend, 0);
        t = bench_now() - t;
        counts = studio_counts(s, before);
        if (run >= 0) {
            ms[run] = t;
        }
    }
    rml_backend_resize_pixels(s->backend, width, height, ratio);
    frames(b, 2);
    char name[128];
    snprintf(
        name,
        sizeof name,
        "full layout of Studio, %s, %d documents",
        comfortable ? "comfortable" : "compact",
        rml_context_document_count(rml_backend_context(s->backend))
    );
    bench_row("1", name, bench_stat_of(ms, RUNS), counts);
}

/* ---------- 13: a text change inside one area ---------- */

/* Rewrites one Outliner row's name, as a rename does, and updates: RmlUi
 * lays out the document that holds the row. Counts the documents laid out
 * and the elements in the row's document. */
static void area_text_change(studio_bench *b)
{
    studio *s = b->studio;
    frames(b, 2);
    rml_element *name = studio_query(s, "#outliner-list .tree-row .name");
    assert(name);
    char text[256];
    size_t length = rml_element_inner_rml(name, text, sizeof text);
    assert(length < sizeof text);
    double ms[RUNS];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        rml_element_set_inner_rml(name, run & 1 ? "Renamed" : "Renamed again");
        bench_counts before = studio_mark(s);
        double t = bench_now();
        rml_backend_update(s->backend, 0);
        t = bench_now() - t;
        counts = (bench_counts){
            .layouts = bench_layouts() - before.layouts,
            .elements = rml_element_query_selector_all(rml_element_owner_document(name), "*", NULL, 0)
        };
        if (run >= 0) {
            ms[run] = t;
        }
    }
    rml_element_set_inner_rml(name, text);
    frames(b, 2);
    bench_row(
        "13",
        "a text change in the Outliner: documents laid out, elements in its "
        "document",
        bench_stat_of(ms, RUNS),
        counts
    );
}

/* ---------- 5: Properties, native and authored ---------- */

static int actor_named(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (scene->actors[i].name && !strcmp(scene->actors[i].name, name)) {
            return i;
        }
    }
    return -1;
}

/* Another object with components of its own to show: a Swarmer when the
 * scene places one. */
static int other_actor(const app_scene_authoring *scene, int player)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        const char *prototype = scene->actors[i].prototype_name;
        if (i != player && prototype && !strcmp(prototype, "Swarmer")) {
            return i;
        }
    }
    return player ? 0 : 1;
}

/* Opens every component panel, as a user expanding them does. */
static void open_panels(studio_bench *b)
{
    rml_element *panels[64];
    rml_element *area = studio_query(b->studio, "[data-ui=Properties]");
    int count = rml_element_query_selector_all(area, "section.panel", panels, 64);
    for (int i = 0; i < count; i++) {
        if (!rml_element_has_attribute(panels[i], "data-open")) {
            rml_element_click(rml_element_query_selector(panels[i], ".panel-head"));
            frames(b, 1);
        }
    }
    frames(b, 2);
}

typedef struct properties_case
{
    studio_bench *b;
    bench_spike *spike;
    int player, other;
} properties_case;

static void select_actor(properties_case *c, int actor, bool authored)
{
    app_scene_authoring *scene = c->b->scene;
    if (authored) {
        bench_spike_show(
            c->spike,
            scene->world,
            scene->actors[actor].entity,
            scene->actors[actor].name,
            &c->b->studio->presentation.hints
        );
        return;
    }
    select_instance(scene, actor);
}

/* The spike's UI work after its data changed: RmlUi's update, then the
 * capture; fills those phases. */
static void authored_ui(bench_spike *spike, double *phases)
{
    double t[3] = { bench_now() };
    rml_backend_update(bench_spike_backend(spike), 0);
    t[1] = bench_now();
    rml_backend_render(bench_spike_backend(spike));
    t[2] = bench_now();
    phases[PHASE_EMIT] = 0;
    phases[PHASE_LAYOUT] = t[1] - t[0];
    phases[PHASE_CAPTURE] = t[2] - t[1];
}

/* Arm 0's (native) or arm 1's (authored) UI work for `step`: 0 selects
 * `actor`, 1 and 2 show values changed. Returns its time and fills its
 * phases; selecting and the spike's refresh count as sync. */
static double properties_ui(properties_case *c, int step, int arm, int actor, double *phases)
{
    double start = bench_now();
    if (step == 0) {
        select_actor(c, actor, arm);
    } else if (arm) {
        bench_spike_refresh(c->spike);
    }
    double synced = bench_now() - start;
    if (arm) {
        authored_ui(c->spike, phases);
        phases[PHASE_SYNC] = synced;
    } else {
        native_ui(c->b->studio, phases);
        phases[PHASE_SYNC] += synced;
    }
    return bench_now() - start;
}

/* Step 1 edits one shown field and step 2 moves every float, as Play does. */
static void change_player(properties_case *c, int step, int run)
{
    app_scene_authoring *scene = c->b->scene;
    ecs_entity player = scene->actors[c->player].entity;
    float sign = (run & 1) ? 1 : -1;
    if (step == 1) {
        bench_edit_float(scene->world, player, "GameAppearance", "width", sign);
    } else if (step == 2) {
        bench_move_floats(scene->world, player, sign * 0.25f);
    }
}

/* Times arm 0 (native) against arm 1 (authored) for `step`, each in the
 * same run after the same change. */
static void properties_step(properties_case *c, int step, const char *name)
{
    double ms[ARMS][RUNS], phases[ARMS][PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts[ARMS];
    for (int run = -WARMUP; run < RUNS; run++) {
        for (int arm = 0; arm < ARMS; arm++) {
            change_player(c, step, run);
            cf_app_update(NULL);
            bench_counts before = arm ? bench_spike_counts(c->spike)
                                      : studio_mark(c->b->studio);
            double t = properties_ui(c, step, arm, run & 1 ? c->other : c->player, phase);
            cf_app_draw_onto_screen(true);
            bench_counts after = arm ? bench_spike_counts(c->spike)
                                     : studio_counts(c->b->studio, before);
            counts[arm] = arm ? (bench_counts){ after.values - before.values,
                                                after.lookups - before.lookups,
                                                after.layouts - before.layouts,
                                                after.elements }
                              : after;
            frames(c->b, 1);
            if (run < 0) {
                continue;
            }
            ms[arm][run] = t;
            for (int p = 0; p < PHASE_COUNT; p++) {
                phases[arm][p][run] = phase[p];
            }
        }
    }
    char label[128];
    bench_stat native = bench_stat_of(ms[0], RUNS),
               authored = bench_stat_of(ms[1], RUNS);
    snprintf(label, sizeof label, "native kit: %s", name);
    bench_row("5", label, native, counts[0]);
    phase_row("native phases", phases[0]);
    snprintf(label, sizeof label, "authored spike: %s", name);
    bench_row("5", label, authored, counts[1]);
    phase_row("authored phases", phases[1]);
    snprintf(label, sizeof label, "authored over native: %s", name);
    bench_ratio("5", label, native, authored);
    /* The Player stays shown. */
    select_actor(c, c->player, false);
    select_actor(c, c->player, true);
    frames(c->b, 2);
}

static void properties(studio_bench *b)
{
    studio *s = b->studio;
    app_scene_authoring *scene = b->scene;
    properties_case c = { .b = b, .player = actor_named(scene, "Player") };
    assert(c.player >= 0);
    c.other = other_actor(scene, c.player);
    select_instance(scene, c.player);
    studio_show_editor(s, STUDIO_EDITOR_PROPERTIES);
    s->tab = STUDIO_TAB_OBJECT;
    frames(b, 4);
    open_panels(b);
    rml_element *area = studio_query(s, "[data-ui=Properties]");
    assert(area);
    rml_vec2 size = rml_element_box_size(area, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    c.spike = bench_spike_new((int)(size.x / ratio), (int)(size.y / ratio));
    rml_backend_resize(bench_spike_backend(c.spike), (int)(size.x / ratio), (int)(size.y / ratio), ratio);
    select_actor(&c, c.player, true);
    /* Captures retain unchanged paint, as Studio's do. */
    cf_app_update(NULL);
    rml_backend_frame_retained(bench_spike_backend(c.spike), 0, true);
    cf_app_draw_onto_screen(true);
    printf(
        "|  | Properties area %.0fx%.0f dp; native shows %d rows, the spike "
        "%d; the other "
        "object is %s | | | | | | |\n",
        size.x / ratio,
        size.y / ratio,
        studio_count(s, "[data-ui=Properties] .prop"),
        bench_spike_rows(c.spike),
        scene->actors[c.other].name
    );
    double idle[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        cf_app_draw_onto_screen(true);
        counts = studio_counts(s, before);
        frames(b, 1);
        if (run >= 0) {
            idle[run] = t;
            for (int p = 0; p < PHASE_COUNT; p++) {
                phases[p][run] = phase[p];
            }
        }
    }
    bench_row("5", "native kit: a UI frame with nothing changed", bench_stat_of(idle, RUNS), counts);
    phase_row("native phases", phases);
    properties_step(&c, 0, "select the other object or the Player");
    properties_step(&c, 1, "edit one field of the Player");
    properties_step(&c, 2, "Play refresh, every float of the Player changed");
    bench_spike_free(c.spike);
}

/* ---------- 14: Assets over 500 assets ---------- */

#define ASSETS_ADDED 480
/* Where the scroll cases scroll from, in lines or rows, past the overscan
   that keeps the window still for small scrolls near the top. */
#define ASSETS_SCROLLED 10
/* Opening takes this many UI frames to show every tile in view. */
#define OPEN_FRAMES 3

typedef enum
{
    ASSETS_OPEN,   /* show Assets in an area that showed the Outliner */
    ASSETS_SCROLL, /* scroll the grid a line */
    ASSETS_FILTER, /* type in the filter */
    ASSETS_SELECT, /* select another tile */
    ASSETS_IDLE,   /* a UI frame with nothing changed */
    ASSETS_LIST,   /* scroll the list a row */
} assets_step;

/* The Assets view's scroller. */
static rml_element *assets_scroller(studio *s)
{
    return studio_query(s, "#assets-list");
}

/* Scrolls the view to `lines` of `height` dp, and half a line more for an
   odd run. */
static void assets_scroll(studio *s, float height, int run)
{
    float lines = ASSETS_SCROLLED + (run & 1 ? 1.0f : 0.0f);
    rml_element_set_scroll_top(assets_scroller(s), lines * height * studio_pixel_ratio(s));
}

static void assets_change(studio_bench *b, assets_step step, int run)
{
    studio *s = b->studio;
    switch (step) {
        case ASSETS_OPEN:
            studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_OUTLINER;
            frames(b, 2);
            studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_ASSETS;
            break;
        case ASSETS_SCROLL:
            assets_scroll(s, (float)s->prefs.assets_tile + 2 * 4 + 18 + 4, run);
            break;
        case ASSETS_FILTER:
            snprintf(s->assets_filter, sizeof s->assets_filter, "%s", run & 1 ? "QaBench1" : "QaBench");
            break;
        case ASSETS_SELECT:
            s->assets_selected_kind = REFERENCE_ASSET_BEHAVIOR;
            snprintf(s->assets_selected, sizeof s->assets_selected, "%s", run & 1 ? "QaBench001" : "QaBench003");
            break;
        case ASSETS_LIST: assets_scroll(s, 26, run); break;
        case ASSETS_IDLE: break;
    }
}

/* Times one step's UI work: three UI frames to open (a mount, then its
   window once the List has a width), one otherwise; counts the views
   evaluated, the documents laid out and the elements in the area's
   document. */
static void assets_case(studio_bench *b, assets_step step, const char *name)
{
    studio *s = b->studio;
    double ms[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT], more[PHASE_COUNT];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        assets_change(b, step, run);
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        for (int f = 1; step == ASSETS_OPEN && f < OPEN_FRAMES; f++) {
            t += native_ui(s, more);
            for (int p = 0; p < PHASE_COUNT; p++) {
                phase[p] += more[p];
            }
        }
        cf_app_draw_onto_screen(true);
        counts = studio_counts(s, before);
        rml_element *document = rml_element_owner_document(assets_scroller(s));
        counts.elements = rml_element_query_selector_all(document, "*", NULL, 0);
        frames(b, 1);
        if (run < 0) {
            continue;
        }
        ms[run] = t;
        for (int p = 0; p < PHASE_COUNT; p++) {
            phases[p][run] = phase[p];
        }
    }
    bench_row("14", name, bench_stat_of(ms, RUNS), counts);
    phase_row("phases", phases);
}

/* Assets maximized over the Sandbox's assets and 480 more, which have no
   files, so their tiles keep their kinds' icons: open, scroll and filter
   the grid, an idle frame, and a scroll of the list. */
static void assets(studio_bench *b)
{
    studio *s = b->studio;
    for (int i = 0; i < ASSETS_ADDED; i++) {
        char name[32];
        snprintf(name, sizeof name, "QaBench%03d", i);
        reference_asset_kind kind = i & 1 ? REFERENCE_ASSET_BEHAVIOR
                                          : REFERENCE_ASSET_MATERIAL;
        reference_index_add_asset(b->scene->asset_index, kind, name, NULL, name);
    }
    /* Inserted directly: start the cached model afresh, as a rebuild would. */
    studio_assets_free(s);
    s->maximized = STUDIO_AREA_TOP;
    studio_active_layout(s)->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_ASSETS;
    s->prefs.assets_tile = STUDIO_ASSETS_TILE;
    frames(b, 4);
    for (int i = 0; i < 600 && studio_thumbs_pending(s); i++) {
        frames(b, 1);
    }
    /* Maximized, the area is a document of its own. */
    bench_probe(rml_element_owner_document(assets_scroller(s)));
    printf(
        "|  | Assets: %d assets, %d tiles shown | | | | | | |\n",
        reference_index_asset_count(b->scene->asset_index),
        studio_count(s, "#assets-list [data-ui=Tile]")
    );
    assets_case(b, ASSETS_OPEN, "Assets grid: open, 3 UI frames");
    assets_case(b, ASSETS_SCROLL, "Assets grid: scroll a line");
    assets_case(b, ASSETS_FILTER, "Assets grid: filter keystroke");
    /* The first QaBench behaviors in view. */
    rml_element_set_scroll_top(assets_scroller(s), 0);
    frames(b, 2);
    assets_case(b, ASSETS_SELECT, "Assets grid: select another tile");
    assets_case(b, ASSETS_IDLE, "Assets grid: UI frame, nothing changed");
    s->prefs.assets_tile = STUDIO_ASSETS_LIST;
    frames(b, 4);
    assets_case(b, ASSETS_LIST, "Assets list: scroll a row");
    s->prefs.assets_tile = STUDIO_ASSETS_TILE;
    s->maximized = -1;
    frames(b, 2);
}

/* ---------- 11: Output following a log ---------- */

/* Gives documents opened since the bench began their layout probes. */
static void probe_new_documents(studio *s)
{
    rml_context *context = rml_backend_context(s->backend);
    for (int i = 0; i < rml_context_document_count(context); i++) {
        rml_element *document = rml_context_document(context, i);
        if (!rml_element_query_selector(document, "bench-probe")) {
            bench_probe(document);
        }
    }
}

/* Prints `count` lines, as a behavior printing every frame does. */
static void print_lines(editor *graph, int count)
{
    static int printed;
    for (int i = 0; i < count; i++, printed++) {
        char text[192];
        snprintf(
            text,
            sizeof text,
            "warning: Player hit \"Wall %d\" at (%d.5, -%d.25) for %d damage "
            "in content/behaviors/Weapon.kdl",
            printed % 97,
            printed,
            printed % 13,
            printed % 9
        );
        editor_log_print(graph, "Weapon", text);
    }
}

/* One UI frame after `lines` new lines with Output's Log following them:
   its document's layouts and elements, and the views Studio's authored
   panels evaluated. */
static void log_step(studio_bench *b, int lines, bool full, rml_element *document)
{
    studio *s = b->studio;
    editor_clear_log(b->graph);
    print_lines(b->graph, full ? ED_LOG_MAX : 200);
    frames(b, 2);
    double ms[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        print_lines(b->graph, lines);
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        cf_app_draw_onto_screen(true);
        counts = studio_counts(s, before);
        counts.elements = rml_element_query_selector_all(document, "*", NULL, 0);
        frames(b, 1);
        if (run < 0) {
            continue;
        }
        ms[run] = t;
        for (int p = 0; p < PHASE_COUNT; p++) {
            phases[p][run] = phase[p];
        }
    }
    char name[128];
    snprintf(
        name,
        sizeof name,
        "Output's Log following %d new line%s a frame, the log %s",
        lines,
        lines == 1 ? "" : "s",
        full ? "full (the oldest drop)" : "growing"
    );
    bench_row("11", name, bench_stat_of(ms, RUNS), counts);
    phase_row("phases", phases);
}

static void output_log(studio_bench *b)
{
    studio *s = b->studio;
    studio_show_editor(s, STUDIO_EDITOR_OUTPUT);
    s->output_view = 0;
    frames(b, 4);
    probe_new_documents(s);
    rml_element *list = studio_query(s, ".output-list");
    assert(list);
    rml_element *document = rml_element_owner_document(list);
    log_step(b, 1, false, document);
    log_step(b, 8, false, document);
    log_step(b, 1, true, document);
    log_step(b, 8, true, document);
    editor_clear_log(b->graph);
    frames(b, 2);
}

/* ---------- 12: the Outliner over ten thousand objects ---------- */

#define OUTLINER_OBJECTS 10000
#define SCROLL_ROWS 3
/* Rows a document query collects at most; a window holds fewer. */
#define MAX_ROWS 256

/* The Outliner's scroller, the authored List (workstream P2). Before it,
   the kit's list was ".vlist.outliner". */
static rml_element *outliner_list(studio *s)
{
    return studio_query(s, "#outliner-list");
}

/* The rows shown (templates left out) that lack the mark, which a step
   instanced; with `mark`, marks them all. `shown` counts them. */
static int fresh_rows(rml_element *list, bool mark, int *shown)
{
    rml_element *rows[MAX_ROWS];
    int count = rml_element_query_selector_all(list, ".tree-row", rows, MAX_ROWS);
    assert(count < MAX_ROWS);
    int fresh = 0;
    *shown = 0;
    for (int i = 0; i < count; i++) {
        if (!rml_element_is_visible(rows[i], false)) {
            continue;
        }
        ++*shown;
        fresh += !rml_element_has_attribute(rows[i], "data-bench");
        if (mark) {
            rml_element_set_attribute(rows[i], "data-bench", "");
        }
    }
    return fresh;
}

/* What a run did: data views read, layouts, and elements in the
   Outliner's document. */
static bench_counts outliner_counts(studio *s, rml_element *list, bench_counts before)
{
    bench_counts now = studio_mark(s);
    return (bench_counts){
        .values = now.values - before.values,
        .lookups = now.lookups - before.lookups,
        .layouts = now.layouts - before.layouts,
        .elements = rml_element_query_selector_all(rml_element_owner_document(list), "*", NULL, 0)
    };
}

/* Scrolls the Outliner three rows down per run, from the middle of ten
   thousand objects, and times the UI frame that follows: rows instanced,
   layouts and elements in the Outliner's document; then selects one of two
   objects in view in turn. */
static void outliner_scroll(studio_bench *b)
{
    studio *s = b->studio;
    for (int i = 0; i < OUTLINER_OBJECTS; i++) {
        char name[32];
        snprintf(name, sizeof name, "Marker %d", i);
        int added = add_actor(b->scene, name, "DriftingMarker");
        assert(added == 0);
    }
    select_instance(b->scene, 0);
    studio_show_editor(s, STUDIO_EDITOR_OUTLINER);
    frames(b, 6);
    rml_element *list = outliner_list(s);
    assert(list);
    probe_documents(s);
    char value[32] = "";
    rml_element_property(list, "--row", value, sizeof value);
    float row = (float)atof(value) * studio_pixel_ratio(s);
    assert(row > 0);
    float top = row * OUTLINER_OBJECTS / 2;
    rml_element_set_scroll_top(list, top);
    frames(b, 4);
    double ms[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts = { 0 };
    int instanced = 0, shown = 0;
    for (int run = -WARMUP; run < RUNS; run++) {
        fresh_rows(list, true, &shown);
        top += SCROLL_ROWS * row;
        rml_element_set_scroll_top(list, top);
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        cf_app_draw_onto_screen(true);
        counts = outliner_counts(s, list, before);
        instanced = fresh_rows(list, false, &shown);
        if (run >= 0) {
            ms[run] = t;
            for (int p = 0; p < PHASE_COUNT; p++) {
                phases[p][run] = phase[p];
            }
        }
    }
    char name[160];
    snprintf(
        name,
        sizeof name,
        "Outliner over %d objects, %.0f dp tall, a %d-row scroll step: %d rows "
        "shown, %d instanced",
        acount(b->scene->actors),
        rml_element_client_height(list) / studio_pixel_ratio(s),
        SCROLL_ROWS,
        shown,
        instanced
    );
    bench_row("12", name, bench_stat_of(ms, RUNS), counts);
    phase_row("its phases", phases);
    /* Two objects in view, selected in turn, as clicks on their rows. */
    int first = (int)(top / row);
    for (int run = -WARMUP; run < RUNS; run++) {
        select_instance(b->scene, first + 4 + (run & 1));
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        cf_app_draw_onto_screen(true);
        counts = outliner_counts(s, list, before);
        if (run >= 0) {
            ms[run] = t;
            for (int p = 0; p < PHASE_COUNT; p++) {
                phases[p][run] = phase[p];
            }
        }
    }
    bench_row(
        "12",
        "Outliner over 10k objects: select another object in view",
        bench_stat_of(ms, RUNS),
        counts
    );
    phase_row("its phases", phases);
}

/* ---------- 15: the Scene's overlays while its camera moves ---------- */

#define PAN_STEP 8.0f   /* world units a run pans, back and forth */
#define ZOOM_STEP 1.15f /* about a wheel notch */

/* Moves the Scene's camera as a held drag or a wheel notch does, `pan`
   across or by `zoom`, draws it, as the frame of the move does after its
   UI, and times the UI frame that follows, which shows the drawn camera. */
static double scene_step(studio_bench *b, float pan, float zoom, double *phases)
{
    engine_viewport_view view;
    engine_viewport_get_view(b->viewport, &view);
    view.center_x += pan;
    view.center_y += pan;
    view.width *= zoom;
    view.height *= zoom;
    engine_viewport_set_view(b->viewport, &view);
    frames(b, 1);
    cf_app_update(NULL);
    double t = native_ui(b->studio, phases);
    cf_app_draw_onto_screen(true);
    return t;
}

/* Layouts of every document, data views and elements in the Scene's. */
static void scene_case(studio_bench *b, float pan, float zoom, const char *name)
{
    studio *s = b->studio;
    rml_element *viewport = studio_query(s, ".viewport");
    assert(viewport);
    rml_element *document = rml_element_owner_document(viewport);
    double ms[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        bool back = run & 1;
        bench_counts before = studio_mark(s);
        double t = scene_step(b, back ? -pan : pan, back ? 1 / zoom : zoom, phase);
        counts = studio_counts(s, before);
        counts.elements = rml_element_query_selector_all(document, "*", NULL, 0);
        if (run >= 0) {
            ms[run] = t;
            for (int p = 0; p < PHASE_COUNT; p++) {
                phases[p][run] = phase[p];
            }
        }
    }
    char row[160];
    snprintf(row, sizeof row, "%s, %d camera labels", name, studio_count(s, ".viewport .camera-label"));
    bench_row("15", row, bench_stat_of(ms, RUNS), counts);
    phase_row("its phases", phases);
}

/* A pan moves each camera frame's label; a zoom also rolls the zoom
   readout. Both should repaint without laying out the Scene's document. */
static void scene_overlays(studio_bench *b)
{
    studio *s = b->studio;
    studio_show_editor(s, STUDIO_EDITOR_SCENE);
    s->prefs.scene_cameras = true;
    frames(b, 6);
    probe_documents(s);
    scene_case(b, PAN_STEP, 1, "Scene camera panned");
    scene_case(b, 0, ZOOM_STEP, "Scene camera zoomed a notch");
    frames(b, 2);
}

/* ---------- 7: popovers and area clipping ---------- */

#define POP_SIZE 200

/* Two areas side by side like Studio's (relative, rounded, overflow
 * hidden, a scrolling panel inside); the first holds a fixed popover that
 * reaches over the second and below both. */
static const char
    POPOVER_DOC[] = "<rml><head><style>"
                    "body { width: 100%%; height: 100%%; margin: 0px; "
                    "background-color: #000000; }"
                    ".area { position: absolute; top: 0px; width: 100px; "
                    "height: 100px; overflow: hidden; "
                    "border-radius: 12px; }"
                    ".panel { position: absolute; left: 0px; top: 0px; right: "
                    "0px; bottom: 0px; overflow-y: auto; }"
                    "#a { left: 0px; background-color: #202020; %s }"
                    "#b { left: 100px; background-color: #00ff00; %s }"
                    ".pop { position: fixed; left: 60px; top: 60px; width: "
                    "100px; height: 60px; "
                    "background-color: #ff00ff; z-index: 10; %s }"
                    "</style></head><body>"
                    "<div class=\"area\" id=\"a\"><div class=\"panel\"><div "
                    "class=\"pop\"/></div></div>"
                    "<div class=\"area\" id=\"b\"/>"
                    "</body></rml>";

static CF_Pixel pixel_at(const CF_Pixel *pixels, int x, int y)
{
    return pixels[y * POP_SIZE + x];
}

static bool magenta(CF_Pixel p)
{
    return p.colors.r > 200 && p.colors.g < 60 && p.colors.b > 200;
}

static const char *seen(CF_Pixel p)
{
    return magenta(p)       ? "popover"
         : p.colors.g > 200 ? "neighbor area"
         : p.colors.r > 20  ? "own area"
                            : "nothing (clipped)";
}

static void popover_case(rml_backend *ui, const char *name, const char *a, const char *b, const char *pop)
{
    char text[2048];
    snprintf(text, sizeof text, POPOVER_DOC, a, b, pop);
    rml_element *doc = rml_context_load_document_from_memory(rml_backend_context(ui), text, "popover");
    assert(doc);
    rml_document_show(doc, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_AUTO);
    CF_Canvas target = cf_make_canvas(cf_canvas_defaults(POP_SIZE, POP_SIZE));
    static CF_Pixel pixels[POP_SIZE * POP_SIZE];
    for (int frame = 0; frame < 2; frame++) {
        cf_app_update(NULL);
        rml_backend_update(ui, FRAME);
        rml_backend_render(ui);
        cf_draw_push();
        rml_backend_present(ui);
        cf_render_to(target, true);
        cf_draw_pop();
        cf_app_draw_onto_screen(true);
    }
    CF_Readback readback = cf_canvas_readback(target);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        assert(cf_get_ticks() - start < cf_get_tick_frequency() * 10);
        cf_sleep(1);
    }
    int read = cf_readback_data(readback, pixels, (int)sizeof pixels);
    assert(read == (int)sizeof pixels);
    cf_destroy_readback(readback);
    printf(
        "| 7 | %s | inside its area: %s; over the next area: %s; outside both: "
        "%s | | | | | |\n",
        name,
        seen(pixel_at(pixels, 80, 80)),
        seen(pixel_at(pixels, 130, 80)),
        seen(pixel_at(pixels, 130, 110))
    );
    cf_destroy_canvas(target);
    rml_document_close(doc);
    rml_backend_update(ui, 0);
}

static void popover_clipping()
{
    rml_backend *ui = rml_backend_create(POP_SIZE, POP_SIZE, 1);
    popover_case(ui, "fixed popover, clip: none", "", "", "clip: none;");
    popover_case(ui, "fixed popover without clip: none", "", "", "");
    popover_case(ui, "clip: none, the next area has z-index: 1", "", "z-index: 1;", "clip: none;");
    popover_case(ui, "clip: none, its area has z-index: 0", "z-index: 0;", "", "clip: none;");
    popover_case(ui, "clip: none, its area has a transform", "transform: translateX(0px);", "", "clip: none;");
    popover_case(ui, "clip: none, its area has a filter", "filter: opacity(1);", "", "clip: none;");
    rml_backend_destroy(ui);
}

/* ---------- 10: Members over ~200 members ---------- */

#define MEMBER_VARIABLES 192
#define MEMBER_CATEGORY 16 /* variables per category */

/* Members' scroller, native (the kit's list) or authored (the List). */
static rml_element *members_list(studio *s)
{
    rml_element *authored = studio_query(s, "#members-list");
    return authored ? authored : studio_query(s, ".members-list");
}

/* What one run of a Members case changes before its UI work. */
typedef enum
{
    MEMBERS_IDLE,
    MEMBERS_SELECT,
    MEMBERS_SCROLL,
    MEMBERS_FILTER,
} members_step;

static void members_change(studio *s, members_step step, int run, const uint64_t *ids)
{
    rml_element *list = members_list(s);
    float row = 26 * studio_pixel_ratio(s);
    switch (step) {
        case MEMBERS_IDLE: break;
        case MEMBERS_SELECT:
            s->members.kind = STUDIO_MEMBER_VARIABLE;
            s->members.id = ids[run & 1];
            break;
        case MEMBERS_SCROLL:
            rml_element_set_scroll_top(list, (run & 1 ? 40 : 43) * row);
            break;
        case MEMBERS_FILTER:
            snprintf(s->members.filter, sizeof s->members.filter, "%s", run & 1 ? "Value 1" : "");
            break;
    }
}

static void members_step_row(studio_bench *b, members_step step, const char *name, const uint64_t *ids)
{
    studio *s = b->studio;
    double ms[RUNS], phases[PHASE_COUNT][RUNS], phase[PHASE_COUNT];
    bench_counts counts = { 0 };
    for (int run = -WARMUP; run < RUNS; run++) {
        members_change(s, step, run, ids);
        cf_app_update(NULL);
        bench_counts before = studio_mark(s);
        double t = native_ui(s, phase);
        cf_app_draw_onto_screen(true);
        counts = studio_counts(s, before);
        frames(b, 1);
        if (run < 0) {
            continue;
        }
        ms[run] = t;
        for (int p = 0; p < PHASE_COUNT; p++) {
            phases[p][run] = phase[p];
        }
    }
    bench_row("10", name, bench_stat_of(ms, RUNS), counts);
    phase_row("phases", phases);
}

/* The open behavior with MEMBER_VARIABLES more variables in categories,
   shown by Members in the Properties area: a UI frame with nothing
   changed, a selection change, a scroll step of 3 rows and a filter
   keystroke. Run each build's binary in turn for an A/B. */
static void members(studio_bench *b)
{
    studio *s = b->studio;
    behavior_asset *asset = b->graph->asset;
    for (int i = 0; i < MEMBER_VARIABLES; i++) {
        char name[32], category[32];
        snprintf(name, sizeof name, "Value %d", i);
        snprintf(category, sizeof category, "Group %d", i / MEMBER_CATEGORY);
        behavior_variable *v = behavior_asset_add_variable(
            asset,
            name,
            PIN_F32,
            "float",
            PIN_CONTAINER_SINGLE,
            NULL,
            category
        );
        assert(v);
    }
    uint64_t ids[2] = {
        asset->variables[acount(asset->variables) - MEMBER_VARIABLES].id,
        asset->variables[acount(asset->variables) - MEMBER_VARIABLES + 2].id
    };
    s->maximized = -1;
    studio_active_layout(s)->editors[STUDIO_AREA_PROPERTIES] = STUDIO_EDITOR_MEMBERS;
    frames(b, 6);
    rml_element *list = members_list(s);
    assert(list);
    probe_documents(s);
    float ratio = studio_pixel_ratio(s);
    rml_vec2 size = rml_element_box_size(list, RML_BOX_BORDER);
    rml_element *rows[256];
    int count = rml_element_query_selector_all(list, ".tree-row", rows, 256),
        shown = 0;
    for (int i = 0; i < count && i < 256; i++) {
        shown += rml_element_is_visible(rows[i], true);
    }
    printf(
        "|  | Members %.0fx%.0f dp over %d variables; %d rows in its window | "
        "| | | | | |\n",
        size.x / ratio,
        size.y / ratio,
        acount(asset->variables),
        shown
    );
    members_step_row(b, MEMBERS_IDLE, "Members: a UI frame with nothing changed", ids);
    members_step_row(b, MEMBERS_SELECT, "Members: select another variable", ids);
    members_step_row(b, MEMBERS_SCROLL, "Members: scroll 3 rows", ids);
    members_step_row(b, MEMBERS_FILTER, "Members: filter by a keystroke", ids);
}

/* ---------- Run ---------- */

int ui_bench_studio(const char *program, bool members_only)
{
    CF_Result result = cf_make_app(
        "UI bench",
        0,
        0,
        0,
        WIDTH,
        HEIGHT,
        CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT,
        program
    );
    if (cf_is_error(result)) {
        fprintf(stderr, "UI bench app: %s\n", result.details);
        return 1;
    }
    app_scene_renderer_initialize();
    app_input_initialize();
    studio_bench b = bench_open();
    frames(&b, 4);
    probe_documents(b.studio);
    bench_header("Studio on the Sandbox at 1440x900, native");
    printf("|  | pixel scale %.1f | | | | | | |\n", cf_app_get_pixel_scale());
    /* UI_BENCH_SCENE runs case 15 alone. */
    if (getenv("UI_BENCH_SCENE")) {
        scene_overlays(&b);
        bench_close(&b);
        app_input_shutdown();
        cf_destroy_app();
        return 0;
    }
    /* UI_BENCH_ASSETS runs case 14 alone. */
    bool assets_only = getenv("UI_BENCH_ASSETS") != NULL;
    if (!members_only && !assets_only) {
        full_layout(&b, false);
        full_layout(&b, true);
        b.studio->prefs.comfortable = false;
        area_text_change(&b);
        properties(&b);
        output_log(&b);
        scene_overlays(&b);
    }
    if (!assets_only) {
        members(&b);
    }
    if (!members_only) {
        assets(&b);
    }
    /* Last: it leaves ten thousand objects in the scene. */
    if (!members_only && !assets_only) {
        outliner_scroll(&b);
    }
    bench_close(&b);
    if (!members_only && !assets_only) {
        popover_clipping();
    }
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
