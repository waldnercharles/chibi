/* The Studio cases of the S0 UI bench (ui_bench.h), native in a hidden
 * window: today's Studio document over the Sandbox at 1440x900, its
 * Properties showing the Player against the authored spike, and whether a
 * popover escapes an area's clipping. */
#include "editor/studio/studio_internal.h"
#include "editor/ui/kit.h"
#include "editor/workspace/input.h"
#include "editor/workspace/scene_internal.h"
#include "render/scene_renderer.h"
#include "test/editor/ui_bench.h"
#include "test/suite_internal.h"
#include "ui/rml.h"

#include <assert.h>
#include <stdio.h>
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

static void frames(studio_bench *b, int count)
{
    for (int i = 0; i < count; i++) {
        b->time += FRAME;
        cf_app_update(NULL);
        app_input_frame();
        editor_tick(b->graph, b->time);
        bool ran = app_scene_authoring_frame(b->scene, b->time) == FLOW_OK;
        assert(ran);
        studio_frame(b->studio, FRAME);
        cf_app_draw_onto_screen(true);
        b->studio->prefs_changed_at = 0;
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
    return t[PHASE_COUNT] - t[0];
}

static bench_counts studio_mark()
{
    return (bench_counts){ .layouts = bench_layouts() };
}

static bench_counts studio_counts(studio *s, bench_counts before)
{
    return (bench_counts){ .layouts = bench_layouts() - before.layouts,
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
        bench_counts before = studio_mark();
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
            bench_counts before = arm ? bench_spike_counts(c->spike) : studio_mark();
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
        bench_counts before = studio_mark();
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

/* ---------- Run ---------- */

int ui_bench_studio(const char *program)
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
    full_layout(&b, false);
    full_layout(&b, true);
    b.studio->prefs.comfortable = false;
    properties(&b);
    bench_close(&b);
    popover_clipping();
    app_input_shutdown();
    cf_destroy_app();
    return 0;
}
