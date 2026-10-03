/* Studio's shell: the authored top bar, status bar and area headers,
 * layouts, the command palette, keys, the wheel and the Performance
 * editor. */

#include "assets/ui_events.h"
#include "editor/collab/collab.h"
#include "editor/studio/studio_designer_internal.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/studio/studio_internal.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/editor_internal.h"
#include "editor/workspace/input.h"
#include "graph/document/serialize.h"
#include "graph/nodes/component_nodes.h"
#include "platform/cute.h"
#include "render/scene_renderer.h"
#include "render/screenshot.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "test/editor/studio_documents.h"
#include "test/editor/ui_bench.h"
#include "test/suite_internal.h"
#include "ui/input_feel.h"
#include "ui/popover.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void host_ui_refresh_keeps_input_immediate()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 90);
    s->active_until = 0;
    uint64_t before = s->ui_schedule.frames;
    double time = f.time;
    frames(&f, 60);
    uint64_t idle = s->ui_schedule.frames - before;
    test_check(idle >= 3 && idle < 10 && f.time > time + 0.99, "settled idle UI refresh is bounded while simulation frames keep advancing");

    s->active_until = 0;
    s->ui_schedule.next = s->time + 1;
    before = s->ui_schedule.frames;
    int toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){
            .title = "Async host wake",
            .duration = -1,
        }
    );
    frame(&f, false);
    char toast_text[128] = "";
    rml_element *toast_element = studio_element(s, ".toast .title");
    if (toast_element) {
        rml_element_inner_rml(toast_element, toast_text, sizeof toast_text);
    }
    test_check(
        s->ui_schedule.frames == before + 1 && strstr(toast_text, "Async host wake"),
        "a new toast wakes and renders the idle host without SDL input"
    );
    kit_toast_dismiss(s->kit, toast);
    frames(&f, 90);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 1;
    before = s->ui_schedule.frames;
    kit_style_sheet(s->kit, "host-wake-probe", ".statusbar { opacity: 0.999; }");
    frame(&f, false);
    char opacity[32] = "";
    rml_element *bar = studio_element(s, ".statusbar");
    if (bar) {
        rml_element_property(bar, "opacity", opacity, sizeof opacity);
    }
    test_check(s->ui_schedule.frames == before + 1 && fabs(atof(opacity) - 0.999) < 0.0001, "an asynchronous restyle wakes and updates the idle host without SDL input");
    kit_style_sheet(s->kit, "host-wake-probe", "");
    frames(&f, 3);

    before = s->ui_schedule.frames;
    s->prefs.short_bindings = !s->prefs.short_bindings;
    frame(&f, false);
    bool prefs = s->ui_schedule.frames == before + 1;
    /* Hold the passive deadline ahead to make each immediate wake observable. */
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    ed_select_node(f.graph, graph_node_by_id(sg_active_graph(s), 2), false);
    frame(&f, false);
    bool selection = s->ui_schedule.frames == before + 1;

    graph *selected_graph = sg_active_graph(s);
    node *first = graph_node_by_id(selected_graph, 2);
    node *second = graph_node_by_id(selected_graph, SETTER);
    node *third = graph_node_by_id(selected_graph, 6);
    assert(first && second && third);
    ed_add_selected(f.graph, second);
    frame(&f, false);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    ed_remove_selected(f.graph, second);
    ed_add_selected(f.graph, third);
    frame(&f, false);
    test_check(s->ui_schedule.frames == before + 1, "replacing a secondary selected node wakes host emission with the first node and count unchanged");

    assert(acount(selected_graph->edges) >= 2);
    ed_link_ref first_link, second_link;
    sg_link_ref_of(&selected_graph->edges[0], &first_link);
    sg_link_ref_of(&selected_graph->edges[1], &second_link);
    ed_add_selected_link(f.graph, &first_link);
    frame(&f, false);
    s->active_until = 0;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    asetlen(f.graph->selected_links, 0);
    ed_add_selected_link(f.graph, &second_link);
    frame(&f, false);
    test_check(s->ui_schedule.frames == before + 1, "replacing a selected link wakes host emission with its selection count unchanged");

    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    graph *gr = sg_active_graph(s);
    assert(
        graph_set_node_presentation(gr, graph_node_by_id(gr, SETTER), NODE_PRESENTATION_EXPANDED) == 0
    );
    frame(&f, false);
    bool model = s->ui_schedule.frames == before + 1;
    s->ui_schedule.next = s->time + 0.25;
    before = s->ui_schedule.frames;
    text_event("a");
    frame(&f, false);
    bool input = s->ui_schedule.frames == before + 1;
    test_check(prefs && selection && model && input, "preferences, selection, direct graph edits and SDL text wake the host UI on the next frame");

    s->active_until = 0;
    s->in.down[0] = true;
    before = s->ui_schedule.frames;
    frames(&f, 3);
    bool held = s->ui_schedule.frames == before + 3;
    s->in.down[0] = false;
    test_check(held, "held pointer interaction retains one UI emission per simulation frame");

    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 4);
    s->active_until = 0;
    before = s->ui_schedule.frames;
    time = f.time;
    frames(&f, 60);
    uint64_t playing = s->ui_schedule.frames - before;
    app_runtime_state runtime = app_scene_authoring_runtime_state(f.scene);
    if (!runtime.playing || playing < 15 || playing >= 30 || f.time <= time + 0.99) {
        printf(
            "  playing refresh: running=%d target=%d emissions=%llu "
            "elapsed=%.6f kit_idle=%.6f rml_next=%.6f active_for=%.6f "
            "next_in=%.6f\n",
            runtime.playing,
            runtime.target,
            (unsigned long long)playing,
            f.time - time,
            kit_idle_time(s->kit),
            rml_context_next_update_delay(rml_backend_context(s->backend)),
            s->active_until - s->time,
            s->ui_schedule.next - s->time
        );
    }
    test_check(runtime.playing && playing >= 15 && playing < 30 && f.time > time + 0.99, "playing inspection refreshes at a bounded rate while runtime frames continue");
    /* A real input burst keeps the application active, while settled model
       inspection can continue at its passive cadence. Empty canvas does not
       start tooltip or control motion. */
    mouse_move(
        s,
        s->graph.surface.rect.x + s->graph.surface.rect.w - 12,
        s->graph.surface.rect.y + s->graph.surface.rect.h - 12
    );
    frame(&f, false);
    bool active = s->active_until > s->time + 0.9;
    before = s->ui_schedule.frames;
    time = f.time;
    frames(&f, 60);
    uint64_t after_input = s->ui_schedule.frames - before;
    test_check(
        active && after_input >= 15 && after_input < 30 &&
            app_scene_authoring_runtime_state(f.scene).playing && f.time > time + 0.99,
        "a real input activity window keeps runtime clocks advancing without "
        "forcing full-rate host model emissions"
    );
    studio_runtime(s, APP_RUNTIME_STOP);
    fixture_free(&f);
}

static void performance_pointer_button(studio *s, float x, float y, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP };
    event.button.windowID = SDL_GetWindowID(cf_app_get_window());
    event.button.button = SDL_BUTTON_MIDDLE;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x * s->prefs.ui_scale;
    event.button.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

static bool performance_pan_frames(studio_fixture *f, float x, float y)
{
    studio *s = f->studio;
    bool deadline = true;
    for (int i = 0; i < 60; i++) {
        double next = s->perf_publish_next;
        uint64_t publications = s->perf_publications;
        float camera_x = s->graph.x, camera_y = s->graph.y;
        rml_element *focus = rml_context_focus_element(rml_backend_context(s->backend));
        mouse_move(s, x + (float)(i % 20), y + (float)(i % 10));
        frame(f, false);
        deadline &= (!i || s->graph.x != camera_x || s->graph.y != camera_y) &&
                    rml_context_focus_element(rml_backend_context(s->backend)) == focus;
        if (s->time < next) {
            deadline &= s->perf_publications == publications &&
                        s->perf_publish_next == next;
        }
        deadline &= s->graph.gesture == STUDIO_GRAPH_PAN;
    }
    return deadline;
}

static void performance_snapshot_retains_intermediate_frames()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 90);
    float x = s->graph.surface.rect.x + s->graph.surface.rect.w / 2;
    float y = s->graph.surface.rect.y + s->graph.surface.rect.h / 2;
    mouse_move(s, x, y);
    performance_pointer_button(s, x, y, true);
    frame(&f, false);
    int recorded = acount(s->perf);
    uint64_t published = s->perf_publications, emitted = s->ui_schedule.frames;
    bool idle_deadline = performance_pan_frames(&f, x, y);
    uint64_t idle_publications = s->perf_publications - published;
    test_check(
        idle_deadline && acount(s->perf) == recorded + 60 && idle_publications >= 3 &&
            idle_publications <= 5 && s->ui_schedule.frames >= emitted + 3 &&
            s->ui_schedule.frames <= emitted + 10,
        "real idle panning advances the camera and recorder every frame while "
        "host inspection and telemetry stay bounded without losing focus or "
        "rebasing deadlines"
    );
    emitted = s->ui_schedule.frames;
    mouse_move(s, s->graph.surface.rect.x - 4, y);
    frame(&f, false);
    bool boundary = s->ui_schedule.frames == emitted + 1 &&
                    !s->graph.surface.hovered && s->graph.gesture == STUDIO_GRAPH_PAN;
    emitted = s->ui_schedule.frames;
    mouse_move(s, x, y);
    frame(&f, false);
    test_check(
        boundary && s->ui_schedule.frames == emitted + 1 &&
            s->graph.surface.hovered && s->graph.gesture == STUDIO_GRAPH_PAN,
        "a captured pan still routes leaving and reentering the graph surface "
        "through the host"
    );

    emitted = s->ui_schedule.frames;
    studio_inspect_toggle(s);
    mouse_move(s, x + 1, y + 1);
    frame(&f, false);
    test_check(studio_inspect_picking(s) && s->ui_schedule.frames == emitted + 1, "Inspect Element picking keeps host motion delivery during an established graph pan");
    studio_inspect_toggle(s);
    frame(&f, false);

    kit_command_open(s->kit, "Search");
    emitted = s->ui_schedule.frames;
    mouse_move(s, x + 2, y + 2);
    frame(&f, false);
    rml_element *input = studio_element(s, ".cmd-input input");
    test_check(
        kit_command_is_open(s->kit) && input &&
            rml_context_focus_element(rml_backend_context(s->backend)) == input &&
            s->ui_schedule.frames == emitted + 1,
        "a command overlay immediately takes host focus and input during graph "
        "panning"
    );
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frame(&f, false);
    performance_pointer_button(s, x, y, false);
    emitted = s->ui_schedule.frames;
    frame(&f, false);
    test_check(
        s->ui_schedule.frames == emitted + 1 &&
            s->graph.gesture == STUDIO_GRAPH_IDLE && !s->graph.captured &&
            !s->in.down[2] && !s->graph.in.down[2] && !kit_command_is_open(s->kit),
        "Escape and the captured pan release keep normal host and graph input "
        "lifecycles"
    );
    frames(&f, 90);

    published = s->perf_publications;
    studio_runtime(s, APP_RUNTIME_PLAY);
    frame(&f, false);
    test_check(s->perf_publish_playing && s->perf_publications == published + 1, "entering Play immediately publishes telemetry instead of waiting for the idle deadline");
    published = s->perf_publications;
    mouse_move(s, x + 1, y + 1);
    frame_delta(&f, false, 0.025);
    float spike_time = alast(s->perf).time;
    bool intermediate = s->perf_publications == published &&
                        fabsf(alast(s->perf).frame_ms - 25) < 0.001f;
    frames(&f, 2);
    bool retained = false;
    for (int i = 0; i < acount(s->perf_published); i++) {
        const studio_perf_sample *sample = &s->perf_published[i];
        retained |= sample->time == spike_time && fabsf(sample->frame_ms - 25) < 0.001f;
    }
    test_check(intermediate && retained && s->perf_publications > published, "a longer real frame between telemetry polls survives in the next complete published history");

    performance_pointer_button(s, x, y, true);
    frame(&f, false);
    recorded = acount(s->perf);
    published = s->perf_publications;
    emitted = s->ui_schedule.frames;
    bool playing_deadline = performance_pan_frames(&f, x, y);
    uint64_t playing_publications = s->perf_publications - published;
    test_check(
        playing_deadline && acount(s->perf) == recorded + 60 &&
            playing_publications >= 15 && playing_publications <= 21 &&
            s->ui_schedule.frames >= emitted + 15 &&
            s->ui_schedule.frames <= emitted + 29,
        "real playing panning retains full-rate camera input and recording "
        "while host inspection and telemetry publish at bounded rates"
    );
    performance_pointer_button(s, x, y, false);
    frame(&f, false);
    published = s->perf_publications;
    studio_runtime(s, APP_RUNTIME_STOP);
    frame(&f, false);
    test_check(
        !s->perf_publish_playing && s->perf_publications == published + 1,
        "stopping Play immediately publishes the idle telemetry mode"
    );
    fixture_free(&f);
}

static rml_element *status_bar(studio *s)
{
    return test_query(rml_backend_context(s->backend), "[data-ui=StatusBar]");
}

/* The shown labels of the status bar's key hints, joined by '|'. */
static void status_hints(studio *s, char *out, size_t size)
{
    rml_element *labels[8];
    int count = rml_element_query_selector_all(status_bar(s), "[data-ui=KeyHint] .label", labels, 8);
    out[0] = '\0';
    for (int i = 0; i < count && i < 8; i++) {
        char text[64] = "";
        rml_element_inner_rml(labels[i], text, sizeof text);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s", used ? "|" : "", text);
    }
}

static bool toggle_on(studio *s)
{
    char state[8] = "";
    rml_element *assets = rml_element_query_selector(status_bar(s), ".toggle-drawer");
    rml_element_attribute(assets, "data-state", state, sizeof state);
    return !strcmp(state, "on");
}

/* Studio shows the authored status bar over its own state: the hovered
   area's hints, the last report, a drawer toggle that runs its command, and
   UI Scale native in its slot. */
static void status_bar_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    frames(&f, 3);
    kit_rect area = s->areas[STUDIO_AREA_TOP].rect;
    s->in.mouse_x = area.x + area.w / 2;
    s->in.mouse_y = area.y + area.h / 2;
    frames(&f, 2);
    char scene[256], elsewhere[256];
    status_hints(s, scene, sizeof scene);
    s->in.mouse_x = s->in.mouse_y = 1;
    frames(&f, 2);
    status_hints(s, elsewhere, sizeof elsewhere);
    bool hints = !strcmp(scene, "Select|Toggle|Grab|Snap|Frame") &&
                 !strcmp(elsewhere, "Select|Search|Shortcuts");

    studio_report(s, "Saved");
    frames(&f, 2);
    rml_element *report = rml_element_query_selector(status_bar(s), ".report");
    char said[32] = "";
    rml_element_attribute(rml_element_query_selector(report, "morph-text"), "aria-label", said, sizeof said);
    bool reported = rml_element_is_visible(report, false) && !strcmp(said, "Saved");
    s->status_time = s->time - 4;
    frames(&f, 2);
    reported &= rml_element_has_attribute(report, "data-ending-style") ||
                !rml_element_is_visible(report, false);

    int before = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool shown_before = toggle_on(s) == (before == 1);
    rml_element_click(rml_element_query_selector(status_bar(s), ".toggle-drawer"));
    frames(&f, 2);
    int after = studio_command_checked(s, STUDIO_COMMAND_TOGGLE_ASSETS);
    bool toggled = shown_before && after != before && toggle_on(s) == (after == 1);

    kit_rect scale;
    bool native = kit_find(s->kit, "app/status/scale/scale", &scale);
    if (!(hints && reported && toggled && native)) {
        printf("  %d %d %d %d: %s / %s\n", hints, reported, toggled, native, scene, elsewhere);
    }
    test_check(
        hints && reported && toggled && native,
        "Studio's authored status bar follows the hovered area and reports, "
        "runs "
        "its drawer toggles' commands and hosts UI Scale natively"
    );
    fixture_free(&f);
}

/* The palette finds settings: a switch flips where it is found, and any
   other setting opens Settings searching for it. */
static void palette_finds_settings()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 2);
    bool vim = s->prefs.code_vim;
    studio_command_run(s, STUDIO_COMMAND_SEARCH);
    frames(&f, 2);
    text_event("vim");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool flipped = s->prefs.code_vim == !vim && !kit_command_is_open(s->kit) &&
                   studio_element(s, ".toast");
    studio_command_run(s, STUDIO_COMMAND_SEARCH);
    frames(&f, 2);
    text_event("wire style");
    frames(&f, 2);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 4);
    rml_element *search = studio_element(s, ".search-input");
    char query[64] = "";
    if (search) {
        rml_element_value(search, query, sizeof query);
    }
    bool searched = !strcmp(query, "Wire Style");
    if (!(flipped && searched)) {
        printf("  %d %d '%s'\n", flipped, searched, query);
    }
    test_check(
        flipped && searched,
        "the palette flips a switch setting where it "
        "finds it and opens Settings at any other"
    );
    fixture_free(&f);
}

/* The palette's first row's label, without its highlighting, and the group
   it heads, or "". */
static void first_row(studio *s, char *label, char *group, size_t size)
{
    label[0] = group[0] = '\0';
    char rml[256] = "";
    rml_element *row = studio_element(s, ".cmd-item .label");
    if (row) {
        rml_element_inner_rml(row, rml, sizeof rml);
    }
    size_t n = 0;
    bool tag = false;
    for (const char *c = rml; *c && n + 1 < size; c++) {
        tag = *c == '<' || (tag && *c != '>');
        if (!tag && *c != '>') {
            label[n++] = *c;
        }
    }
    label[n] = '\0';
    rml_element *heading = studio_element(s, ".cmd-group");
    if (heading) {
        rml_element_inner_rml(heading, group, size);
    }
}

static void search_for(studio_fixture *f, const char *query)
{
    /* The last palette finishes leaving first. */
    frames(f, 20);
    studio_command_run(f->studio, STUDIO_COMMAND_SEARCH);
    frames(f, 2);
    if (query[0]) {
        text_event(query);
    }
    frames(f, 2);
}

/* The palette puts what was chosen lately first, alone while nothing is
   typed and above equal matches after; '>' keeps commands and settings, '@'
   the open behavior's graphs and nodes, and '?' lists the prefixes, one of
   which types itself. */
static void palette_recent_and_prefixes()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    studio_prefs saved = s->prefs;
    memset(s->prefs.recent, 0, sizeof s->prefs.recent);
    frames(&f, 2);
    char label[128], group[64];
    search_for(&f, "toggle");
    first_row(s, label, group, sizeof label);
    bool unranked = !strcmp(label, "Toggle Assets");
    tap(SDL_SCANCODE_DOWN);
    tap(SDL_SCANCODE_DOWN);
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 2);
    bool remembered = !strcmp(s->prefs.recent[0].key, "command:togglePerformance") &&
                      s->prefs.recent[0].uses == 1;
    search_for(&f, "toggle");
    first_row(s, label, group, sizeof label);
    bool boosted = !strcmp(label, "Toggle Performance");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "");
    first_row(s, label, group, sizeof label);
    bool recent_first = !strcmp(label, "Toggle Performance") &&
                        !strcmp(group, "Recent");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);

    search_for(&f, ">weapons");
    first_row(s, label, group, sizeof label);
    bool commands_only = strcmp(group, "Assets") != 0;
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "@");
    first_row(s, label, group, sizeof label);
    bool symbols = !strcmp(group, "In This Behavior") && label[0];
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    search_for(&f, "?");
    tap(SDL_SCANCODE_RETURN);
    frames(&f, 3);
    bool prefix = kit_command_is_open(s->kit) &&
                  !strcmp(kit_command_query(s->kit), ">");
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    if (!(unranked && remembered && boosted && recent_first && commands_only &&
          symbols && prefix)) {
        printf(
            "  %d %d %d %d %d %d %d '%s' '%s'\n",
            unranked,
            remembered,
            boosted,
            recent_first,
            commands_only,
            symbols,
            prefix,
            label,
            group
        );
    }
    test_check(
        unranked && remembered && boosted && recent_first,
        "the palette ranks what was chosen lately first, and leads with "
        "it while nothing is typed"
    );
    test_check(
        commands_only && symbols && prefix,
        "'>' keeps commands and settings, '@' the open behavior's "
        "symbols, and '?' types a prefix"
    );
    s->prefs = saved;
    s->applied = saved;
    fixture_free(&f);
}

/* Turns the wheel `notches` at (x, y) in dp, as SDL reports a notch. */
static void wheel_at(const studio *s, float x, float y, float notches)
{
    SDL_Event wheel = { .type = SDL_EVENT_MOUSE_WHEEL };
    wheel.wheel.windowID = SDL_GetWindowID(cf_app_get_window());
    wheel.wheel.mouse_x = x * s->prefs.ui_scale;
    wheel.wheel.mouse_y = y * s->prefs.ui_scale;
    wheel.wheel.y = notches;
    assert(SDL_PushEvent(&wheel));
}

static void button_at(const studio *s, float x, float y, Uint8 button, bool down)
{
    SDL_Event event = { .type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP };
    event.button.windowID = SDL_GetWindowID(cf_app_get_window());
    event.button.button = button;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x * s->prefs.ui_scale;
    event.button.y = y * s->prefs.ui_scale;
    assert(SDL_PushEvent(&event));
}

static bool near_ratio(float ratio, float expected)
{
    return fabsf(ratio / expected - 1) < 0.002f;
}

static float camera_span(const studio_fixture *f)
{
    engine_viewport_camera camera;
    engine_viewport_get_camera(f->viewport, &camera);
    return camera.span_width;
}

/* A notch scrolls a list FEEL_SCROLL_POINTS window points, and a trackpad's
   two half notches in one frame scroll as far. */
static void wheel_scrolls_lists()
{
    studio_fixture f = studio_project_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_layout *layout = studio_active_layout(s);
    layout->editors[STUDIO_AREA_TOP] = STUDIO_EDITOR_PANEL;
    snprintf(layout->panels[STUDIO_AREA_TOP], sizeof layout->panels[0], "Widgets");
    frames(&f, 12);
    rml_element *view = studio_element(s, ".vlist");
    assert(view);
    /* The list sits low in the Widgets panel, which scrolls it into view. */
    rml_element_scroll_into_view(view, RML_SCROLL_CENTER, RML_SCROLL_NEAREST);
    frames(&f, 4);
    rml_vec2 at = rml_element_absolute_offset(view, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(view, RML_BOX_BORDER);
    float ratio = studio_pixel_ratio(s);
    float x = (at.x + size.x / 2) / ratio, y = (at.y + size.y / 2) / ratio;
    float notch = FEEL_SCROLL_POINTS * cf_app_get_pixel_scale();
    mouse_move(s, x, y);
    frames(&f, 2);
    float top = rml_element_scroll_top(view);
    wheel_at(s, x, y, -1);
    frames(&f, 30);
    float one = rml_element_scroll_top(view);
    wheel_at(s, x, y, -0.5f);
    wheel_at(s, x, y, -0.5f);
    frames(&f, 30);
    float two = rml_element_scroll_top(view);
    if (fabsf(one - top - notch) > 1 || fabsf(two - one - notch) > 1) {
        printf("  scrolled %.1f then %.1f px, a notch %.1f\n", one - top, two - one, notch);
    }
    test_check(
        fabsf(one - top - notch) <= 1 && fabsf(two - one - notch) <= 1,
        "a wheel notch scrolls a list 100 points, and a trackpad's two half "
        "notches in one frame as far"
    );
    fixture_free(&f);
}

/* A notch zooms the Scene's camera and the graph by FEEL_ZOOM_RATE once,
   however many frames draw after it; half notches zoom by half; one event
   never more than halves the zoom. */
static void wheel_zooms_canvases()
{
    studio_fixture scene = vault_open();
    studio *s = scene.studio;
    frames(&scene, 10);
    kit_rect r = s->surface.rect;
    float x = r.x + r.w / 2, y = r.y + r.h / 2;
    mouse_move(s, x, y);
    frames(&scene, 3);
    float before = camera_span(&scene);
    wheel_at(s, x, y, 1);
    frames(&scene, 8);
    float one = camera_span(&scene);
    wheel_at(s, x, y, 0.5f);
    wheel_at(s, x, y, 0.5f);
    frames(&scene, 8);
    float two = camera_span(&scene);
    wheel_at(s, x, y, -10);
    frames(&scene, 8);
    float most = camera_span(&scene);
    float notch = feel_zoom_scale(1);
    if (!near_ratio(before / one, notch) || !near_ratio(one / two, notch) ||
        !near_ratio(most / two, 2)) {
        printf("  Scene zoomed %.4f, %.4f and %.4f\n", before / one, one / two, most / two);
    }
    test_check(
        near_ratio(before / one, notch) && near_ratio(one / two, notch) &&
            near_ratio(most / two, 2),
        "a wheel notch zooms the Scene camera about 15% once, two half notches "
        "as much, and one event at most halves it"
    );
    /* A right press while the left is held reaches the Scene, whose tools
       cancel a drag with it, and only in its own frame. */
    button_at(s, x, y, SDL_BUTTON_LEFT, true);
    frames(&scene, 2);
    mouse_move(s, x + 40, y + 20);
    button_at(s, x + 40, y + 20, SDL_BUTTON_RIGHT, true);
    frame(&scene, false);
    bool heard = s->surface.pressed[1] && s->surface.down[0];
    frame(&scene, false);
    bool once = !s->surface.pressed[1];
    button_at(s, x + 40, y + 20, SDL_BUTTON_RIGHT, false);
    button_at(s, x + 40, y + 20, SDL_BUTTON_LEFT, false);
    frames(&scene, 2);
    if (kit_has_popup(s->kit)) {
        tap(SDL_SCANCODE_ESCAPE);
        frames(&scene, 2);
    }
    test_check(heard && once, "a right press during a Scene drag reaches its tools once");
    fixture_free(&scene);

    studio_fixture f = fixture_new();
    s = f.studio;
    frames(&f, 3);
    studio_graph *g = &s->graph;
    g->camera.active = false;
    g->framed = true;
    g->zoom = 1;
    frames(&f, 2);
    r = g->surface.rect;
    x = r.x + r.w / 2;
    y = r.y + r.h / 2;
    mouse_move(s, x, y);
    frames(&f, 2);
    wheel_at(s, x, y, 1);
    frames(&f, 3);
    float zoom = g->zoom;
    wheel_at(s, x, y, -0.5f);
    wheel_at(s, x, y, -0.5f);
    frames(&f, 3);
    test_check(
        near_ratio(zoom, notch) && near_ratio(g->zoom, 1),
        "a wheel notch zooms the graph about 15%, and two half notches back"
    );
    fixture_free(&f);
}

/* Runs one key press as Studio's shortcuts do, over area `hover`. */
static void press_over(studio *s, int hover, SDL_Keycode key, SDL_Keymod mod)
{
    s->hover_area = hover;
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ key, mod, false }));
    studio_shortcuts(s, false);
    asetlen(s->keys, 0);
}

/* Keys act on the area under the pointer (Blender): Undo there undoes the
   graph although another area was clicked last, Find opens the graph's,
   and a menu's Undo, with no key, still follows the area clicked last. */
static void keys_follow_pointer()
{
    studio_fixture f = fixture_new();
    studio *s = f.studio;
    frames(&f, 3);
    int graph = s->graph.area;
    assert(graph >= 0);
    editor *e = f.graph;
    int count = acount(editor_active_graph(e)->nodes);
    assert(count > 0);
    ed_clear_selection(e);
    ed_add_selected(e, editor_active_graph(e)->nodes[0]);
    ed_duplicate_selected(e);
    bool duplicated = acount(editor_active_graph(e)->nodes) > count;
    int other = graph == STUDIO_AREA_OUTLINER ? STUDIO_AREA_PROPERTIES
                                              : STUDIO_AREA_OUTLINER;
    s->focus_area = other;
    bool menu_follows_click = studio_command_area(s) == other;
    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    press_over(s, graph, SDLK_Z, primary);
    bool undone = acount(editor_active_graph(e)->nodes) == count;
    press_over(s, graph, SDLK_F, primary);
    bool found = s->graph.find.open;
    test_check(
        duplicated && menu_follows_click && undone && found && s->key_area < 0,
        "Undo and Find from the keyboard act on the area under the pointer, "
        "not the one clicked last"
    );
    fixture_free(&f);
}

/* Repeats remain limited to commands that are useful while held. */
static void shortcut_repeat_policy()
{
    studio_fixture zoom = fixture_new();
    studio *s = zoom.studio;
    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    snprintf(s->prefs.shortcuts[STUDIO_COMMAND_ZOOM_IN], STUDIO_CHORD_SIZE, "Mod+F12");
    s->prefs.ui_scale = 1;
    apush(s->keys, ((studio_key){ SDLK_F12, primary, true }));
    studio_shortcuts(s, false);
    float held_scale = s->prefs.ui_scale;
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_F12, primary, false }));
    studio_shortcuts(s, false);
    bool zoom_repeat_runs = held_scale > 1;
    bool zoom_press_runs = s->prefs.ui_scale > held_scale;
    fixture_free(&zoom);

    studio_fixture search = fixture_new();
    s = search.studio;
    primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    apush(s->keys, ((studio_key){ SDLK_K, primary, true }));
    studio_shortcuts(s, false);
    bool toggle_repeat_ignored = !kit_command_is_open(s->kit);
    asetlen(s->keys, 0);
    apush(s->keys, ((studio_key){ SDLK_K, primary, false }));
    studio_shortcuts(s, false);
    bool toggle_press_runs = kit_command_is_open(s->kit);
    test_check(
        zoom_repeat_runs && zoom_press_runs && toggle_repeat_ignored && toggle_press_runs,
        "repeatable zoom runs on a held shortcut while the Search toggle "
        "waits for a fresh press"
    );
    fixture_free(&search);
}

/* Whether the Scene's last draw framed the camera it holds now, as the
   pointer it measured shows. */
static bool drew_camera(const studio_fixture *f)
{
    engine_viewport_camera camera;
    engine_viewport_pointer pointer;
    engine_viewport_get_camera(f->viewport, &camera);
    engine_viewport_get_pointer(f->viewport, &pointer);
    float width = f->studio->surface.rect.w * f->studio->prefs.ui_scale;
    return fabsf(pointer.world_per_pixel * width - camera.span_width) <
           0.01f * camera.span_width;
}

/* The Scene draws the camera its first draw fits to the Vault, which the
   reset camera does not frame, and its zoom readout shows nothing before
   then and that camera's zoom after, so it never rolls from the reset
   camera's. */
static void readout_follows_camera()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    bool shown = false, current = true, drawn = true;
    for (int i = 0; i < 6; i++) {
        frames(&f, 1);
        drawn &= f.scene->camera_target < 0 || drew_camera(&f);
        rml_element *readout = studio_element(s, ".readout");
        if (!readout) {
            continue;
        }
        engine_viewport_view view;
        engine_viewport_get_view(f.viewport, &view);
        char value[32], expected[32];
        rml_element_attribute(readout, "aria-label", value, sizeof value);
        snprintf(expected, sizeof expected, "%d%%", (int)roundf(s->surface.rect.w / view.width * 100));
        current &= !strcmp(value, expected);
        shown = true;
    }
    test_check(
        shown && current && drawn,
        "the Scene draws the camera it fits from its first frame, and its "
        "zoom readout shows that camera"
    );
    fixture_free(&f);
}

/* Studio shows its authored top bar and area headers over its own state:
   the menubar, the play target and each area's editor type menu and
   controls in their native slots, the document tabs, whose faces and close
   buttons run their commands with the tab's index, the transport's
   commands, and each header's Maximize. */
static void chrome_follows_studio()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    /* The Text Editor shows the document, so the Scene gives up the workspace. */
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 3);
    kit_rect r;
    rml_element *heads[8], *tabs[8], *play[8];
    int shown = panel_elements(s, "AreaHeader", ".area-head", heads);
    bool slots = kit_find(s->kit, "app/topbar/menus/file", &r) &&
                 kit_find(s->kit, "app/topbar/target/target", &r) && heads[0] &&
                 rml_element_query_selector(heads[0], ".editor-type") &&
                 kit_find(s->kit, "app/workspace/main/editors/top/head/controls", &r) &&
                 s->maximized < 0 && shown >= STUDIO_AREA_COUNT - 1;
    int count = panel_elements(s, "TopBar", ".doc-tab", tabs) - 1;
    int bank = tab_named(f.scene, "Bank"), scene = bank == 0 ? 1 : 0;
    rml_element *close = bank >= 0
                           ? rml_element_query_selector(tabs[bank], ".doc-close")
                           : NULL;
    bool listed = count == acount(f.scene->tabs) && f.scene->active_tab == bank &&
                  close && rml_element_is_visible(close, true) &&
                  !rml_element_is_visible(rml_element_query_selector(tabs[scene], ".doc-close"), true);
    /* The press takes the keyboard from the Text Editor, whose focused text
       would make its document active again. */
    press(s, rml_element_query_selector(tabs[scene], ".face"));
    frames(&f, 2);
    bool selected = f.scene->active_tab == scene &&
                    kit_wants_keyboard(s->kit) && studio_ui_shortcut_focus(s);
    /* This narrow window clips the last tab, so the click goes to it. */
    rml_element_click(close);
    frames(&f, 2);
    bool closed = tab_named(f.scene, "Bank") < 0 &&
                  panel_elements(s, "TopBar", ".doc-tab", tabs) - 1 ==
                      acount(f.scene->tabs);

    panel_elements(s, "TopBar", ".group > .btn", play);
    rml_element_click(play[0]);
    frames(&f, 2);
    bool playing = app_scene_authoring_runtime_state(f.scene).playing;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 1);

    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool maximized = s->maximized == STUDIO_AREA_TOP &&
                     panel_elements(s, "AreaHeader", ".area-head", heads) == 1;
    rml_element_click(rml_element_query_selector(heads[0], ".maximize"));
    frames(&f, 2);
    bool restored = maximized && s->maximized < 0 &&
                    panel_elements(s, "AreaHeader", ".area-head", heads) == shown;
    if (!(slots && listed && selected && closed && playing && restored)) {
        printf("  %d %d %d %d %d %d\n", slots, listed, selected, closed, playing, restored);
    }
    test_check(
        slots && listed && selected && closed && playing && restored,
        "Studio's authored top bar and area headers host their native "
        "slots and run tab, transport and area commands"
    );
    fixture_free(&f);
}

/* Global shortcuts remain available after authored toolbar controls take Rml
   focus, while an authored text input keeps its keys. */
static void toolbar_shortcuts_keep_text_focus()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 4);
    rml_context *context = rml_backend_context(s->backend);
    rml_element *button = studio_element(s, ".side-pane");
    assert(context && button);
    press(s, button);
    frames(&f, 1);
    studio_command_run(s, STUDIO_COMMAND_TOGGLE_SIDE_PANE);
    frames(&f, 1);
    /* Toolbar actions can remount the authored header; focus the button
       from the reconciled document before sending the shortcut. */
    button = studio_element(s, ".side-pane");
    assert(button);
    rml_element_focus(button, true);
    frames(&f, 1);
    bool button_allowed = studio_ui_shortcut_focus(s);
    bool button_keyboard = kit_wants_keyboard(s->kit);
    bool button_focus = button_allowed && button_keyboard;

    SDL_Keymod primary = s->mac ? SDL_KMOD_GUI : SDL_KMOD_CTRL;
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool button_shortcut = kit_command_is_open(s->kit);
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 2);
    bool dismissed = !kit_command_is_open(s->kit);

    rml_element *tab = studio_element(s, ".doc-tab .face");
    assert(tab);
    press(s, tab);
    frames(&f, 1);
    tab = studio_element(s, ".doc-tab");
    assert(tab && rml_element_focus(tab, true));
    frames(&f, 1);
    bool tab_focus = rml_context_focus_element(context) == tab &&
                     studio_ui_shortcut_focus(s) && kit_wants_keyboard(s->kit);
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool tab_shortcut = kit_command_is_open(s->kit);
    key_event(SDL_SCANCODE_ESCAPE, true);
    key_event(SDL_SCANCODE_ESCAPE, false);
    frames(&f, 2);

    rml_element *input = studio_element(s, "input[placeholder=Filter]");
    assert(input);
    bool focused_input = rml_element_focus(input, true);
    frames(&f, 1);
    bool input_keyboard = kit_wants_keyboard(s->kit);
    bool input_allowed = studio_ui_shortcut_focus(s);
    bool input_focus = focused_input && rml_context_focus_element(context) == input &&
                       input_keyboard && !input_allowed;
    /* A plain key stays in the field: Space types, not Play. */
    key_event(SDL_SCANCODE_SPACE, true);
    key_event(SDL_SCANCODE_SPACE, false);
    frames(&f, 2);
    rml_element *input_focus_after = rml_context_focus_element(context);
    bool input_protected = !app_scene_authoring_runtime_state(s->scene).playing &&
                           input_focus_after == input && kit_wants_keyboard(s->kit);
    /* A chord the field edits no text with still reaches Studio. */
    key_event_mod(SDL_SCANCODE_K, true, primary, false);
    key_event_mod(SDL_SCANCODE_K, false, primary, false);
    frames(&f, 2);
    bool input_palette = kit_command_is_open(s->kit);
    input_protected &= input_palette;

    if (!(button_focus && button_shortcut && dismissed && tab_focus &&
          tab_shortcut && input_focus && input_protected)) {
        printf("  button allowed %d, keyboard %d\n", button_allowed, button_keyboard);
        printf("  tab focus %d, shortcut %d\n", tab_focus, tab_shortcut);
        printf(
            "  %d %d %d %d %d (open %d, focus %s, input %d, allowed %d, "
            "keyboard %d, attempted %d, typing %d)\n",
            button_focus,
            button_shortcut,
            dismissed,
            input_focus,
            input_protected,
            input_palette,
            input_focus_after ? rml_element_tag_name(input_focus_after) : "none",
            input_focus_after == input,
            input_allowed,
            input_keyboard,
            focused_input,
            s->typing
        );
    }
    test_check(
        button_focus && button_shortcut && dismissed && tab_focus &&
            tab_shortcut && input_focus && input_protected,
        "focused authored toolbar buttons and document tabs allow global "
        "shortcuts, and text inputs keep their keys but pass Studio's other "
        "chords"
    );
    fixture_free(&f);
}

static bool beside(const studio *s)
{
    kit_rect top = s->areas[STUDIO_AREA_TOP].rect,
             bottom = s->areas[STUDIO_AREA_BOTTOM].rect;
    return bottom.w > 0 && fabsf(bottom.y - top.y) < 1 && bottom.x >= top.x + top.w;
}

static bool stacked(const studio *s)
{
    kit_rect top = s->areas[STUDIO_AREA_TOP].rect,
             bottom = s->areas[STUDIO_AREA_BOTTOM].rect;
    return bottom.w > 0 && fabsf(bottom.x - top.x) < 1 && bottom.y >= top.y + top.h;
}

static bool side_shown(const studio *s)
{
    return s->areas[STUDIO_AREA_OUTLINER].rect.w > 0 &&
           s->areas[STUDIO_AREA_PROPERTIES].rect.w > 0;
}

static void select_tab(studio_fixture *f, int tab)
{
    studio_command_run_with(f->studio, STUDIO_COMMAND_SELECT_TAB, tab);
    frames(f, 3);
}

/* Opening documents and changing tabs keep the selected split mode. Each mode
   remembers its arrangement, and switching modes preserves authoring state. */
static void layout_modes_are_explicit()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    studio_layout *vertical = &s->prefs.layouts[STUDIO_WORKSPACE_VERTICAL_SPLIT];
    studio_layout *horizontal = &s->prefs.layouts[STUDIO_WORKSPACE_HORIZONTAL_SPLIT];
    vertical->side_width = 416;
    vertical->main_split = 0.66f;
    s->prefs.code_apply_live = false;
    s->applied = s->prefs;
    frames(&f, 2);
    select_instance(f.scene, 0);
    int actor = f.scene->selected;

    char path[PATH_MAX];
    assert(realpath(BANK, path) && studio_open_ui_source(s, path, 1));
    frames(&f, 4);
    bool opened_kept_mode = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT;
    int bank = tab_named(f.scene, "Bank"), scene = bank == 0 ? 1 : 0;
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 3);
    kit_rect r;
    bool horizontal_shown = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                            beside(s) &&
                            kit_find(s->kit, "app/workspace/main/editors/bottom/body/code/text", &r) &&
                            !horizontal->drawer_open && vertical->drawer_open &&
                            vertical->editors[STUDIO_AREA_DRAWER] == STUDIO_EDITOR_CODE;
    ui_buffer *buffer = studio_ui_buffer(s);
    studio_designer *designer = designer_state(s, &buffer);
    assert(buffer && designer && acount(designer->rml->elements) > 0);
    designer_select(s, 0);
    int length = (int)strlen(ui_buffer_text(buffer, UI_PART_RML));
    text_edit edit = { length, length, "<!-- workspace -->\n" };
    ui_buffer_edit(buffer, UI_PART_RML, &edit, 1);
    frames(&f, 2);
    kit_code_editor *code = s->code.editor;
    int cursor = kit_code_cursor(code);
    studio_runtime(s, APP_RUNTIME_PLAY);
    frames(&f, 2);

    select_tab(&f, scene);
    bool tab_kept_horizontal = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                               beside(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT);
    frames(&f, 3);
    bool vertical_shown = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT &&
                          stacked(s) && vertical->side_width == 416 &&
                          vertical->main_split == 0.66f && vertical->drawer_open;
    select_tab(&f, bank);
    bool tab_kept_vertical = s->prefs.workspace == STUDIO_WORKSPACE_VERTICAL_SPLIT &&
                             stacked(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 3);
    bool returned = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT &&
                    beside(s) && studio_ui_buffer(s) == buffer &&
                    s->code.editor == code && kit_code_cursor(code) == cursor &&
                    designer->selected == 0 && f.scene->selected == actor &&
                    app_scene_authoring_runtime_state(f.scene).playing;

    rml_element *toggles[2];
    assert(panel_elements(s, "TopBar", ".side-pane", toggles) == 1);
    press(s, rml_element_query_selector(toggles[0], ".icon"));
    frames(&f, 30);
    bool side_hidden = !horizontal->side_open && !side_shown(s) && vertical->side_open;
    rml_element *area_heads[8];
    bool side_headers_hidden = panel_elements(s, "AreaHeader", ".area-head", area_heads) ==
                               2;
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_VERTICAL_SPLIT);
    frames(&f, 30);
    bool side_kept = vertical->side_open && side_shown(s);
    studio_command_run(s, STUDIO_COMMAND_WORKSPACE_HORIZONTAL_SPLIT);
    frames(&f, 30);
    bool side_remembered = !horizontal->side_open && !side_shown(s);
    studio_show_editor(s, STUDIO_EDITOR_OUTLINER);
    frames(&f, 30);
    bool side_reopened = horizontal->side_open && side_shown(s);
    int headers = panel_elements(s, "AreaHeader", ".area-head", area_heads);
    char area_name[32], alias[32];
    snprintf(area_name, sizeof area_name, "editor.areas[%d]", STUDIO_AREA_OUTLINER);
    bool top_header_restored = false;
    for (int i = 0; i < headers; i++) {
        rml_element *host = rml_element_parent(area_heads[i]);
        if (rml_element_attribute(host, "data-alias-area", alias, sizeof alias) > 0 &&
            !strcmp(alias, area_name)) {
            rml_vec2 size = rml_element_box_size(area_heads[i], RML_BOX_BORDER);
            top_header_restored = rml_element_is_visible(area_heads[i], true) &&
                                  size.y > 0;
            break;
        }
    }
    bool side_header_restored = headers == 4 && top_header_restored;

    horizontal->main_split = 0.75f;
    studio_command_run(s, STUDIO_COMMAND_RESET_LAYOUT);
    frames(&f, 2);
    bool reset_one = horizontal->main_split == 0.5f &&
                     vertical->main_split == 0.66f && vertical->side_width == 416;
    studio_history(s, false);
    bool undone = !strstr(ui_buffer_text(buffer, UI_PART_RML), "<!-- workspace -->");
    studio_history(s, true);
    bool redone = strstr(ui_buffer_text(buffer, UI_PART_RML), "<!-- workspace -->") !=
                  NULL;
    bool playing = app_scene_authoring_runtime_state(f.scene).playing;

    select_tab(&f, scene);
    bool tab_kept_mode = s->prefs.workspace == STUDIO_WORKSPACE_HORIZONTAL_SPLIT;
    studio_runtime(s, APP_RUNTIME_STOP);
    frames(&f, 1);
    if (!(opened_kept_mode && horizontal_shown && tab_kept_horizontal && vertical_shown &&
          tab_kept_vertical && returned && side_hidden && side_headers_hidden &&
          side_kept && side_remembered && side_reopened && side_header_restored &&
          reset_one && undone && redone && playing && tab_kept_mode)) {
        printf(
            "  %d %d %d %d %d %d | %d %d %d %d %d %d | %d %d %d %d %d\n",
            opened_kept_mode,
            horizontal_shown,
            tab_kept_horizontal,
            vertical_shown,
            tab_kept_vertical,
            returned,
            side_hidden,
            side_headers_hidden,
            side_kept,
            side_remembered,
            side_reopened,
            side_header_restored,
            reset_one,
            undone,
            redone,
            playing,
            tab_kept_mode
        );
    }
    test_check(
        opened_kept_mode && horizontal_shown && tab_kept_horizontal &&
            vertical_shown && tab_kept_vertical && returned,
        "opening documents and changing tabs keep the selected split mode"
    );
    test_check(
        side_hidden && side_headers_hidden && side_kept && side_remembered &&
            side_reopened && side_header_restored,
        "the top bar hides the side pane per mode and restores its editor "
        "headers"
    );
    test_check(
        reset_one && undone && redone && playing && tab_kept_mode,
        "Reset Layout resets the active mode only; switching keeps "
        "history and playback"
    );
    fixture_free(&f);
}

/* Each area is a document of its own (workstream F5): text rewritten in the
   Outliner lays out none of the others. The guest Studio, run in the Scene
   area, places its own areas over theirs the same way, and stops. */
static void area_documents()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    frames(&f, 6);
    rml_context *context = rml_backend_context(s->backend);
    rml_element *name = test_query(context, "#outliner-list .tree-row .name");
    rml_element *outliner = name ? rml_element_owner_document(name) : NULL;
    int documents = rml_context_document_count(context);
    for (int i = 0; outliner && i < documents; i++) {
        rml_element *document = rml_context_document(context, i);
        if (document != outliner) {
            bench_probe(document);
        }
    }
    frames(&f, 30);
    uint64_t before = bench_layouts();
    rml_element_set_inner_rml(name, "Renamed in place");
    rml_backend_update(s->backend, 0);
    test_check(
        outliner && documents >= 8 && bench_layouts() == before,
        "text rewritten in the Outliner lays out the Outliner's document "
        "and no other area's, bar's or the layer's"
    );
    studio_guest_start(s);
    frames(&f, 8);
    kit_rect top = { 0 };
    bool guest = s->guest &&
                 kit_find(s->guest->kit, "app/workspace/main/editors/top", &top) &&
                 top.w > 0 && top.h > 0 &&
                 rml_context_document_count(rml_backend_context(s->guest->backend)) >= 8;
    studio_guest_stop(s);
    frames(&f, 2);
    test_check(
        guest && !s->guest,
        "the guest Studio lays its areas over their places in documents of "
        "its own, and stops"
    );
    fixture_free(&f);
}

/* The text of an element's .label, or "". */
static void label_text(rml_element *el, char *out, size_t size)
{
    rml_element *label = el ? rml_element_query_selector(el, ".label") : NULL;
    rml_element *node = label ? rml_element_first_child(label) : NULL;
    out[0] = '\0';
    if (node && !strcmp(rml_element_tag_name(node), "#text")) {
        rml_element_text(node, out, size);
    }
}

/* The shown element in `menu` matching `selector` labeled `name`, or NULL. */
static rml_element *labeled(rml_element *menu, const char *selector, const char *name)
{
    rml_element *items[64];
    int count = menu ? rml_element_query_selector_all(menu, selector, items, 64) : 0;
    for (int i = 0; i < count && i < 64; i++) {
        char label[64];
        label_text(items[i], label, sizeof label);
        if (!strcmp(label, name) && rml_element_is_visible(items[i], true)) {
            return items[i];
        }
    }
    return NULL;
}

/* A point inside `el` outside `document`'s box: where it draws over another
   document. False when it has none. */
static bool beyond(rml_element *el, rml_element *document, rml_vec2 *point)
{
    rml_vec2 at = rml_element_absolute_offset(el, RML_BOX_BORDER);
    rml_vec2 size = rml_element_box_size(el, RML_BOX_BORDER);
    rml_vec2 box_at = rml_element_absolute_offset(document, RML_BOX_BORDER);
    rml_vec2 box = rml_element_box_size(document, RML_BOX_BORDER);
    for (float y = at.y + 4; y < at.y + size.y - 4; y += 4) {
        if (y < box_at.y || y > box_at.y + box.y) {
            *point = (rml_vec2){ at.x + size.x / 2, y };
            return true;
        }
    }
    return false;
}

static bool inside(rml_element *el, const rml_element *ancestor)
{
    for (; el; el = rml_element_parent(el)) {
        if (el == ancestor) {
            return true;
        }
    }
    return false;
}

/* Each area header's editor type menu is an authored Menu over
   choices.editors: it draws over the areas beside its own, marks the area's
   editor, and a chosen item shows that editor there. The Behavior header's
   View menu toggles a view preference and stays open; Escape closes it. */
static void area_menus()
{
    studio_fixture f = vault_open();
    studio *s = f.studio;
    s->maximized = -1;
    frames(&f, 6);
    rml_context *context = rml_backend_context(s->backend);
    rml_element *heads[8];
    int shown = panel_elements(s, "AreaHeader", ".area-head", heads);
    bool over = false, current = true;
    for (int i = 0; i < shown; i++) {
        press(s, rml_element_query_selector(heads[i], ".editor-type"));
        frames(&f, 3);
        rml_element *menu = rml_element_query_selector(heads[i], "#editor-types");
        char label[64];
        label_text(rml_element_query_selector(menu, "[data-current]"), label, sizeof label);
        current &= popover_open(menu) && label[0];
        rml_vec2 point;
        over |= beyond(menu, rml_element_owner_document(menu), &point) &&
                inside(rml_context_element_at_point(context, point, NULL), menu);
        press(s, rml_element_query_selector(heads[i], ".editor-type"));
        frames(&f, 2);
    }
    studio_layout *layout = studio_active_layout(s);
    studio_editor before[STUDIO_AREA_COUNT];
    memcpy(before, layout->editors, sizeof before);
    press(s, rml_element_query_selector(heads[0], ".editor-type"));
    frames(&f, 3);
    rml_element *members = labeled(
        rml_element_query_selector(heads[0], "#editor-types"),
        "[role=menuitem]",
        "Members"
    );
    if (members) {
        press(s, members);
        frames(&f, 3);
    }
    int shows = 0, changed = 0;
    for (int a = 0; a < STUDIO_AREA_COUNT; a++) {
        shows += layout->editors[a] == STUDIO_EDITOR_MEMBERS;
        changed += layout->editors[a] != before[a];
    }
    test_check(
        shown >= 2 && current && over && shows == 1 && changed == 1 &&
            !popover_top(context),
        "an area header's editor type menu marks its editor, draws over the "
        "next area, and its item shows that editor in the area"
    );
    memcpy(layout->editors, before, sizeof before);
    frames(&f, 4);
    rml_element *view = studio_element(s, "[data-ui=GraphView] button");
    bool compact = s->prefs.compact_nodes;
    if (view) {
        press(s, view);
        frames(&f, 3);
    }
    rml_element *menu = studio_element(s, "#graph-view");
    rml_element *toggle = labeled(menu, ".menu-check", "Compact Nodes");
    if (toggle) {
        press(s, toggle);
        frames(&f, 3);
    }
    bool toggled = toggle && s->prefs.compact_nodes != compact && popover_open(menu);
    tap(SDL_SCANCODE_ESCAPE);
    frames(&f, 2);
    test_check(
        view && toggled && !popover_open(menu),
        "the Behavior header's View menu toggles a view preference, stays "
        "open, and closes on Escape"
    );
    s->prefs.compact_nodes = compact;
    fixture_free(&f);
}

void studio_documents_shell_checks()
{
    host_ui_refresh_keeps_input_immediate();
    performance_snapshot_retains_intermediate_frames();
    status_bar_follows_studio();
    palette_finds_settings();
    palette_recent_and_prefixes();
    wheel_scrolls_lists();
    wheel_zooms_canvases();
    shortcut_repeat_policy();
    keys_follow_pointer();
    readout_follows_camera();
    chrome_follows_studio();
    layout_modes_are_explicit();
    toolbar_shortcuts_keep_text_focus();
    area_documents();
    area_menus();
}
