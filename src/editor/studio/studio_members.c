/* Members: the open behavior's graphs, the events it handles, its functions,
 * macros and variables, and the open graph's named values, after Unreal's My
 * Blueprint. A click opens a graph or selects a member for Properties;
 * double-click or F2 renames. A variable dragged onto the canvas places its
 * Get, or its Set with Alt (Unreal), and a function or macro its call. Each
 * member counts its uses across the project, and the count lists them in
 * Find. */
#include "editor/studio/studio_members_internal.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    SECTION_GRAPHS,
    SECTION_EVENTS,
    SECTION_FUNCTIONS,
    SECTION_MACROS,
    SECTION_VARIABLES,
    SECTION_VALUES,
    SECTION_COUNT,
} section;

static const char *const SECTION_LABELS[SECTION_COUNT] = {
    "Graphs", "Events", "Functions", "Macros", "Variables", "Named Values",
};

typedef enum
{
    ROW_SECTION,
    ROW_CATEGORY,
    ROW_GRAPH,
    ROW_VARIABLE,
    ROW_LOCAL,
    ROW_EVENT,
} row_kind;

typedef struct
{
    row_kind kind;
    /* section, graph, variable or local; a category's first variable; an
       event's graph, or -1 for an event definition's own event */
    int index;
    int depth;
    int count; /* a section's or category's members */
    int node;  /* an event's Event node in its graph */
} member_row;

/* A member's uses by role, counted once per build of the asset index. */
struct studio_member_uses
{
    reference_symbol_kind kind;
    uint64_t graph_id, id;
    char event[128];
    int roles[REFERENCE_ROLE_COUNT];
};

/* ---------- Selection ---------- */

behavior_variable *sm_variable(const studio *s)
{
    return s->members.kind == STUDIO_MEMBER_VARIABLE
             ? behavior_asset_find_variable_by_id(
                   sg_graph_editor(s)->asset,
                   s->members.id
               )
             : NULL;
}

graph *sm_graph(const studio *s)
{
    return s->members.kind == STUDIO_MEMBER_GRAPH && s->members.id
             ? behavior_asset_find_graph(sg_graph_editor(s)->asset, s->members.id)
             : NULL;
}

const graph_local *sm_local(const studio *s)
{
    return s->members.kind == STUDIO_MEMBER_LOCAL
             ? graph_local_by_id(sg_active_graph(s), s->members.id)
             : NULL;
}

/* A member that no longer exists, as after Undo, is deselected, and so is any
   member once a graph node is selected: Details shows one or the other. */
static void follow(studio *s)
{
    studio_members *m = &s->members;
    uint64_t node = studio_graph_node(s);
    bool exists = studio_graph_active(s) &&
                  (sm_variable(s) || sm_graph(s) || sm_local(s));
    if (!exists || (node && node != m->shown_node)) {
        m->kind = STUDIO_MEMBER_NONE;
    }
    m->shown_node = node;
}

uint64_t studio_member_shown(studio *s)
{
    follow(s);
    return s->members.kind ? (uint64_t)s->members.kind << 56 ^ s->members.id : 0;
}

static void select_member(studio *s, studio_member_kind kind, uint64_t id)
{
    s->members.kind = kind;
    s->members.id = id;
    ed_clear_selection(sg_graph_editor(s));
    s->members.shown_node = 0;
}

/* A named value selects and frames the node that gives it. */
static void select_local(studio *s, const graph_local *local)
{
    editor *e = sg_graph_editor(s);
    node *source = graph_node_by_id(sg_active_graph(s), local->source_id);
    select_member(s, STUDIO_MEMBER_LOCAL, local->id);
    sg_select_only(e, source);
    s->members.shown_node = source->id;
    sg_frame_nodes(s, true, true);
}

/* ---------- Edits ---------- */

bool sm_record(editor *e, behavior_asset *before, bool ok)
{
    behavior_asset *pending = e->pending_snapshot;
    e->pending_snapshot = before;
    if (ok) {
        ed_commit_snapshot(e);
    } else {
        ed_cancel_snapshot(e);
    }
    e->pending_snapshot = pending;
    return ok;
}

void sm_refused(studio *s, const char *title)
{
    kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_WARNING, .title = title });
}

static bool locked(const studio *s)
{
    return !behavior_asset_mutable(sg_graph_editor(s)->asset);
}

static void start_rename(studio *s, studio_member_kind kind, uint64_t id)
{
    studio_members *m = &s->members;
    if (kind == STUDIO_MEMBER_NONE || locked(s)) {
        return;
    }
    m->rename_kind = kind;
    m->rename_id = id;
    m->rename_focus = true;
}

/* Names a new member after `base`, numbering it past the names in use. */
static void unique_graph_name(const behavior_asset *asset, const char *base, char *out, size_t size)
{
    snprintf(out, size, "%s", base);
    for (int i = 2; behavior_asset_find_graph_by_name(asset, out); i++) {
        snprintf(out, size, "%s %d", base, i);
    }
}

static void unique_variable_name(const behavior_asset *asset, const char *base, char *out, size_t size)
{
    snprintf(out, size, "%s", base);
    for (int i = 2; behavior_asset_find_variable_by_name(asset, out); i++) {
        snprintf(out, size, "%s %d", base, i);
    }
}

/* A new function or macro opens and takes its name at once (Unreal). */
static void add_graph(studio *s, graph_kind kind, const char *base)
{
    editor *e = sg_graph_editor(s);
    char name[128];
    unique_graph_name(e->asset, base, name, sizeof name);
    editor_command create = { .kind = EDITOR_COMMAND_CREATE_GRAPH,
                              .data.create_graph = { kind, name } };
    graph *made = editor_command_apply(e, create) == 0
                    ? behavior_asset_find_graph_by_name(e->asset, name)
                    : NULL;
    if (!made) {
        return;
    }
    editor_open_graph(e, made->graph_id);
    select_member(s, STUDIO_MEMBER_GRAPH, made->graph_id);
    start_rename(s, STUDIO_MEMBER_GRAPH, made->graph_id);
}

/* A new variable takes the selected variable's type, else Boolean (Unreal). */
static void add_variable(studio *s)
{
    editor *e = sg_graph_editor(s);
    const behavior_variable *like = sm_variable(s);
    pin_spec type = like ? sm_variable_pin(like)
                         : (pin_spec){ .type = PIN_BOOL, .type_name = "bool" };
    char name[128];
    unique_variable_name(e->asset, "New Variable", name, sizeof name);
    behavior_asset *before = ed_snapshot(e);
    behavior_variable *v = behavior_asset_add_variable(
        e->asset,
        name,
        type.type,
        type.type_name,
        type.container,
        NULL,
        NULL
    );
    if (sm_record(e, before, v != NULL)) {
        select_member(s, STUDIO_MEMBER_VARIABLE, v->id);
        start_rename(s, STUDIO_MEMBER_VARIABLE, v->id);
    }
}

static void duplicate_variable(studio *s)
{
    editor *e = sg_graph_editor(s);
    const behavior_variable *v = sm_variable(s);
    if (!v || locked(s)) {
        return;
    }
    char name[128];
    unique_variable_name(e->asset, v->name, name, sizeof name);
    behavior_asset *before = ed_snapshot(e);
    behavior_variable *copy = behavior_asset_duplicate_variable(e->asset, v->id, name);
    if (sm_record(e, before, copy != NULL)) {
        select_member(s, STUDIO_MEMBER_VARIABLE, copy->id);
    }
}

static const char *graph_label(const graph *g)
{
    return g->name && g->name[0] ? g->name : "Event Graph";
}

/* The selected member's name, for its toast. */
static const char *member_name(const studio *s)
{
    switch (s->members.kind) {
        case STUDIO_MEMBER_GRAPH: return graph_label(sm_graph(s));
        case STUDIO_MEMBER_VARIABLE: return sm_variable(s)->name;
        case STUDIO_MEMBER_LOCAL: return sm_local(s)->name;
        default: return "";
    }
}

/* The graph editor's latest undo step, which a delete's toast undoes. */
static const behavior_asset *latest_step(const editor *e)
{
    return e && acount(e->undo) ? alast(e->undo) : NULL;
}

/* A toast offers Undo while the delete is still the latest edit, as scene
   deletes do. */
static void offer_undo(studio *s, const char *name)
{
    studio_members *m = &s->members;
    char title[160];
    snprintf(title, sizeof title, "Deleted %s", name);
    kit_toast_dismiss(s->kit, m->undo.toast);
    m->undo.toast = kit_toast(
        s->kit,
        0,
        &(kit_toast_desc){ .title = title, .action = "Undo", .duration = 6 }
    );
    m->undo.step = latest_step(sg_graph_editor(s));
    studio_record(s, KIT_TOAST_DEFAULT, title);
}

void studio_members_toast(studio *s)
{
    studio_members *m = &s->members;
    if (!m->undo.toast) {
        return;
    }
    bool current = latest_step(sg_graph_editor(s)) == m->undo.step;
    if (kit_toast_action(s->kit, m->undo.toast)) {
        m->undo.toast = 0;
        if (current && studio_graph_can_replay(s, false)) {
            studio_graph_history(s, false);
            studio_report(s, "Undo");
        }
        return;
    }
    if (!current) {
        kit_toast_dismiss(s->kit, m->undo.toast);
        m->undo.toast = 0;
    }
}

/* The open document's member as a symbol of the asset index. */
static reference_symbol member_symbol(studio *s, studio_member_kind kind, uint64_t id)
{
    reference_symbol symbol = { .asset = studio_refresh_document_asset(s), .id = id };
    symbol.kind = kind == STUDIO_MEMBER_GRAPH ? REFERENCE_SYMBOL_GRAPH
                                              : REFERENCE_SYMBOL_VARIABLE;
    return symbol;
}

/* Deleting a variable deletes its Get and Set nodes (Unreal); deleting a
   named value draws its wires again. */
static void delete_member_now(studio *s)
{
    editor *e = sg_graph_editor(s);
    studio_members *m = &s->members;
    if (locked(s) || m->kind == STUDIO_MEMBER_NONE) {
        return;
    }
    char name[128];
    snprintf(name, sizeof name, "%s", member_name(s));
    behavior_asset *before = NULL;
    bool deleted = false;
    switch (m->kind) {
        case STUDIO_MEMBER_GRAPH:
            deleted = editor_delete_graph(e, m->id) == 0;
            break;
        case STUDIO_MEMBER_VARIABLE:
            before = ed_snapshot(e);
            behavior_asset_remove_variable_uses(e->asset, m->id);
            deleted = sm_record(e, before, behavior_asset_delete_variable(e->asset, m->id) == 0);
            break;
        case STUDIO_MEMBER_LOCAL:
            before = ed_snapshot(e);
            deleted = sm_record(e, before, graph_expand_local(sg_active_graph(s), m->id) == 0);
            break;
        default: return;
    }
    m->kind = STUDIO_MEMBER_NONE;
    if (deleted) {
        offer_undo(s, name);
    }
}

void studio_members_delete_now(studio *s, studio_member_kind kind, uint64_t id)
{
    select_member(s, kind, id);
    delete_member_now(s);
}

/* A function or variable something still uses asks first, listing its uses. */
static void delete_member(studio *s)
{
    studio_members *m = &s->members;
    bool asks = (m->kind == STUDIO_MEMBER_GRAPH || m->kind == STUDIO_MEMBER_VARIABLE) &&
                !locked(s) &&
                studio_refactor_confirm_delete(
                    s,
                    member_symbol(s, m->kind, m->id),
                    member_name(s),
                    m->kind,
                    m->id
                );
    if (!asks) {
        delete_member_now(s);
    }
}

bool studio_members_rename_across(
    studio *s,
    studio_member_kind kind,
    uint64_t id,
    const char *name,
    const char *new_name
)
{
    return name && strcmp(name, new_name) &&
           studio_refactor_member_rename(s, member_symbol(s, kind, id), name, new_name);
}

static void rename_graph(studio *s, graph *g, const char *name)
{
    editor_command rename = { .kind = EDITOR_COMMAND_RENAME_GRAPH,
                              .data.rename_graph = { g->graph_id, name } };
    if (editor_command_apply(sg_graph_editor(s), rename) != 0) {
        sm_refused(s, "A graph has that name");
    }
}

static void rename_member(studio *s, const char *name)
{
    editor *e = sg_graph_editor(s);
    studio_members *m = &s->members;
    graph *g = behavior_asset_find_graph(e->asset, m->rename_id);
    const behavior_variable *v = behavior_asset_find_variable_by_id(e->asset, m->rename_id);
    const graph_local *local = graph_local_by_id(sg_active_graph(s), m->rename_id);
    behavior_asset *before = NULL;
    /* A library's functions and an event's payload are named elsewhere too. */
    const char *old = m->rename_kind == STUDIO_MEMBER_GRAPH ? (g ? g->name : NULL)
                    : m->rename_kind == STUDIO_MEMBER_VARIABLE ? (v ? v->name : NULL)
                                                               : NULL;
    if (studio_members_rename_across(s, m->rename_kind, m->rename_id, old, name)) {
        return;
    }
    switch (m->rename_kind) {
        case STUDIO_MEMBER_GRAPH:
            if (g && strcmp(g->name, name)) {
                rename_graph(s, g, name);
            }
            break;
        case STUDIO_MEMBER_VARIABLE:
            if (v && strcmp(v->name, name)) {
                before = ed_snapshot(e);
                if (!sm_record(e, before, behavior_asset_rename_variable(e->asset, v->id, name) == 0)) {
                    sm_refused(s, "A variable has that name");
                }
            }
            break;
        case STUDIO_MEMBER_LOCAL:
            if (local && strcmp(local->name, name)) {
                before = ed_snapshot(e);
                if (!sm_record(
                        e,
                        before,
                        graph_rename_local(sg_active_graph(s), local->id, name) == 0
                    )) {
                    sm_refused(s, "A value has that name");
                }
            }
            break;
        default: break;
    }
}

/* ---------- Rows ---------- */

static section graph_section(const graph *g)
{
    return g->kind == GRAPH_FUNCTION ? SECTION_FUNCTIONS
         : g->kind == GRAPH_MACRO    ? SECTION_MACROS
                                     : SECTION_GRAPHS;
}

/* Libraries hold functions only; variables are a behavior's, or a field's
   and an event's parameters. */
static bool shows(section sec, behavior_domain domain)
{
    switch (sec) {
        case SECTION_GRAPHS:
            return domain != BEHAVIOR_DOMAIN_LIBRARY &&
                   domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY &&
                   domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION;
        case SECTION_FUNCTIONS:
            return domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION;
        case SECTION_MACROS: return domain == BEHAVIOR_DOMAIN_EVENT;
        case SECTION_EVENTS:
            return domain == BEHAVIOR_DOMAIN_EVENT ||
                   domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION;
        case SECTION_VARIABLES:
            return domain == BEHAVIOR_DOMAIN_EVENT ||
                   domain == BEHAVIOR_DOMAIN_FIELD ||
                   domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION;
        default: return true;
    }
}

static const char *section_label(const studio *s, section sec)
{
    bool parameters = sec == SECTION_VARIABLES &&
                      sg_graph_editor(s)->asset->domain != BEHAVIOR_DOMAIN_EVENT;
    return parameters ? "Parameters" : SECTION_LABELS[sec];
}

/* "Default" is Unreal's uncategorized category. */
static const char *category_of(const behavior_variable *v)
{
    return v->category && v->category[0] && strcmp(v->category, "Default") ? v->category
                                                                           : NULL;
}

static bool same_category(const behavior_variable *a, const behavior_variable *b)
{
    const char *x = category_of(a), *y = category_of(b);
    return x == y || (x && y && !strcmp(x, y));
}

static bool variable_matches(const behavior_variable *v, const char *filter)
{
    return studio_matches(filter, v->name) ||
           (category_of(v) && studio_matches(filter, category_of(v)));
}

/* Uncategorized variables first, then each category in order of first use
   with its variables. */
static int variable_rows(const behavior_asset *asset, const char *filter, member_row **rows)
{
    const behavior_variable *vars = asset->variables;
    int total = 0;
    for (int i = 0; i < acount(vars); i++) {
        if (!category_of(&vars[i]) && variable_matches(&vars[i], filter)) {
            apush(*rows, ((member_row){ ROW_VARIABLE, i, 1, 0 }));
            total++;
        }
    }
    for (int i = 0; i < acount(vars); i++) {
        bool first = category_of(&vars[i]) != NULL;
        for (int j = 0; first && j < i; j++) {
            first = !same_category(&vars[i], &vars[j]);
        }
        if (!first) {
            continue;
        }
        int header = acount(*rows);
        apush(*rows, ((member_row){ ROW_CATEGORY, i, 1, 0 }));
        for (int j = i; j < acount(vars); j++) {
            if (same_category(&vars[i], &vars[j]) &&
                variable_matches(&vars[j], filter)) {
                apush(*rows, ((member_row){ ROW_VARIABLE, j, 2, 0 }));
                (*rows)[header].count++;
            }
        }
        total += (*rows)[header].count;
        if (!(*rows)[header].count) {
            asetlen(*rows, header);
        }
    }
    return total;
}

static bool event_node(const node *n)
{
    return n && n->desc && !strcmp(n->desc->name, "Event") &&
           n->reference_name && n->reference_name[0];
}

/* Whether an Event node for this event comes before node `index` of graph
   `at`; each event lists once. */
static bool event_listed(const behavior_asset *asset, int at, int index, const char *name)
{
    for (int g = 0; g <= at; g++) {
        const graph *gr = asset->graphs[g];
        for (int i = 0; i < (g == at ? index : acount(gr->nodes)); i++) {
            if (event_node(gr->nodes[i]) &&
                !strcmp(gr->nodes[i]->reference_name, name)) {
                return true;
            }
        }
    }
    return false;
}

/* The events a behavior's event graphs handle, or an event definition's own. */
static int event_rows(const studio *s, member_row **rows)
{
    const behavior_asset *asset = sg_graph_editor(s)->asset;
    const char *filter = s->members.filter;
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        bool shown = studio_matches(filter, s->scene->graph_document_id);
        if (shown) {
            apush(*rows, ((member_row){ ROW_EVENT, -1, 1, 0, 0 }));
        }
        return shown;
    }
    int count = 0;
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *gr = asset->graphs[g];
        for (int i = 0; gr->kind == GRAPH_EVENT && i < acount(gr->nodes); i++) {
            const node *n = gr->nodes[i];
            if (event_node(n) && studio_matches(filter, n->reference_name) &&
                !event_listed(asset, g, i, n->reference_name)) {
                apush(*rows, ((member_row){ ROW_EVENT, g, 1, 0, i }));
                count++;
            }
        }
    }
    return count;
}

static int section_rows(const studio *s, section sec, member_row **rows)
{
    const behavior_asset *asset = sg_graph_editor(s)->asset;
    const graph *active = sg_active_graph(s);
    const char *filter = s->members.filter;
    int count = 0;
    if (sec == SECTION_VARIABLES) {
        return variable_rows(asset, filter, rows);
    }
    if (sec == SECTION_EVENTS) {
        return event_rows(s, rows);
    }
    if (sec == SECTION_VALUES) {
        for (int i = 0; i < acount(active->locals); i++) {
            if (studio_matches(filter, active->locals[i].name)) {
                apush(*rows, ((member_row){ ROW_LOCAL, i, 1, 0 }));
                count++;
            }
        }
        return count;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        /* An expression's generated function belongs to its node. */
        if (!g->math_expression && graph_section(g) == sec &&
            studio_matches(filter, graph_label(g))) {
            apush(*rows, ((member_row){ ROW_GRAPH, i, 1, 0 }));
            count++;
        }
    }
    return count;
}

/* Sections with their members; an empty section shows while it can take a
   new member and nothing is filtered. */
static member_row *member_rows(const studio *s)
{
    const studio_members *m = &s->members;
    behavior_domain domain = sg_graph_editor(s)->asset->domain;
    member_row *rows = NULL, *items = NULL;
    for (int sec = 0; sec < SECTION_COUNT; sec++) {
        asetlen(items, 0);
        int count = shows((section)sec, domain)
                      ? section_rows(s, (section)sec, &items)
                      : 0;
        bool addable = sec == SECTION_FUNCTIONS || sec == SECTION_MACROS ||
                       sec == SECTION_VARIABLES;
        if (!shows((section)sec, domain) || (!count && (m->filter[0] || !addable))) {
            continue;
        }
        apush(rows, ((member_row){ ROW_SECTION, sec, 0, count }));
        if (m->filter[0] || !(m->collapsed & 1u << sec)) {
            for (int i = 0; i < acount(items); i++) {
                apush(rows, items[i]);
            }
        }
    }
    afree(items);
    return rows;
}

pin_spec sm_variable_pin(const behavior_variable *v)
{
    return (pin_spec){ .name = v->name,
                       .type = v->type,
                       .type_name = v->type_name,
                       .kind = PIN_DATA,
                       .container = v->container,
                       .default_value = v->default_value };
}

void sm_swatch(studio *s, const char *key, const pin_spec *pin)
{
    uint32_t color = sg_pin_color(s, pin);
    kit_dot_desc dot = { pin->kind == PIN_EXEC ? KIT_DOT_ARROW
                         : pin->container == PIN_CONTAINER_SINGLE ? KIT_DOT_CIRCLE
                                                                  : KIT_DOT_SQUARE,
                         color,
                         color,
                         0 };
    kit_dot(s->kit, key, "member-swatch", &dot);
}

static void section_row(studio *s, const member_row *row)
{
    kit *k = s->kit;
    studio_members *m = &s->members;
    static const char *const ADD_TIPS[SECTION_COUNT] = {
        [SECTION_FUNCTIONS] = "New Function",
        [SECTION_MACROS] = "New Macro",
        [SECTION_VARIABLES] = "New Variable",
    };
    char key[8], meta[16];
    snprintf(key, sizeof key, "s%d", row->index);
    snprintf(meta, sizeof meta, "%d", row->count);
    bool open = m->filter[0] || !(m->collapsed & 1u << row->index);
    kit_row_desc d = { .label = section_label(s, (section)row->index),
                       .twisty = open,
                       .meta = meta };
    unsigned events = kit_row_begin(k, key, &d);
    if (ADD_TIPS[row->index]) {
        kit_button_desc add = { .icon = KIT_ICON_ADD,
                                .cls = "toggle",
                                .tip = ADD_TIPS[row->index],
                                .flags = locked(s) ? KIT_DISABLED : 0 };
        if (kit_button(k, "add", &add)) {
            if (row->index == SECTION_VARIABLES) {
                add_variable(s);
            } else {
                add_graph(
                    s,
                    row->index == SECTION_MACROS ? GRAPH_MACRO : GRAPH_FUNCTION,
                    ADD_TIPS[row->index]
                );
            }
        }
    }
    kit_row_end(k);
    kit_flag(k, "data-section", true);
    if (events & (KIT_ROW_TWISTY | KIT_ROW_CLICK)) {
        m->collapsed ^= 1u << row->index;
    }
}

/* The name as a field in the row's place (Blender's F2): Enter or leaving the
   field renames, Escape keeps the name. */
static void rename_row(studio *s, const char *name, uint32_t icon, int depth)
{
    kit *k = s->kit;
    studio_members *m = &s->members;
    kit_push(k, "rename", NULL, "tree-row member-rename");
    char pad[16];
    snprintf(pad, sizeof pad, "%ddp", 24 + depth * 14);
    kit_style(k, "padding-left", pad);
    kit_icon(k, "icon", NULL, icon);
    char text[128];
    snprintf(text, sizeof text, "%s", name);
    bool committed = kit_text_field(k, "name", text, sizeof text, NULL, 0);
    bool focus = m->rename_focus;
    if (focus) {
        kit_focus(k);
        m->rename_focus = false;
    }
    bool left = !focus && !kit_item_active(k);
    kit_pop(k);
    if (committed && text[0]) {
        rename_member(s, text);
    }
    if (committed || left) {
        m->rename_kind = STUDIO_MEMBER_NONE;
    }
}

static bool renaming(const studio *s, studio_member_kind kind, uint64_t id)
{
    return s->members.rename_kind == kind && s->members.rename_id == id;
}

/* The member a row shows; NONE for sections, categories and events. */
static studio_member_kind row_member(const studio *s, const member_row *row, uint64_t *id)
{
    const behavior_asset *asset = sg_graph_editor(s)->asset;
    switch (row->kind) {
        case ROW_GRAPH:
            *id = asset->graphs[row->index]->graph_id;
            return STUDIO_MEMBER_GRAPH;
        case ROW_VARIABLE:
            *id = asset->variables[row->index].id;
            return STUDIO_MEMBER_VARIABLE;
        case ROW_LOCAL:
            *id = sg_active_graph(s)->locals[row->index].id;
            return STUDIO_MEMBER_LOCAL;
        default: return STUDIO_MEMBER_NONE;
    }
}

/* Whether a row shows the member being renamed. */
static bool renaming_row(const studio *s, const member_row *row)
{
    uint64_t id = 0;
    studio_member_kind kind = row_member(s, row, &id);
    return kind && renaming(s, kind, id);
}

static bool selected_row(const studio *s, const member_row *row)
{
    uint64_t id = 0;
    studio_member_kind kind = row_member(s, row, &id);
    return kind && kind == s->members.kind && id == s->members.id;
}

static void open_menu(studio *s, unsigned events, int area)
{
    if (events & KIT_ROW_CONTEXT) {
        char key[24];
        snprintf(key, sizeof key, "member:%d", area);
        kit_popup_open_at(s->kit, key, s->in.mouse_x, s->in.mouse_y);
    }
}

static void member_drag(kit *k, char kind, uint64_t id, const char *label, uint32_t icon)
{
    char payload[32];
    snprintf(payload, sizeof payload, "%c%llu", kind, (unsigned long long)id);
    kit_drag_desc drag = { .type = "member", .payload = payload, .label = label, .icon = icon };
    kit_drag_source(k, &drag);
}

/* ---------- Uses ---------- */

static const char *const ROLE_WORDS[REFERENCE_ROLE_COUNT] = {
    [REFERENCE_USE] = "use",           [REFERENCE_CALL] = "call",
    [REFERENCE_ARGUMENT] = "argument", [REFERENCE_READ] = "read",
    [REFERENCE_WRITE] = "write",       [REFERENCE_SEND] = "sender",
    [REFERENCE_HANDLE] = "handler",    [REFERENCE_SPAWN] = "spawn",
    [REFERENCE_ATTACH] = "attachment", [REFERENCE_BIND] = "binding",
};

static bool same_member(const struct studio_member_uses *uses, const reference_symbol *symbol)
{
    if (uses->kind != symbol->kind) {
        return false;
    }
    return symbol->kind == REFERENCE_SYMBOL_EVENT
             ? !strcmp(uses->event, symbol->name)
             : uses->graph_id == symbol->graph_id && uses->id == symbol->id;
}

static struct studio_member_uses *uses_of(studio_members *m, const reference_symbol *symbol)
{
    for (int i = 0; i < acount(m->uses.items); i++) {
        if (same_member(&m->uses.items[i], symbol)) {
            return &m->uses.items[i];
        }
    }
    return NULL;
}

/* Tallies by role the uses of the document's graphs, variables and locals,
   and of every event. */
static void count_uses(studio_members *m, const reference_index *index, int asset)
{
    asetlen(m->uses.items, 0);
    for (int i = 0; asset >= 0 && i < reference_index_count(index); i++) {
        const reference *r = reference_index_at(index, i);
        const reference_symbol *symbol = &r->symbol;
        bool event = symbol->kind == REFERENCE_SYMBOL_EVENT;
        bool member = symbol->asset == asset &&
                      (symbol->kind == REFERENCE_SYMBOL_GRAPH ||
                       symbol->kind == REFERENCE_SYMBOL_VARIABLE ||
                       symbol->kind == REFERENCE_SYMBOL_LOCAL);
        if (r->role == REFERENCE_DEFINE || (!event && !member)) {
            continue;
        }
        struct studio_member_uses *uses = uses_of(m, symbol);
        if (!uses) {
            struct studio_member_uses added = { .kind = symbol->kind,
                                                .graph_id = symbol->graph_id,
                                                .id = symbol->id };
            snprintf(added.event, sizeof added.event, "%s", event ? symbol->name : "");
            apush(m->uses.items, added);
            uses = &alast(m->uses.items);
        }
        uses->roles[r->role]++;
    }
}

/* Counts follow the index's builds and the open document. */
static const struct studio_member_uses *member_uses(studio *s, const reference_symbol *symbol)
{
    studio_members *m = &s->members;
    unsigned built = s->scene->asset_index_built_generation;
    int asset = studio_document_asset(s);
    if (!m->uses.counted || m->uses.generation != built || m->uses.asset != asset) {
        count_uses(m, app_scene_authoring_asset_index(s->scene), asset);
        m->uses.counted = true;
        m->uses.generation = built;
        m->uses.asset = asset;
    }
    return uses_of(m, symbol);
}

/* A graph, variable or local of the open document. */
static reference_symbol own_symbol(const studio *s, reference_symbol_kind kind, uint64_t graph_id, uint64_t id)
{
    return (reference_symbol){ .kind = kind,
                               .asset = studio_document_asset(s),
                               .graph_id = graph_id,
                               .id = id };
}

/* A member's use count, whose tip splits it by role; pressing it lists the
   uses in Find. Events count every sender and handler. */
static void uses_button(studio *s, const char *title, reference_symbol symbol)
{
    const struct studio_member_uses *uses = member_uses(s, &symbol);
    char tip[160] = "", label[16];
    int total = 0;
    for (int role = 0; uses && role < REFERENCE_ROLE_COUNT; role++) {
        int count = uses->roles[role];
        size_t used = strlen(tip);
        if (count) {
            snprintf(
                tip + used,
                sizeof tip - used,
                "%s%d %s%s",
                total ? ", " : "",
                count,
                ROLE_WORDS[role],
                count == 1 ? "" : "s"
            );
        }
        total += count;
    }
    snprintf(label, sizeof label, "%d", total);
    kit_button_desc d = { .label = label,
                          .size = KIT_SM,
                          .cls = "member-uses",
                          .tip = total ? tip : "No uses" };
    bool pressed = kit_button(s->kit, "uses", &d);
    kit_flag(s->kit, "data-unused", total == 0);
    if (pressed) {
        studio_find_references(s, title, symbol);
    }
}

static void graph_row(studio *s, const member_row *row, int area)
{
    kit *k = s->kit;
    editor *e = sg_graph_editor(s);
    graph *g = e->asset->graphs[row->index];
    bool member = g->kind != GRAPH_EVENT;
    uint32_t icon = g->kind == GRAPH_FUNCTION ? KIT_ICON_FUNCTION
                  : g->kind == GRAPH_MACRO    ? KIT_ICON_EXPRESSION
                                              : KIT_ICON_EVENT;
    char key[32];
    snprintf(key, sizeof key, "g%llu", (unsigned long long)g->graph_id);
    kit_push(k, key, NULL, "member-row");
    if (renaming(s, STUDIO_MEMBER_GRAPH, g->graph_id)) {
        rename_row(s, graph_label(g), icon, row->depth);
        kit_pop(k);
        return;
    }
    bool selected = s->members.kind == STUDIO_MEMBER_GRAPH &&
                    s->members.id == g->graph_id;
    kit_row_desc d = {
        .label = graph_label(g),
        .icon = icon,
        .depth = row->depth,
        .twisty = -1,
        .meta = g->kind == GRAPH_FUNCTION && graph_signature_pure(g) ? "pure" : NULL,
        .flags = (selected ? KIT_ON : 0) | (g == e->active_graph ? KIT_ACTIVE : 0)
    };
    unsigned events = kit_row_begin(k, "row", &d);
    if (member) {
        uses_button(s, graph_label(g), own_symbol(s, REFERENCE_SYMBOL_GRAPH, 0, g->graph_id));
    }
    kit_row_end(k);
    if (member) {
        member_drag(k, 'g', g->graph_id, graph_label(g), icon);
    }
    kit_pop(k);
    if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
        if (g != e->active_graph) {
            editor_open_graph(e, g->graph_id);
        }
        select_member(s, member ? STUDIO_MEMBER_GRAPH : STUDIO_MEMBER_NONE, g->graph_id);
    }
    if (member && (events & KIT_ROW_DOUBLE_CLICK)) {
        start_rename(s, STUDIO_MEMBER_GRAPH, g->graph_id);
    }
    if (member) {
        open_menu(s, events, area);
    }
}

static void variable_row(studio *s, const member_row *row, int area)
{
    kit *k = s->kit;
    behavior_asset *asset = sg_graph_editor(s)->asset;
    const behavior_variable *v = &asset->variables[row->index];
    char key[32], type[96];
    snprintf(key, sizeof key, "v%llu", (unsigned long long)v->id);
    kit_push(k, key, NULL, "member-row");
    if (renaming(s, STUDIO_MEMBER_VARIABLE, v->id)) {
        rename_row(s, v->name, KIT_ICON_VARIABLE, row->depth);
        kit_pop(k);
        return;
    }
    pin_spec pin = sm_variable_pin(v);
    bool selected = s->members.kind == STUDIO_MEMBER_VARIABLE &&
                    s->members.id == v->id;
    kit_row_desc d = { .label = v->name,
                       .icon = KIT_ICON_VARIABLE,
                       .depth = row->depth,
                       .twisty = -1,
                       .meta = sm_type_label(&pin, type, sizeof type),
                       .flags = selected ? KIT_ON | KIT_ACTIVE : 0 };
    unsigned events = kit_row_begin(k, "row", &d);
    sm_swatch(s, "type", &pin);
    /* Event and field parameters are read by name, not through Get nodes. */
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT) {
        uses_button(s, v->name, own_symbol(s, REFERENCE_SYMBOL_VARIABLE, 0, v->id));
    }
    kit_row_end(k);
    /* Get and Set nodes read a behavior's own variables. */
    if (asset->domain == BEHAVIOR_DOMAIN_EVENT) {
        member_drag(k, 'v', v->id, v->name, KIT_ICON_VARIABLE);
    }
    kit_pop(k);
    uint64_t id = v->id;
    if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
        select_member(s, STUDIO_MEMBER_VARIABLE, id);
    }
    if (events & KIT_ROW_DOUBLE_CLICK) {
        start_rename(s, STUDIO_MEMBER_VARIABLE, id);
    }
    open_menu(s, events, area);
}

static void category_row(studio *s, const member_row *row)
{
    const behavior_variable *v = &sg_graph_editor(s)->asset->variables[row->index];
    char key[16], meta[16];
    snprintf(key, sizeof key, "c%d", row->index);
    snprintf(meta, sizeof meta, "%d", row->count);
    kit_row_desc d = { .label = category_of(v),
                       .icon = KIT_ICON_FOLDER,
                       .depth = row->depth,
                       .twisty = -1,
                       .meta = meta };
    kit_row_begin(s->kit, key, &d);
    kit_row_end(s->kit);
}

static void local_row(studio *s, const member_row *row, int area)
{
    kit *k = s->kit;
    graph *gr = sg_active_graph(s);
    const graph_local *local = &gr->locals[row->index];
    node *source = graph_node_by_id(gr, local->source_id);
    int output = 0, index = -1;
    const pin_spec *pin = graph_pin_by_id(source, local->source_pin_id, &output, &index);
    char key[32], type[96];
    snprintf(key, sizeof key, "l%llu", (unsigned long long)local->id);
    kit_push(k, key, NULL, "member-row");
    if (renaming(s, STUDIO_MEMBER_LOCAL, local->id)) {
        rename_row(s, local->name, KIT_ICON_LOCAL, row->depth);
        kit_pop(k);
        return;
    }
    bool selected = s->members.kind == STUDIO_MEMBER_LOCAL &&
                    s->members.id == local->id;
    kit_row_desc d = { .label = local->name,
                       .icon = KIT_ICON_LOCAL,
                       .depth = row->depth,
                       .twisty = -1,
                       .meta = pin ? sm_type_label(pin, type, sizeof type) : NULL,
                       .flags = selected ? KIT_ON | KIT_ACTIVE : 0 };
    unsigned events = kit_row_begin(k, "row", &d);
    if (pin) {
        sm_swatch(s, "type", pin);
    }
    uses_button(
        s,
        local->name,
        own_symbol(s, REFERENCE_SYMBOL_LOCAL, gr->graph_id, local->id)
    );
    kit_row_end(k);
    kit_pop(k);
    if (events & (KIT_ROW_CLICK | KIT_ROW_CONTEXT)) {
        select_local(s, local);
    }
    if (events & KIT_ROW_DOUBLE_CLICK) {
        start_rename(s, STUDIO_MEMBER_LOCAL, s->members.id);
    }
    open_menu(s, events, area);
}

/* An event the behavior handles, which a click reveals, or an event
   definition's own event. */
static void event_row(studio *s, const member_row *row)
{
    kit *k = s->kit;
    const behavior_asset *asset = sg_graph_editor(s)->asset;
    const graph *gr = row->index >= 0 ? asset->graphs[row->index] : NULL;
    const node *n = gr ? gr->nodes[row->node] : NULL;
    const char *name = n ? n->reference_name : s->scene->graph_document_id;
    char key[32];
    snprintf(key, sizeof key, "e%d.%d", row->index, row->node);
    kit_push(k, key, NULL, "member-row");
    kit_row_desc d = { .label = name,
                       .icon = KIT_ICON_EVENT,
                       .depth = row->depth,
                       .twisty = -1 };
    unsigned events = kit_row_begin(k, "row", &d);
    uses_button(s, name, (reference_symbol){ .kind = REFERENCE_SYMBOL_EVENT, .asset = -1, .name = name });
    kit_row_end(k);
    kit_pop(k);
    if (n && (events & KIT_ROW_CLICK)) {
        studio_reveal_node(s, gr->graph_id, n->id);
    }
}

static bool menu_item(kit *k, const char *key, const char *label, uint32_t icon, const char *shortcut, bool disabled)
{
    kit_menu_item_desc d = { .label = label,
                             .icon = icon,
                             .shortcut = shortcut,
                             .checked = -1,
                             .disabled = disabled };
    return kit_menu_item(k, key, &d);
}

static void member_menu(studio *s, int area)
{
    kit *k = s->kit;
    studio_members *m = &s->members;
    char key[24];
    snprintf(key, sizeof key, "member:%d", area);
    if (!kit_popup_begin(k, key, "menu")) {
        return;
    }
    bool readonly = locked(s);
    graph *g = sm_graph(s);
    if (g && menu_item(k, "open", "Open", KIT_ICON_EXTERNAL, NULL, false)) {
        editor_open_graph(sg_graph_editor(s), g->graph_id);
    }
    if (menu_item(k, "rename", "Rename", KIT_ICON_EDIT, "F2", readonly)) {
        start_rename(s, m->kind, m->id);
    }
    if (m->kind == STUDIO_MEMBER_VARIABLE &&
        menu_item(k, "dup", "Duplicate", KIT_ICON_COPY, studio_chord(s, "Shift+D"), readonly)) {
        duplicate_variable(s);
    }
    bool callable = g && (g->kind == GRAPH_FUNCTION || g->kind == GRAPH_MACRO);
    if ((m->kind == STUDIO_MEMBER_VARIABLE || callable) &&
        menu_item(k, "refs", "Find References", KIT_ICON_REFERENCES, NULL, false)) {
        if (callable) {
            studio_find_graph_references(s, g);
        } else {
            studio_find_variable_references(s, m->id);
        }
    }
    if (callable &&
        menu_item(k, "calls", "Call Hierarchy", KIT_ICON_CALLERS, NULL, false)) {
        studio_calls_graph(s, g);
    }
    kit_menu_separator(k, "s0");
    if (menu_item(k, "delete", "Delete", KIT_ICON_TRASH, "X", readonly)) {
        delete_member(s);
    }
    kit_popup_end(k);
}

/* ---------- Editor ---------- */

void studio_members_header(studio *s, int area)
{
    (void)area;
    kit *k = s->kit;
    kit_push(k, "filter", NULL, "members-filter");
    kit_search(k, "search", s->members.filter, sizeof s->members.filter, "Filter");
    studio_find_field(s, STUDIO_EDITOR_MEMBERS);
    kit_pop(k);
}

void studio_members_body(studio *s, int area)
{
    kit *k = s->kit;
    follow(s);
    if (!studio_graph_active(s)) {
        s->members.rename_kind = STUDIO_MEMBER_NONE;
        kit_label(k, "empty", "output-empty", "No behavior open");
        return;
    }
    member_row *rows = member_rows(s);
    /* A new member, named as it is added, scrolls into view, and so does
       one Go to Definition selects. */
    int reveal = -1;
    for (int r = 0; s->members.rename_focus && r < acount(rows); r++) {
        reveal = renaming_row(s, &rows[r]) ? r : reveal;
    }
    for (int r = 0; s->members.reveal && r < acount(rows); r++) {
        reveal = selected_row(s, &rows[r]) ? r : reveal;
    }
    s->members.reveal = false;
    kit_list_desc list = { .count = acount(rows),
                           .row_height = kit_metric(s->kit, "--row"),
                           .reveal = reveal };
    int first, last;
    kit_list_begin(k, "members", "members-list", &list, &first, &last);
    for (int r = first; r <= last && r < acount(rows); r++) {
        switch (rows[r].kind) {
            case ROW_SECTION: section_row(s, &rows[r]); break;
            case ROW_CATEGORY: category_row(s, &rows[r]); break;
            case ROW_GRAPH: graph_row(s, &rows[r], area); break;
            case ROW_VARIABLE: variable_row(s, &rows[r], area); break;
            case ROW_LOCAL: local_row(s, &rows[r], area); break;
            case ROW_EVENT: event_row(s, &rows[r]); break;
        }
    }
    kit_list_end(k);
    afree(rows);
    if (s->members.kind) {
        member_menu(s, area);
    }
}

bool studio_members_key(studio *s, SDL_Keycode key, SDL_Keymod mod)
{
    if (!studio_graph_active(s) || (mod & SDL_KMOD_ALT)) {
        return false;
    }
    /* Duplicate is Shift+D or Mod+D, as for objects. */
    bool duplicate = key == SDLK_D &&
                     ((mod & SDL_KMOD_SHIFT) || studio_primary(s, mod));
    if (studio_primary(s, mod) && !duplicate) {
        return false;
    }
    follow(s);
    if (key == SDLK_F2) {
        start_rename(s, s->members.kind, s->members.id);
        return true;
    }
    if (key == SDLK_X || key == SDLK_DELETE || key == SDLK_BACKSPACE) {
        delete_member(s);
        return true;
    }
    if (duplicate) {
        duplicate_variable(s);
        return true;
    }
    return false;
}

/* ---------- Canvas drop ---------- */

static node *drop_node(studio *s, char kind, uint64_t id)
{
    editor *e = sg_graph_editor(s);
    graph *gr = sg_active_graph(s);
    if (kind == 'v') {
        bool set = s->mods & SDL_KMOD_ALT;
        return set ? behavior_asset_add_set_node_to_graph(e->asset, gr, id)
                   : behavior_asset_add_get_node_to_graph(e->asset, gr, id);
    }
    /* A function may call itself; a macro can't expand into itself. */
    graph *target = behavior_asset_find_graph(e->asset, id);
    if (!target || (target == gr && target->kind == GRAPH_MACRO)) {
        return NULL;
    }
    return target->kind == GRAPH_MACRO
             ? behavior_asset_add_macro_call_node_to_graph(e->asset, gr, target)
             : behavior_asset_add_function_call_node_to_graph(e->asset, gr, target);
}

void studio_members_drop(studio *s)
{
    char payload[32];
    if (!kit_drop_target(s->kit, "member", payload, sizeof payload) ||
        sg_graph_readonly(s)) {
        return;
    }
    editor *e = sg_graph_editor(s);
    behavior_asset *before = ed_snapshot(e);
    node *n = drop_node(s, payload[0], strtoull(payload + 1, NULL, 10));
    if (!sm_record(e, before, n != NULL)) {
        return;
    }
    sg_to_world(s, s->in.mouse_x, s->in.mouse_y, &n->x, &n->y);
    n->placed = true;
    sg_select_only(e, n);
    s->focus_area = s->graph.area;
}

/* Its section opens, and Members shows in an area or the drawer. */
void studio_members_reveal(studio *s, studio_member_kind kind, uint64_t id)
{
    select_member(s, kind, id);
    graph *g = sm_graph(s);
    section sec = kind == STUDIO_MEMBER_VARIABLE ? SECTION_VARIABLES
                : kind == STUDIO_MEMBER_LOCAL    ? SECTION_VALUES
                : g                              ? graph_section(g)
                                                 : SECTION_GRAPHS;
    s->members.reveal = true;
    s->members.collapsed &= ~(1u << sec);
    studio_show_editor(s, STUDIO_EDITOR_MEMBERS);
}
