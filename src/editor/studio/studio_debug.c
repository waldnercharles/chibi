/* Debugging: jumping to a node, the open behavior's compile problems, the
 * playing scene's runtime errors (Unreal's PIE message log) and a run paused
 * at a breakpoint (Unreal's Blueprint debugger): stepping, the paused node
 * and its call stack. */
#include "editor/studio/studio_graph_internal.h"
#include "editor/ui/labels.h"
#include "graph/document/library.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---------- Jumping to a node ---------- */

static int behavior_area(const studio_layout *layout)
{
    int area = -1;
    for (int i = 0; i < STUDIO_AREA_COUNT; i++) {
        area = layout->editors[i] == STUDIO_EDITOR_BEHAVIOR ? i : area;
    }
    return area;
}

/* A Behavior area shows the graph. If the active split has none, the bottom
   area becomes one; a different maximized area gives way to it. */
static void show_behavior_area(studio *s)
{
    int area = behavior_area(studio_active_layout(s));
    if (area < 0) {
        area = STUDIO_AREA_BOTTOM;
        studio_active_layout(s)->editors[area] = STUDIO_EDITOR_BEHAVIOR;
    }
    if (s->maximized >= 0 && s->maximized != area) {
        s->maximized = -1;
    }
}

/* The graph editor holds `asset`: a behavior, or a system's graphs. */
static bool behavior_open(const studio *s, const char *asset)
{
    const char *open = s->scene->graph_document_id;
    return open && !strcmp(open, asset);
}

/* Opens the behavior and graph `at` names in a Behavior area. */
static bool open_graph(studio *s, const studio_node_ref *at)
{
    if ((!behavior_open(s, at->asset) &&
         app_scene_authoring_open_behavior(s->scene, at->asset) != 0) ||
        editor_open_graph(sg_graph_editor(s), at->graph_id) != 0) {
        return false;
    }
    show_behavior_area(s);
    return true;
}

/* The node `at` names in the open graph, or the node it is folded into. */
static node *shown_node(const studio *s, const studio_node_ref *at)
{
    graph *gr = sg_active_graph(s);
    node *n = gr && at->node_id ? graph_node_by_id(gr, at->node_id) : NULL;
    return n ? graph_inline_owner(gr, n) : NULL;
}

void studio_reveal(studio *s, const studio_node_ref *at)
{
    if (!open_graph(s, at)) {
        char message[192];
        snprintf(message, sizeof message, "Could not open %s", at->asset);
        studio_report(s, message);
        return;
    }
    node *n = shown_node(s, at);
    if (n) {
        sg_select_only(sg_graph_editor(s), n);
        s->graph.reveal = n->id;
        s->graph.reveal_frame = true;
    }
}

void studio_node_place(const studio *s, const studio_node_ref *at, char *out, size_t size)
{
    const editor *e = sg_graph_editor(s);
    graph *g = behavior_open(s, at->asset)
                 ? behavior_asset_find_graph(e->asset, at->graph_id)
                 : NULL;
    node *n = g && at->node_id ? graph_node_by_id(g, at->node_id) : NULL;
    const char *name = g && g->name && g->name[0] ? g->name : "Event Graph";
    if (!n) {
        snprintf(out, size, "%s", g ? name : at->asset);
        return;
    }
    char title[128];
    ed_node_title(e, graph_inline_owner(g, n), title, sizeof title);
    snprintf(out, size, "%s \xe2\x80\xba %s", name, title);
}

/* ---------- Where running nodes were authored ---------- */

/* A running behavior's graphs include copies of the libraries it calls. */
static studio_node_ref authored(const char *behavior, const graph *g, uint64_t node_id)
{
    graph_node_source source = graph_node_authored(g, node_id);
    studio_node_ref at = { .graph_id = source.graph_id, .node_id = source.node_id };
    snprintf(at.asset, sizeof at.asset, "%s", source.asset ? source.asset : behavior);
    return at;
}

/* A behavior compiles linked: with copies of the libraries it calls and of
   its macros. */
studio_node_ref studio_problem_at(const studio *s, const compile_diagnostic *d)
{
    const editor *e = sg_graph_editor(s);
    const char *behavior = s->scene->graph_document_id ? s->scene->graph_document_id
                                                       : "";
    if (!e->compiled) {
        studio_node_ref at = { .graph_id = d->graph_id, .node_id = d->node_id };
        snprintf(at.asset, sizeof at.asset, "%s", behavior);
        return at;
    }
    const graph *g = behavior_asset_find_graph(e->compiled, d->graph_id);
    assert(g);
    return authored(behavior, g, d->node_id);
}

static const graph *graph_holding(const behavior_asset *definition, const node *n)
{
    for (int i = 0; i < acount(definition->graphs); i++) {
        if (graph_node_by_id(definition->graphs[i], n->id) == n) {
            return definition->graphs[i];
        }
    }
    return NULL;
}

static bool locate(const behavior_asset *definition, const char *behavior, const node *n, studio_node_ref *out)
{
    const graph *g = graph_holding(definition, n);
    if (g) {
        *out = authored(behavior, g, n->id);
    }
    return g != NULL;
}

/* The canvas title while its behavior is open, else what the node does. */
static void node_label(const studio *s, const studio_node_ref *at, const node *n, char *out, size_t size)
{
    const editor *e = sg_graph_editor(s);
    graph *g = !strcmp(e->asset->name, at->asset)
                 ? behavior_asset_find_graph(e->asset, at->graph_id)
                 : NULL;
    node *open = g ? graph_node_by_id(g, at->node_id) : NULL;
    if (open) {
        ed_node_title(e, open, out, size);
    } else if (n->reference_name && n->reference_name[0]) {
        editor_identifier_label(n->reference_name, out, size);
    } else {
        editor_node_label(n->desc, out, size);
    }
}

/* ---------- Runtime errors ---------- */

static game_behavior_world *running_world(const studio *s)
{
    app_scene_authoring *scene = s->scene;
    return scene->active_runtime_target == APP_RUNTIME_PREVIEW
             ? game_prototype_preview_behaviors(scene->prototype_preview)
             : scene->live->behaviors;
}

static const behavior_asset *definition_named(const game_behavior_world *world, const char *name)
{
    for (int i = 0; i < game_behavior_asset_count(world); i++) {
        if (!strcmp(game_behavior_asset_name_at(world, i), name)) {
            return game_behavior_asset_definition_at(world, i);
        }
    }
    return NULL;
}

static bool same_error(const studio_runtime_error *a, const studio_runtime_error *b)
{
    return !strcmp(a->behavior, b->behavior) &&
           !strcmp(a->at.asset, b->at.asset) && a->at.graph_id == b->at.graph_id &&
           a->at.node_id == b->at.node_id && !strcmp(a->message, b->message);
}

static void add_error(studio *s, const studio_runtime_error *error)
{
    for (int i = 0; i < acount(s->runtime_errors); i++) {
        if (same_error(&s->runtime_errors[i], error)) {
            s->runtime_errors[i].count++;
            return;
        }
    }
    apush(s->runtime_errors, *error);
}

/* The log is drained each frame: its nodes belong to the running definition,
   which the next applied edit replaces. */
static void collect_errors(studio *s, game_behavior_world *world)
{
    for (int i = 0; i < game_behavior_log_count(world); i++) {
        game_behavior_log_info info;
        int read = game_behavior_log_info_at(world, i, &info);
        assert(read == 0);
        (void)read;
        if (!info.failure) {
            continue;
        }
        studio_runtime_error error = { .count = 1 };
        snprintf(error.behavior, sizeof error.behavior, "%s", info.asset_name);
        snprintf(error.at.asset, sizeof error.at.asset, "%s", info.asset_name);
        snprintf(error.message, sizeof error.message, "%s", info.message);
        const behavior_asset *definition = definition_named(world, info.asset_name);
        if (info.origin && definition &&
            locate(definition, info.asset_name, info.origin, &error.at)) {
            node_label(s, &error.at, info.origin, error.node, sizeof error.node);
        }
        add_error(s, &error);
    }
    game_behavior_clear_logs(world);
}

/* ---------- Compile problems ---------- */

static uint64_t revision_key(const studio *s)
{
    const editor *e = sg_graph_editor(s);
    uint64_t key = 14695981039346656037ull;
    for (const char *c = s->scene->graph_document_id; *c; c++) {
        key = (key ^ (unsigned char)*c) * 1099511628211ull;
    }
    key = (key ^ editor_edit_revision(e)) * 1099511628211ull;
    for (int i = 0; i < acount(e->asset->graphs); i++) {
        key = (key ^ graph_topology_revision(e->asset->graphs[i])) * 1099511628211ull;
    }
    return key;
}

/* The open behavior compiles again whenever its graphs change, so Problems
   and node badges stay current without a Compile button. */
static void compile_on_change(studio *s)
{
    if (s->scene->graph_document_kind != APP_EDITOR_DOCUMENT_BEHAVIOR ||
        !studio_graph_active(s) || revision_key(s) == s->compiled_key) {
        return;
    }
    editor_command_apply(sg_graph_editor(s), (editor_command){ .kind = EDITOR_COMMAND_COMPILE });
    s->compiled_key = revision_key(s);
}

/* ---------- The paused run ---------- */

static flow_run *paused_run(const studio *s, game_behavior_attachment_info *info)
{
    return game_behavior_paused_run(running_world(s), info);
}

/* A new pause, or a step, shows its node, as Unreal jumps to it, and makes its
   instance the debug target so pins read its values; a new pause in the scene
   also selects its object, as Unreal's debugger selects the instance a
   breakpoint stops in. The target's pin comes back once the run continues. */
static void follow_pause(studio *s)
{
    app_scene_authoring *scene = s->scene;
    game_behavior_attachment_info info;
    flow_run *run = paused_run(s, &info);
    s->pause.active = run != NULL;
    if (!run) {
        if (s->pause.holding) {
            s->pause.holding = false;
            app_scene_authoring_set_debug_pinned(scene, s->pause.pinned);
        }
        s->pause.run = s->pause.node = NULL;
        return;
    }
    const node *current = flow_run_current_node(run);
    if (!current || !locate(info.definition, info.asset_name, current, &s->pause.at)) {
        s->pause.at = (studio_node_ref){ 0 };
    }
    /* A step can leave a call without running a node. */
    unsigned long steps = flow_run_steps(run);
    if (run == s->pause.run && current == s->pause.node && steps == s->pause.steps) {
        return;
    }
    s->pause.run = run;
    s->pause.node = current;
    s->pause.steps = steps;
    if (!s->pause.holding) {
        s->pause.holding = true;
        s->pause.pinned = scene->debug_pinned;
        if (scene->active_runtime_target == APP_RUNTIME_SCENE) {
            app_scene_authoring_select_entity(scene, info.entity);
        }
    }
    if (s->pause.at.asset[0] && open_graph(s, &s->pause.at)) {
        node *n = shown_node(s, &s->pause.at);
        s->graph.reveal = n ? n->id : 0;
        s->graph.reveal_frame = false;
    }
    app_scene_authoring_set_debug_target(scene, scene->active_runtime_target, info.entity, info.asset_name);
}

bool studio_debug(studio *s, studio_debug_command command)
{
    game_behavior_attachment_info info;
    flow_run *run = paused_run(s, &info);
    if (!run) {
        return false;
    }
    switch (command) {
        case STUDIO_RESUME: flow_run_resume(run); break;
        case STUDIO_STEP_OVER: flow_run_step_over(run); break;
        case STUDIO_STEP_INTO: flow_run_step(run); break;
        case STUDIO_STEP_OUT: flow_run_step_out(run); break;
    }
    return true;
}

void studio_debug_frame(studio *s)
{
    app_scene_authoring *scene = s->scene;
    bool session = runtime_session(scene);
    if (session && !s->session) {
        asetlen(s->runtime_errors, 0);
    }
    s->session = session;
    collect_errors(s, scene->live->behaviors);
    collect_errors(s, game_prototype_preview_behaviors(scene->prototype_preview));
    compile_on_change(s);
    follow_pause(s);
}

/* ---------- Break when spawned ---------- */

void studio_toggle_spawn_break(studio *s, const char *prototype)
{
    bool on = !app_scene_authoring_spawn_break(s->scene, prototype);
    app_scene_authoring_set_spawn_break(s->scene, prototype, on);
    char report[160];
    snprintf(report, sizeof report, on ? "Play pauses when a %s spawns" : "A %s spawning no longer pauses Play", prototype);
    studio_report(s, report);
}

void studio_spawn_break_item(studio *s, const char *prototype)
{
    kit_menu_item_desc d = {
        .label = "Break When Spawned",
        .checked = app_scene_authoring_spawn_break(s->scene, prototype),
    };
    if (kit_menu_item(s->kit, "spawn-break", &d)) {
        studio_toggle_spawn_break(s, prototype);
    }
}

/* ---------- Playback speed ---------- */

/* Unity's Time.timeScale and Unreal's slomo as steps, as Chrome DevTools
   offers its animations' speeds. */
static const double SPEEDS[] = { STUDIO_SPEED_MIN, 0.25, 0.5, 1, STUDIO_SPEED_MAX };
enum
{
    SPEED_COUNT = sizeof SPEEDS / sizeof *SPEEDS
};

static void speed_label(double speed, char *buf, size_t size)
{
    snprintf(buf, size, "%g\xc3\x97", speed);
}

void studio_speed_text(const studio *s, char *buf, size_t size)
{
    double speed = app_scene_authoring_time_scale(s->scene);
    buf[0] = '\0';
    if (speed != 1) {
        speed_label(speed, buf, size);
    }
}

void studio_speed_step(studio *s, int direction)
{
    double speed = app_scene_authoring_time_scale(s->scene),
           next = direction ? speed : 1;
    /* The last match is the nearest step: up from the top, down from the bottom. */
    for (int i = 0; i < SPEED_COUNT; i++) {
        double up = SPEEDS[SPEED_COUNT - 1 - i], down = SPEEDS[i];
        if (direction > 0 && up > speed) {
            next = up;
        }
        if (direction < 0 && down < speed) {
            next = down;
        }
    }
    app_scene_authoring_set_time_scale(s->scene, next);
}

void studio_speed_menu(studio *s)
{
    kit_menu_group(s->kit, "speed", "Playback Speed");
    studio_command_item(s, STUDIO_COMMAND_SLOWER);
    studio_command_item(s, STUDIO_COMMAND_FASTER);
    studio_command_item(s, STUDIO_COMMAND_NORMAL_SPEED);
}

/* Beside Play, as UI Scale keeps its percentage in the status bar: the
   speed, marked while it is not 1×, and a menu of its steps. */
void studio_speed_control(studio *s)
{
    kit *k = s->kit;
    double speed = app_scene_authoring_time_scale(s->scene);
    char label[16];
    speed_label(speed, label, sizeof label);
    kit_button_desc d = {
        .label = label,
        .variant = KIT_SUBTLE,
        .cls = "speed",
        .tip = "Playback Speed",
        .description = "Game time per second. Behaviors, physics and "
                       "effects slow alike; Stop returns to 1\xc3\x97.",
        .flags = speed != 1 ? KIT_ON : 0,
    };
    if (kit_button(k, "speed", &d)) {
        kit_popup_open(k, "speed-menu");
    }
    if (!kit_popup_begin(k, "speed-menu", "menu")) {
        return;
    }
    studio_speed_menu(s);
    kit_menu_separator(k, "s1");
    for (int i = 0; i < SPEED_COUNT; i++) {
        char key[8], text[16];
        snprintf(key, sizeof key, "p%d", i);
        speed_label(SPEEDS[i], text, sizeof text);
        kit_menu_item_desc preset = { .label = text, .checked = speed == SPEEDS[i] };
        if (kit_menu_item(k, key, &preset)) {
            app_scene_authoring_set_time_scale(s->scene, SPEEDS[i]);
        }
    }
    kit_popup_end(k);
}

/* ---------- Transport and call stack ---------- */

/* Visual Studio's keys, which Unreal follows. */
static const char *const DEBUG_IDS[] = { "resume", "over", "into", "out" };
static const studio_command STUDIO_DEBUG_COMMANDS[] = {
    STUDIO_COMMAND_RESUME,
    STUDIO_COMMAND_STEP_OVER,
    STUDIO_COMMAND_STEP_INTO,
    STUDIO_COMMAND_STEP_OUT,
};

void studio_debug_steps(studio *s)
{
    for (int c = STUDIO_STEP_OVER; c <= STUDIO_STEP_OUT; c++) {
        studio_command command = STUDIO_DEBUG_COMMANDS[c];
        studio_command_info info = studio_command_describe(s, command);
        kit_button_desc d = { .icon = info.icon,
                              .tip = info.label,
                              .shortcut = info.shortcut };
        if (kit_button(s->kit, DEBUG_IDS[c], &d)) {
            studio_command_run(s, command);
        }
    }
}

void studio_debug_menu(studio *s)
{
    for (int c = STUDIO_RESUME; c <= STUDIO_STEP_OUT; c++) {
        studio_command command = STUDIO_DEBUG_COMMANDS[c];
        studio_command_info info = studio_command_describe(s, command);
        kit_menu_item_desc d = { .label = info.menu,
                                 .icon = info.icon,
                                 .shortcut = info.shortcut,
                                 .checked = -1,
                                 .disabled = !studio_command_enabled(s, command) };
        if (kit_menu_item(s->kit, DEBUG_IDS[c], &d)) {
            studio_command_run(s, command);
        }
    }
}

typedef struct
{
    studio_node_ref at;
    char label[128];
    char graph[96];
} stack_row;

static void push_row(const studio *s, stack_row **rows, const char *behavior, const graph *g, const node *n)
{
    stack_row row = { .at = authored(behavior, g, n->id) };
    node_label(s, &row.at, n, row.label, sizeof row.label);
    snprintf(row.graph, sizeof row.graph, "%s", g->name && g->name[0] ? g->name : "Event Graph");
    apush(*rows, row);
}

/* The calls in progress, outermost first, then the node the run is paused at. */
static stack_row *call_stack(const studio *s, flow_run *run, const game_behavior_attachment_info *info)
{
    stack_row *rows = NULL;
    int count = 0;
    const flow_call_frame *frames = flow_run_call_stack(run, &count);
    const node *last = NULL;
    for (int i = 0; i < count; i++) {
        const node *n = graph_node_by_id(frames[i].graph, frames[i].node_id);
        if (n && ed_is_graph_call(n)) {
            push_row(s, &rows, info->asset_name, frames[i].graph, n);
            last = n;
        }
    }
    const node *current = flow_run_current_node(run);
    const graph *g = current && current != last
                       ? graph_holding(info->definition, current)
                       : NULL;
    if (g) {
        push_row(s, &rows, info->asset_name, g, current);
    }
    return rows;
}

void studio_debug_call_stack(studio *s)
{
    game_behavior_attachment_info info;
    flow_run *run = s->pause.active ? paused_run(s, &info) : NULL;
    if (!run) {
        return;
    }
    kit *k = s->kit;
    stack_row *rows = call_stack(s, run, &info);
    kit_push(k, "stack", NULL, "call-stack");
    kit_presence(k);
    kit_push(k, "head", NULL, "call-stack-head");
    kit_label(k, "title", NULL, "Call Stack");
    const char *event = flow_run_event_name(run);
    char owner[256];
    snprintf(owner, sizeof owner, event ? "%s \xc2\xb7 %s" : "%s", info.asset_name, event);
    kit_label(k, "owner", "dim", owner);
    kit_pop(k);
    int top = acount(rows) - 1;
    for (int i = top; i >= 0; i--) {
        char key[16];
        snprintf(key, sizeof key, "f%d", i);
        kit_row_desc d = { .label = rows[i].label,
                           .icon = i == top ? KIT_ICON_FRAME_TOP : KIT_ICON_FRAME_CALL,
                           .twisty = -1,
                           .meta = rows[i].graph,
                           .flags = i == top ? KIT_ON : 0 };
        if (kit_row_begin(k, key, &d) & KIT_ROW_CLICK) {
            studio_reveal(s, &rows[i].at);
        }
        kit_row_end(k);
    }
    kit_pop(k);
    afree(rows);
}
