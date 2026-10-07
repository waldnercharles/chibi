/* Debugging: jumping to a node, the open behavior's compile problems, the
 * playing scene's runtime errors (Unreal's PIE message log) and a run paused
 * at a breakpoint (Unreal's Blueprint debugger): stepping, the paused node
 * and its call stack. */
#include "assets/code_print.h"
#include "editor/studio/studio_graph_internal.h"
#include "editor/ui/labels.h"
#include "graph/document/library.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    char name[256] = "Event Graph";
    if (g) {
        studio_code_graph_label(s, g, name, sizeof name);
    }
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
    for (int i = 0; i < acount(e->asset_catalog); i++) {
        const behavior_asset *asset = e->asset_catalog[i];
        if (strcmp(asset->name, at->asset)) {
            continue;
        }
        const graph *graph = behavior_asset_find_graph(asset, at->graph_id);
        if (graph && code_print_reactive_event(asset, graph, n, out, size)) {
            return;
        }
    }
    if (open) {
        char *expression = open->desc == &NODE_BehaviorExpression
                             ? studio_code_expression(s, open)
                             : NULL;
        if (expression) {
            snprintf(out, size, "%s", expression);
            free(expression);
            return;
        }
        ed_node_title_in(e, e->asset, g, open, out, size);
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
   which the next applied edit replaces. What the game prints goes to
   Output's Log under its behavior's name, as Print String reaches Unreal's
   Output Log while playing; failures go to Runtime. */
static void collect_errors(studio *s, game_behavior_world *world)
{
    editor *log = s->scene->graph_editor;
    for (int i = 0; i < game_behavior_log_count(world); i++) {
        game_behavior_log_info info;
        int read = game_behavior_log_info_at(world, i, &info);
        assert(read == 0);
        (void)read;
        if (!info.failure) {
            if (log) {
                editor_log_print(log, info.asset_name, info.message);
            }
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
        studio_code_reveal_node(s, &s->pause.at);
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
const double studio_speeds[] = { STUDIO_SPEED_MIN, 0.25, 0.5, 1, STUDIO_SPEED_MAX };
const int studio_speed_count = sizeof studio_speeds / sizeof *studio_speeds;

void studio_speed_label(double speed, char *buf, size_t size)
{
    snprintf(buf, size, "%g\xc3\x97", speed);
}

void studio_speed_text(const studio *s, char *buf, size_t size)
{
    double speed = app_scene_authoring_time_scale(s->scene);
    buf[0] = '\0';
    if (speed != 1) {
        studio_speed_label(speed, buf, size);
    }
}

void studio_speed_step(studio *s, int direction)
{
    double speed = app_scene_authoring_time_scale(s->scene),
           next = direction ? speed : 1;
    /* The last match is the nearest step: up from the top, down from the bottom. */
    for (int i = 0; i < studio_speed_count; i++) {
        double up = studio_speeds[studio_speed_count - 1 - i],
               down = studio_speeds[i];
        if (direction > 0 && up > speed) {
            next = up;
        }
        if (direction < 0 && down < speed) {
            next = down;
        }
    }
    app_scene_authoring_set_time_scale(s->scene, next);
}

/* ---------- Call stack ---------- */

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
    studio_code_graph_label(s, g, row.graph, sizeof row.graph);
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
