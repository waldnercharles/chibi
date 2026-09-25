/* The Member tab in Properties: the details of the member selected in
 * Members, as Unreal's Details shows a My Blueprint selection. A variable has
 * its type, container, category and default; a function its purity and its
 * inputs and outputs; a named value its name and source. Types are chosen
 * from a search over basic types, structures and enums (Unreal's pin type
 * picker), on the command surface. */
#include "editor/studio/studio_members_internal.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <stdio.h>
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
   `id` and `pin_id` (see studio_members.pick). */
static void type_button(studio *s, const pin_spec *pin, uint64_t id, uint64_t pin_id)
{
    kit *k = s->kit;
    char label[96];
    kit_push(k, "type", NULL, "type-pick");
    sm_swatch(s, "swatch", pin);
    kit_button_desc d = { .label = sm_type_label(pin, label, sizeof label),
                          .variant = KIT_SUBTLE,
                          .cls = "type-button",
                          .flags = locked(s) ? KIT_DISABLED : 0 };
    if (kit_button(k, "pick", &d)) {
        s->members.pick.open = true;
        s->members.pick.id = id;
        s->members.pick.pin_id = pin_id;
        kit_command_open(k, "Search types");
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

/* The default edits as the canvas edits pin values: numbers and vectors
   scrub, and a scrub is one undo step. */
static void default_value(studio *s, const behavior_variable *v)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    studio_members *m = &s->members;
    unsigned flags = locked(s) ? KIT_READONLY : 0;
    pin_spec pin = sm_variable_pin(v);
    pin_value value = v->default_value;
    const enum_desc *enumeration = v->type == PIN_ENUM
                                     ? registry_enum_by_name(v->type_name)
                                     : NULL;
    int width = v->container == PIN_CONTAINER_SINGLE ? pin_vector_width(&pin) : 0;
    bool changed = false, streaming = false, committed = false;
    float vector[4] = { 0 };
    char text[256];
    kit_prop_begin(k, "value", "Value");
    if (v->container != PIN_CONTAINER_SINGLE) {
        kit_label(k, "v", "dim", sm_type_label(&pin, text, sizeof text));
    } else if (enumeration) {
        changed = enum_control(k, enumeration, &value, flags);
    } else if (v->type == PIN_BOOL) {
        bool b = value.b;
        changed = kit_checkbox(k, "v", &b, NULL, flags);
        value.b = b;
    } else if (width > 0) {
        static const char *const AXES[] = { "X", "Y", "Z", "W" };
        if (value.ptr) {
            memcpy(vector, value.ptr, (size_t)width * sizeof *vector);
        }
        for (int a = 0; a < width; a++) {
            char key[8];
            snprintf(key, sizeof key, "v%d", a);
            double axis = vector[a];
            kit_number_desc d = { .precision = 2, .step = 0.1, .addon = AXES[a], .flags = flags };
            if (kit_number(k, key, &axis, &d)) {
                vector[a] = (float)axis;
                changed = streaming = true;
            }
            committed |= kit_item_committed(k);
        }
        value.ptr = vector;
    } else if (v->type == PIN_STR || v->type == PIN_NAME) {
        snprintf(text, sizeof text, "%s", value.str ? value.str : "");
        changed = kit_text_field(k, "v", text, sizeof text, NULL, flags);
        value.str = text;
    } else if (v->type >= PIN_I8 && v->type <= PIN_F64) {
        bool integer = v->type < PIN_F32;
        double number = number_of(&value);
        kit_number_desc d = { .integer = integer,
                              .precision = integer ? 0 : 2,
                              .step = integer ? 1 : 0.1,
                              .flags = flags };
        changed = streaming = kit_number(k, "v", &number, &d);
        committed = kit_item_committed(k);
        set_number(&value, number);
    } else {
        kit_label(k, "v", "dim", sm_type_label(&pin, text, sizeof text));
    }
    kit_prop_end(k);
    if (!changed || locked(s)) {
        m->value_editing &= !committed;
        return;
    }
    if (!streaming || !m->value_editing) {
        ed_snapshot_before(e);
    }
    m->value_editing = streaming && !committed;
    behavior_asset_set_variable_default(e->asset, v->id, &value);
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
        if (kit_text_field(k, "v", name, sizeof name, NULL, flags) && name[0]) {
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

static void signature_edit(studio *s, char *before, bool ok)
{
    if (!sm_record(member_editor(s), before, ok)) {
        sm_refused(s, "A parameter has that name");
    }
}

/* One parameter or result: its name, its type and a remove button. Returns
   true when an edit changed the signature. */
static bool signature_row(studio *s, graph *g, bool result, int index)
{
    kit *k = s->kit;
    editor *e = member_editor(s);
    node *owner = signature_node(g, result);
    const pin_spec *pin = result ? &owner->input_pins[index]
                                 : &owner->output_pins[index];
    unsigned flags = locked(s) ? KIT_READONLY : 0;
    char key[32], name[128];
    snprintf(key, sizeof key, "p%llu", (unsigned long long)pin->id);
    snprintf(name, sizeof name, "%s", pin->name ? pin->name : "");
    kit_push(k, key, NULL, "param-row");
    bool renamed = kit_text_field(k, "name", name, sizeof name, NULL, flags) &&
                   name[0];
    type_button(s, pin, g->graph_id, pin->id);
    kit_button_desc remove = { .icon = KIT_ICON_TRASH,
                               .tip = "Remove",
                               .flags = locked(s) ? KIT_DISABLED : 0 };
    bool removed = kit_button(k, "remove", &remove);
    kit_pop(k);
    if (renamed && strcmp(name, pin->name)) {
        char *before = ed_snapshot(e);
        signature_edit(s, before, function_signature_rename(e->asset, g, result, index, name) == 0);
        return true;
    }
    if (removed) {
        char *before = ed_snapshot(e);
        sm_record(e, before, function_signature_remove(e->asset, g, result, index) == 0);
        return true;
    }
    return false;
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

/* A new parameter is a Float, named past the names in use. */
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
    pin_spec pin = { .name = name, .type = PIN_F32, .type_name = "float" };
    char *before = ed_snapshot(e);
    sm_record(e, before, function_signature_add(e->asset, g, result, &pin) == 0);
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
            if (function_signature_declared(pin) && signature_row(s, g, result, p)) {
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
    bool pure = function && function_signature_pure(g);
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
            name[0] && strcmp(name, g->name)) {
            editor_command rename = { .kind = EDITOR_COMMAND_RENAME_GRAPH,
                                      .data.rename_graph = { g->graph_id, name } };
            if (editor_command_apply(e, rename) != 0) {
                sm_refused(s, "A graph has that name");
            }
        }
        kit_prop_end(k);
        if (function) {
            kit_prop_begin(k, "pure", "Pure");
            if (kit_checkbox(k, "v", &pure, NULL, locked(s) ? KIT_DISABLED : 0)) {
                char *before = ed_snapshot(e);
                if (!sm_record(e, before, function_signature_set_pure(e->asset, g, pure) == 0)) {
                    sm_refused(s, "The function runs nodes in order");
                }
            }
            kit_prop_end(k);
        }
    }
    kit_panel_end(k);
    if (function) {
        signature_panel(s, g, false);
        signature_panel(s, g, true);
    }
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

/* Basic types, then registered structures and enums. */
static type_choice *type_choices()
{
    type_choice *choices = NULL;
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

static const char *type_group(const type_choice *choice, int index)
{
    return index < TYPE_COUNT         ? "Types"
         : choice->type == PIN_STRUCT ? "Structures"
                                      : "Enums";
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
    bool ok = false;
    for (int result = 0; g && g->kind == GRAPH_FUNCTION && result < 2; result++) {
        node *owner = signature_node(g, result);
        int output = 0, index = -1;
        const pin_spec *pin = owner
                                ? graph_pin_by_id(owner, m->pick.pin_id, &output, &index)
                                : NULL;
        if (pin) {
            pin_spec type = { .type = choice->type,
                              .type_name = choice->type_name,
                              .container = pin->container };
            ok = function_signature_retype(e->asset, g, result, index, &type) == 0;
            break;
        }
    }
    sm_record(e, before, ok);
}

bool studio_member_palette(studio *s)
{
    studio_members *m = &s->members;
    if (!m->pick.open) {
        return false;
    }
    if (!kit_command_is_open(s->kit) || !studio_graph_active(s)) {
        m->pick.open = false;
        return false;
    }
    type_choice *choices = type_choices();
    kit_command_item *items = NULL;
    for (int i = 0; i < acount(choices); i++) {
        apush(
            items,
            ((kit_command_item){ .label = choices[i].label,
                                 .group = type_group(&choices[i], i),
                                 .icon = choices[i].type == PIN_STRUCT ? KIT_ICON_COMPONENT
                                       : choices[i].type == PIN_ENUM ? KIT_ICON_FIELD
                                                                     : KIT_ICON_VARIABLE })
        );
    }
    int chosen = kit_command(s->kit, items, acount(items));
    if (chosen >= 0) {
        apply_type(s, &choices[chosen]);
        m->pick.open = false;
    }
    afree(items);
    afree(choices);
    return true;
}
