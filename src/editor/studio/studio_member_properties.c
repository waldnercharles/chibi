/* The Member tab in Properties: the details of the member selected in
 * Members, as Unreal's Details shows a My Blueprint selection. A variable has
 * its type, container, category and default; a function or macro its inputs
 * and outputs, with their containers and the inputs' defaults, reordered by
 * dragging their grips, and a function its purity; a named value its name and
 * source. Types are chosen from a search over basic types, structures and
 * enums, and Exec for a macro (Unreal's pin type picker), in an anchored
 * dropdown. */
#include "editor/studio/studio_members_internal.h"
#include "editor/ui/choices.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *label;
    pin_type type;
    const char *type_name;
} type_choice;

static const type_choice TYPES[] = {
    { "Boolean", PIN_BOOL, "bool" },   { "Integer", PIN_I32, "int" },
    { "Float", PIN_F32, "float" },     { "Double", PIN_F64, "double" },
    { "Name", PIN_NAME, "name" },      { "String", PIN_STR, "const char *" },
    { "Vector", PIN_STRUCT, "CF_V2" }, { "Entity", PIN_HANDLE, "ecs.entity" },
};
enum
{
    TYPE_COUNT = sizeof TYPES / sizeof *TYPES,
};
/* A macro's tunnels also carry execution (Unreal's Exec type). */
static const type_choice EXEC = { "Exec", PIN_FLOW, NULL };

static const char *const CONTAINERS[] = { "Single", "Array", "Set", "Map" };

/* A basic type matches by type; a named one also by its name. */
static bool same_type(pin_type type, const char *type_name, const type_choice *choice)
{
    bool named = type == PIN_STRUCT || type == PIN_HANDLE || type == PIN_ENUM;
    return type == choice->type &&
           (!named || (type_name && !strcmp(type_name, choice->type_name)));
}

const char *sm_type_label(const pin_spec *pin, char *out, size_t size)
{
    static const char *const SUFFIXES[] = { "", " Array", " Set", " Map" };
    if (pin->kind == PIN_EXEC) {
        snprintf(out, size, "%s", EXEC.label);
        return out;
    }
    const char *label = pin->type_name && pin->type_name[0]
                          ? pin->type_name
                          : pin_type_name(pin->type);
    for (int i = 0; i < TYPE_COUNT; i++) {
        label = same_type(pin->type, pin->type_name, &TYPES[i]) ? TYPES[i].label : label;
    }
    snprintf(out, size, "%s%s", label ? label : "?", SUFFIXES[pin->container]);
    return out;
}

/* ---------- Shared parts ---------- */

static editor *member_editor(const studio *s)
{
    return sg_graph_editor(s);
}

static bool locked(const studio *s)
{
    return !behavior_asset_mutable(member_editor(s)->asset);
}

static void identity(kit *k, uint32_t icon, const char *title, const char *lineage)
{
    kit_push(k, "identity", NULL, "identity");
    kit_push(k, "glyph", NULL, "glyph");
    kit_icon(k, "icon", NULL, icon);
    kit_pop(k);
    kit_push(k, "who", NULL, "who");
    kit_label(k, "name", "title", title);
    kit_push(k, "lineage", NULL, "lineage");
    kit_label(k, "of", NULL, lineage);
    kit_pop(k);
    kit_pop(k);
    kit_pop(k);
}

/* The type as a swatch and a button that opens the type search for
   `id` and `pin_id` (see studio_members.pick). The container has its own
   control, so the button names the value type alone (Unreal). */
static void type_button(studio *s, const pin_spec *pin, uint64_t id, uint64_t pin_id)
{
    kit *k = s->kit;
    char label[96];
    pin_spec value = *pin;
    value.container = PIN_CONTAINER_SINGLE;
    kit_push(k, "type", NULL, "type-pick");
    sm_swatch(s, "swatch", pin);
    kit_button_desc d = { .label = sm_type_label(&value, label, sizeof label),
                          .variant = KIT_SUBTLE,
                          .cls = "type-button",
                          .flags = locked(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "pick", &d)) {
        s->members.pick.open = true;
        s->members.pick.id = id;
        s->members.pick.pin_id = pin_id;
        kit_dropdown_open(k, "member-type", "Search types");
    }
    kit_pop(k);
}

/* ---------- Variables ---------- */

static void variable_edit(studio *s, char *before, bool ok, const char *refusal)
{
    if (!sm_record(member_editor(s), before, ok) && refusal) {
        sm_refused(s, refusal);
    }
}

static double number_of(const pin_value *v)
{
    switch (v->type) {
        case PIN_I8: return v->i8;
        case PIN_U8: return v->u8;
        case PIN_I16: return v->i16;
        case PIN_U16: return v->u16;
        case PIN_I32: return v->i;
        case PIN_U32: return v->u;
        case PIN_I64: return (double)v->i64;
        case PIN_U64: return (double)v->u64;
        case PIN_F32: return v->f;
        default: return v->d;
    }
}

static void set_number(pin_value *v, double number)
{
    switch (v->type) {
        case PIN_I8: v->i8 = (int8_t)number; break;
        case PIN_U8: v->u8 = (uint8_t)number; break;
        case PIN_I16: v->i16 = (int16_t)number; break;
        case PIN_U16: v->u16 = (uint16_t)number; break;
        case PIN_I32: v->i = (int32_t)number; break;
        case PIN_U32: v->u = (uint32_t)number; break;
        case PIN_I64: v->i64 = (int64_t)number; break;
        case PIN_U64: v->u64 = (uint64_t)number; break;
        case PIN_F32: v->f = (float)number; break;
        default: v->d = number; break;
    }
}

static bool enum_control(kit *k, const enum_desc *enumeration, pin_value *value, unsigned flags)
{
    const editor_choices *choices = editor_choices_for_enum(enumeration->name);
    int count = choices ? editor_choices_count(choices) : 0;
    bool described = false;
    int current = -1;
    for (int i = 0; i < count; i++) {
        editor_choice choice = editor_choice_at(choices, i);
        described |= choice.group || choice.icon;
        if (choice.value == value->e) {
            current = i;
        }
    }
    if (described) {
        kit_command_item *items = NULL;
        for (int i = 0; i < count; i++) {
            editor_choice choice = editor_choice_at(choices, i);
            apush(
                items,
                ((kit_command_item){ .label = choice.label ? choice.label
                                                           : choice.name,
                                     .group = choice.group,
                                     .keywords = choice.name,
                                     .icon = choice.icon,
                                     .current = i == current })
            );
        }
        bool changed = kit_choice_items(k, "v", &current, items, count, flags);
        if (changed && current >= 0) {
            value->e = editor_choice_at(choices, current).value;
        }
        afree(items);
        return changed;
    }
    const char **labels = NULL;
    int index = -1;
    for (int i = 0; i < enumeration->value_count; i++) {
        apush(labels, enumeration->values[i].label);
        index = enumeration->values[i].value == value->e ? i : index;
    }
    bool changed = kit_choice(k, "v", &index, labels, enumeration->value_count, flags);
    value->e = changed ? enumeration->values[index].value : value->e;
    afree(labels);
    return changed;
}

/* A value being edited in Properties; vectors and text edit in place. */
typedef struct
{
    pin_value value;
    float vector[4];
    char text[256];
    bool changed, streaming, committed;
} value_edit;

static void vector_control(kit *k, int width, value_edit *edit, unsigned flags)
{
    static const char *const AXES[] = { "X", "Y", "Z", "W" };
    if (edit->value.ptr) {
        memcpy(edit->vector, edit->value.ptr, (size_t)width * sizeof *edit->vector);
    }
    for (int a = 0; a < width; a++) {
        char key[8];
        snprintf(key, sizeof key, "v%d", a);
        double axis = edit->vector[a];
        kit_number_desc d = { .precision = 2,
                              .single_precision = true,
                              .step = 0.1,
                              .addon = AXES[a],
                              .flags = flags };
        if (kit_number(k, key, &axis, &d)) {
            edit->vector[a] = (float)axis;
            edit->changed = edit->streaming = true;
        }
        edit->committed |= kit_item_committed(k);
    }
    edit->value.ptr = edit->vector;
}

static void number_control(kit *k, pin_type type, value_edit *edit, unsigned flags)
{
    bool integer = type < PIN_F32;
    double number = number_of(&edit->value);
    kit_number_desc d = { .integer = integer,
                          .single_precision = type == PIN_F32,
                          .precision = integer ? 0 : 2,
                          .step = integer ? 1 : 0.1,
                          .flags = flags };
    edit->changed = edit->streaming = kit_number(k, "v", &number, &d);
    edit->committed = kit_item_committed(k);
    set_number(&edit->value, number);
}

/* Single values of the types value_control edits. */
static bool value_editable(const pin_spec *pin)
{
    bool enumeration = pin->type == PIN_ENUM && registry_enum_by_name(pin->type_name);
    return pin->kind == PIN_DATA && pin->container == PIN_CONTAINER_SINGLE &&
           (enumeration || pin->type == PIN_BOOL || pin_vector_width(pin) > 0 ||
            pin->type == PIN_STR || pin->type == PIN_NAME ||
            (pin->type >= PIN_I8 && pin->type <= PIN_F64));
}

/* The editor for a value_editable pin's value, as the canvas edits pin
   values: numbers and vectors scrub. */
static void value_control(studio *s, const pin_spec *pin, value_edit *edit)
{
    kit *k = s->kit;
    unsigned flags = locked(s) ? KIT_READONLY : 0;
    if (pin->type == PIN_ENUM) {
        edit->changed = enum_control(k, registry_enum_by_name(pin->type_name), &edit->value, flags);
    } else if (pin->type == PIN_BOOL) {
        bool b = edit->value.b;
        edit->changed = kit_checkbox(k, "v", &b, NULL, flags);
        edit->value.b = b;
    } else if (pin_vector_width(pin) > 0) {
        vector_control(k, pin_vector_width(pin), edit, flags);
    } else if (pin->type == PIN_STR || pin->type == PIN_NAME) {
        snprintf(
            edit->text,
            sizeof edit->text,
            "%s",
            edit->value.str ? edit->value.str : ""
        );
        edit->changed = kit_text_field(k, "v", edit->text, sizeof edit->text, NULL, flags);
        edit->value.str = edit->text;
    } else {
        number_control(k, pin->type, edit, flags);
    }
}

/* Opens the undo step an edit needs: one per discrete change, one per scrub.
   True when the edit should be applied. */
static bool value_step(studio *s, const value_edit *edit)
{
    studio_members *m = &s->members;
    if (!edit->changed || locked(s)) {
        m->value_editing &= !edit->committed;
        return false;
    }
    if (!edit->streaming || !m->value_editing) {
        ed_snapshot_before(member_editor(s));
    }
    m->value_editing = edit->streaming && !edit->committed;
    return true;
}

static void default_value(studio *s, const behavior_variable *v)
{
    pin_spec pin = sm_variable_pin(v);
    value_edit edit = { .value = v->default_value };
    char type[96];
    kit_prop_begin(s->kit, "value", "Value");
    if (value_editable(&pin)) {
        value_control(s, &pin, &edit);
    } else {
        kit_label(s->kit, "v", "dim", sm_type_label(&pin, type, sizeof type));
    }
    kit_prop_end(s->kit);
    if (value_step(s, &edit)) {
        behavior_asset_set_variable_default(member_editor(s)->asset, v->id, &edit.value);
    }
}

static void variable_properties(studio *s, const behavior_variable *v)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    unsigned flags = locked(s) ? KIT_READONLY : 0;
    uint64_t id = v->id;
    pin_spec pin = sm_variable_pin(v);
    identity(k, KIT_ICON_VARIABLE, v->name, v->category && v->category[0] ? v->category : "Variable");
    if (kit_panel_begin(k, "variable", "Variable", NULL, true)) {
        char name[128], category[128];
        snprintf(name, sizeof name, "%s", v->name);
        snprintf(category, sizeof category, "%s", v->category ? v->category : "");
        kit_prop_begin(k, "name", "Name");
        if (kit_text_field(k, "v", name, sizeof name, NULL, flags) && name[0] &&
            !studio_members_rename_across(s, STUDIO_MEMBER_VARIABLE, id, v->name, name)) {
            char *before = ed_snapshot(e);
            variable_edit(s, before, behavior_asset_rename_variable(e->asset, id, name) == 0, "A variable has that name");
        }
        kit_prop_end(k);
        kit_prop_begin(k, "type", "Type");
        type_button(s, &pin, id, 0);
        kit_prop_end(k);
        kit_prop_begin(k, "container", "Container");
        int container = pin.container;
        if (kit_choice(k, "v", &container, CONTAINERS, 4, locked(s) ? KIT_DISABLED : 0)) {
            char *before = ed_snapshot(e);
            variable_edit(
                s,
                before,
                behavior_asset_set_variable_container(e->asset, id, (pin_container)container) ==
                    0,
                NULL
            );
        }
        kit_prop_end(k);
        kit_prop_begin(k, "category", "Category");
        if (kit_text_field(k, "v", category, sizeof category, "Default", flags)) {
            char *before = ed_snapshot(e);
            variable_edit(
                s,
                before,
                behavior_asset_set_variable_category(e->asset, id, category) == 0,
                NULL
            );
        }
        kit_prop_end(k);
    }
    kit_panel_end(k);
    v = behavior_asset_find_variable_by_id(e->asset, id);
    if (kit_panel_begin(k, "default", "Default Value", NULL, true)) {
        default_value(s, v);
    }
    kit_panel_end(k);
}

/* ---------- Functions ---------- */

static node *signature_node(const graph *g, bool result)
{
    return graph_node_by_id(g, result ? g->return_id : g->entry_id);
}

/* An input's default, which its unwired call pins show. Returns true when
   the default changed. */
static bool default_row(studio *s, graph *g, int index)
{
    const pin_spec *pin = &signature_node(g, false)->output_pins[index];
    value_edit edit = { .value = pin->default_value };
    if (!value_editable(pin)) {
        return false;
    }
    kit_prop_begin(s->kit, "default", "Default");
    value_control(s, pin, &edit);
    kit_prop_end(s->kit);
    if (!value_step(s, &edit)) {
        return false;
    }
    graph_signature_set_default(member_editor(s)->asset, g, index, &edit.value);
    return true;
}

bool sm_move_signature_pin(editor *e, graph *g, bool result, uint64_t dragged, int to)
{
    int output, from = -1;
    graph_pin_by_id(signature_node(g, result), dragged, &output, &from);
    if (from == to) {
        return false;
    }
    char *before = ed_snapshot(e);
    return sm_record(e, before, graph_signature_move(e->asset, g, result, from, to) == 0);
}

/* A grip that drags the pin onto another row of its side (dnd-kit). */
static void grip(studio *s, const pin_spec *pin, const char *type)
{
    kit_icon(s->kit, "grip", "param-grip", KIT_ICON_GRIP);
    if (locked(s)) {
        return;
    }
    char id[24];
    snprintf(id, sizeof id, "%llu", (unsigned long long)pin->id);
    kit_drag_desc drag = { .type = type, .payload = id, .label = pin->name };
    kit_drag_source(s->kit, &drag);
}

/* One input or output: a grip, its name, type, container and a remove
   button, and an input's default below. Returns true when an edit changed
   the pins. */
static bool signature_row(studio *s, graph *g, bool result, int index)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    node *owner = signature_node(g, result);
    const pin_spec *pin = result ? &owner->input_pins[index]
                                 : &owner->output_pins[index];
    unsigned flags = locked(s) ? KIT_READONLY : 0;
    bool exec = pin->kind == PIN_EXEC;
    const char *drag = result ? "result" : "parameter";
    char key[32], name[128], dropped[24];
    snprintf(key, sizeof key, "p%llu", (unsigned long long)pin->id);
    snprintf(name, sizeof name, "%s", pin->name ? pin->name : "");
    kit_push(k, key, NULL, "param");
    kit_layout(k);
    kit_push(k, "pin", NULL, "param-row");
    grip(s, pin, drag);
    bool renamed = kit_text_field(k, "name", name, sizeof name, NULL, flags) &&
                   name[0] && strcmp(name, pin->name);
    type_button(s, pin, g->graph_id, pin->id);
    int container = pin->container;
    unsigned fixed = locked(s) || exec ? KIT_DISABLED : 0;
    bool contained = kit_choice(k, "container", &container, CONTAINERS, 4, fixed);
    kit_button_desc remove = { .icon = KIT_ICON_TRASH,
                               .tip = "Remove",
                               .flags = locked(s) ? KIT_DISABLED : 0 };
    bool removed = kit_button(k, "remove", &remove);
    kit_pop(k);
    bool defaulted = !result && default_row(s, g, index);
    kit_pop(k);
    if (kit_drop_target(k, drag, dropped, sizeof dropped)) {
        return sm_move_signature_pin(e, g, result, strtoull(dropped, NULL, 10), index);
    }
    if (!renamed && !contained && !removed) {
        return defaulted;
    }
    /* Calls in other assets name a library function's parameters too. */
    reference_symbol parameter = { .kind = REFERENCE_SYMBOL_PARAMETER,
                                   .asset = studio_refresh_document_asset(s),
                                   .graph_id = g->graph_id,
                                   .id = pin->id };
    if (renamed && studio_refactor_member_rename(s, parameter, pin->name, name)) {
        return false;
    }
    pin_spec type = { .kind = PIN_DATA,
                      .type = pin->type,
                      .type_name = pin->type_name,
                      .container = (pin_container)container };
    char *before = ed_snapshot(e);
    int status = renamed ? graph_signature_rename(e->asset, g, result, index, name)
               : contained
                   ? graph_signature_retype(e->asset, g, result, index, &type)
                   : graph_signature_remove(e->asset, g, result, index);
    if (!sm_record(e, before, status == 0) && renamed) {
        sm_refused(s, "A pin has that name");
    }
    return true;
}

static bool pin_named(const node *n, bool result, const char *name)
{
    int count = result ? n->input_count : n->output_count;
    const pin_spec *pins = result ? n->input_pins : n->output_pins;
    for (int p = 0; p < count; p++) {
        if (pins[p].name && !strcmp(pins[p].name, name)) {
            return true;
        }
    }
    return false;
}

/* A new input or output is a Float, named past the names in use. */
static void add_signature_pin(studio *s, graph *g, bool result)
{
    editor *e = member_editor(s);
    node *owner = signature_node(g, result);
    const char *base = result ? "Result" : "Value";
    char name[64];
    snprintf(name, sizeof name, "%s", base);
    for (int i = 2; pin_named(owner, result, name); i++) {
        snprintf(name, sizeof name, "%s %d", base, i);
    }
    pin_spec pin = { .name = name, .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" };
    char *before = ed_snapshot(e);
    sm_record(e, before, graph_signature_add(e->asset, g, result, &pin) == 0);
}

static void signature_panel(studio *s, graph *g, bool result)
{
    kit *k = s->kit;
    const char *title = result ? "Outputs" : "Inputs";
    if (kit_panel_begin(k, result ? "outputs" : "inputs", title, NULL, true)) {
        node *owner = signature_node(g, result);
        int count = result ? owner->input_count : owner->output_count;
        for (int p = 0; p < count; p++) {
            const pin_spec *pin = result ? &owner->input_pins[p]
                                         : &owner->output_pins[p];
            /* An edit rebuilds the pins; the next frame shows them. */
            if (graph_signature_declared(g, pin) && signature_row(s, g, result, p)) {
                break;
            }
        }
        kit_push(k, "add", NULL, "add-row");
        kit_button_desc add = { .label = result ? "Add Output" : "Add Input",
                                .icon = KIT_ICON_ADD,
                                .variant = KIT_SUBTLE,
                                .flags = locked(s) ? KIT_DISABLED : 0 };
        if (kit_button(k, "add", &add)) {
            add_signature_pin(s, g, result);
        }
        kit_pop(k);
    }
    kit_panel_end(k);
}

static void graph_properties(studio *s, graph *g)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    bool function = g->kind == GRAPH_FUNCTION;
    bool pure = function && graph_signature_pure(g);
    identity(
        k,
        function ? KIT_ICON_FUNCTION : KIT_ICON_EXPRESSION,
        g->name,
        function ? (pure ? "Pure Function" : "Function") : "Macro"
    );
    if (kit_panel_begin(k, "graph", function ? "Function" : "Macro", NULL, true)) {
        char name[128];
        snprintf(name, sizeof name, "%s", g->name);
        kit_prop_begin(k, "name", "Name");
        if (kit_text_field(k, "v", name, sizeof name, NULL, locked(s) ? KIT_READONLY : 0) &&
            name[0] && strcmp(name, g->name) &&
            !studio_members_rename_across(s, STUDIO_MEMBER_GRAPH, g->graph_id, g->name, name)) {
            editor_command rename = { .kind = EDITOR_COMMAND_RENAME_GRAPH,
                                      .data.rename_graph = { g->graph_id, name } };
            if (editor_command_apply(e, rename) != 0) {
                sm_refused(s, "A graph has that name");
            }
        }
        kit_prop_end(k);
        if (function) {
            /* Shader and field functions compute values only. */
            bool fixed = locked(s) || behavior_domain_pure(e->asset->domain);
            kit_prop_begin(k, "pure", "Pure");
            if (kit_checkbox(k, "v", &pure, NULL, fixed ? KIT_DISABLED : 0)) {
                char *before = ed_snapshot(e);
                if (!sm_record(e, before, graph_signature_set_pure(e->asset, g, pure) == 0)) {
                    sm_refused(s, "The function runs nodes in order");
                }
            }
            kit_prop_end(k);
        }
    }
    kit_panel_end(k);
    signature_panel(s, g, false);
    signature_panel(s, g, true);
}

/* ---------- Named values ---------- */

static int local_uses(const graph *g, uint64_t id)
{
    int uses = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        for (int p = 0; p < g->nodes[i]->input_count; p++) {
            const pin_binding *b = g->nodes[i]->input_pins[p].binding;
            uses += b && b->kind == PIN_BINDING_LOCAL && b->local_id == id;
        }
    }
    return uses;
}

static void local_properties(studio *s, const graph_local *local)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    graph *gr = sg_active_graph(s);
    node *source = graph_node_by_id(gr, local->source_id);
    int output = 0, index = -1;
    const pin_spec *pin = graph_pin_by_id(source, local->source_pin_id, &output, &index);
    char name[128], text[320], title[160], pin_label[128];
    identity(k, KIT_ICON_LOCAL, local->name, "Named Value");
    if (kit_panel_begin(k, "value", "Named Value", NULL, true)) {
        snprintf(name, sizeof name, "%s", local->name);
        kit_prop_begin(k, "name", "Name");
        if (kit_text_field(k, "v", name, sizeof name, NULL, sg_graph_readonly(s) ? KIT_READONLY : 0) &&
            name[0] && strcmp(name, local->name)) {
            char *before = ed_snapshot(e);
            if (!sm_record(e, before, graph_rename_local(gr, local->id, name) == 0)) {
                sm_refused(s, "A value has that name");
            }
        }
        kit_prop_end(k);
        ed_pin_label(source, pin, pin_label, sizeof pin_label);
        snprintf(text, sizeof text, "%s \xe2\x80\xba %s", ed_node_title(e, source, title, sizeof title), pin_label);
        kit_prop_begin(k, "source", "Source");
        kit_label(k, "v", "dim", text);
        kit_prop_end(k);
        kit_prop_begin(k, "type", "Type");
        kit_label(k, "v", "dim", sm_type_label(pin, text, sizeof text));
        kit_prop_end(k);
        snprintf(text, sizeof text, "%d", local_uses(gr, local->id));
        kit_prop_begin(k, "uses", "Uses");
        kit_label(k, "v", "dim", text);
        kit_prop_end(k);
    }
    kit_panel_end(k);
}

void studio_member_properties(studio *s)
{
    const behavior_variable *v = sm_variable(s);
    graph *g = sm_graph(s);
    const graph_local *local = sm_local(s);
    if (v) {
        variable_properties(s, v);
    } else if (g) {
        graph_properties(s, g);
    } else if (local) {
        local_properties(s, local);
    } else {
        kit_label(s->kit, "empty", "prop-empty", "No member selected");
    }
}

/* ---------- Type search ---------- */

/* Exec for a macro's pins, basic types, then registered structures and
   enums. */
static type_choice *type_choices(bool exec)
{
    type_choice *choices = NULL;
    if (exec) {
        apush(choices, EXEC);
    }
    for (int i = 0; i < TYPE_COUNT; i++) {
        apush(choices, TYPES[i]);
    }
    for (int i = 0; i < registry_type_count(); i++) {
        const type_desc *type = registry_type(i);
        apush(choices, ((type_choice){ type->name, PIN_STRUCT, type->name }));
    }
    for (int i = 0; i < registry_enum_count(); i++) {
        const enum_desc *type = registry_enum(i);
        apush(choices, ((type_choice){ type->name, PIN_ENUM, type->name }));
    }
    return choices;
}

/* `basic` counts the choices listed as types. */
static kit_command_item type_item(const type_choice *choice, int index, int basic)
{
    const char *group = index < basic              ? "Types"
                      : choice->type == PIN_STRUCT ? "Structures"
                                                   : "Enums";
    uint32_t icon = choice->type == PIN_FLOW   ? KIT_ICON_PLAY
                  : choice->type == PIN_STRUCT ? KIT_ICON_COMPONENT
                  : choice->type == PIN_ENUM   ? KIT_ICON_FIELD
                                               : KIT_ICON_VARIABLE;
    return (kit_command_item){ .label = choice->label, .group = group, .icon = icon };
}

/* The picked pin of a function or macro, and its side. */
static const pin_spec *picked_pin(const studio_members *m, graph *g, bool *result, int *index)
{
    for (int side = 0; g && side < 2; side++) {
        int output = 0;
        const pin_spec *pin = graph_pin_by_id(signature_node(g, side), m->pick.pin_id, &output, index);
        if (pin) {
            *result = side;
            return pin;
        }
    }
    return NULL;
}

static void apply_type(studio *s, const type_choice *choice)
{
    editor *e = member_editor(s);
    studio_members *m = &s->members;
    char *before = ed_snapshot(e);
    if (!m->pick.pin_id) {
        sm_record(
            e,
            before,
            behavior_asset_retype_variable(
                e->asset,
                m->pick.id,
                choice->type,
                choice->type_name
            ) == 0
        );
        return;
    }
    graph *g = behavior_asset_find_graph(e->asset, m->pick.id);
    bool result = false;
    int index = -1;
    const pin_spec *pin = picked_pin(m, g, &result, &index);
    pin_spec type = { .kind = choice->type == PIN_FLOW ? PIN_EXEC : PIN_DATA,
                      .type = choice->type,
                      .type_name = choice->type_name,
                      .container = pin ? pin->container : PIN_CONTAINER_SINGLE };
    sm_record(e, before, pin && graph_signature_retype(e->asset, g, result, index, &type) == 0);
}

bool studio_member_palette(studio *s)
{
    studio_members *m = &s->members;
    if (!m->pick.open) {
        return false;
    }
    if (!kit_dropdown_is_open(s->kit, "member-type") || !studio_graph_active(s)) {
        m->pick.open = false;
        return false;
    }
    graph *g = m->pick.pin_id
                 ? behavior_asset_find_graph(member_editor(s)->asset, m->pick.id)
                 : NULL;
    bool exec = g && g->kind == GRAPH_MACRO;
    bool result = false;
    int index = -1;
    const pin_spec *pin = g ? picked_pin(m, g, &result, &index) : NULL;
    const behavior_variable *variable = !m->pick.pin_id
                                          ? behavior_asset_find_variable_by_id(
                                                member_editor(s)->asset,
                                                m->pick.id
                                            )
                                          : NULL;
    type_choice *choices = type_choices(exec);
    kit_command_item *items = NULL;
    for (int i = 0; i < acount(choices); i++) {
        kit_command_item item = type_item(&choices[i], i, TYPE_COUNT + exec);
        item.current = pin
                         ? same_type(pin->type, pin->type_name, &choices[i])
                         : variable &&
                               same_type(variable->type, variable->type_name, &choices[i]);
        apush(items, item);
    }
    int chosen = kit_dropdown(s->kit, "member-type", items, acount(items));
    if (chosen >= 0) {
        apply_type(s, &choices[chosen]);
        m->pick.open = false;
    }
    afree(items);
    afree(choices);
    return true;
}
