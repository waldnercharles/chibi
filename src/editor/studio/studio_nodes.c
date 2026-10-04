/* Studio's editor nodes (studio_nodes.h): their pins, types and catalog
 * entries, which every editor target registers, and the thin calls that
 * hand each one to the "studio" service, which only Studio's own world
 * binds (studio_ui.c) and studio_node_calls.c implements. */
#include "editor/studio/studio_nodes.h"

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/resources.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char MISSING[] = "Editor nodes require Studio";

/* ---------- Types ---------- */

#define TEXT_FIELD(owner, field, text)                                         \
    { .name = #field,                                                          \
      .type = PIN_STR,                                                         \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(const char *),                                            \
      .type_name = "const char *",                                             \
      .description = text }
#define INT_FIELD(owner, field, text)                                          \
    { .name = #field,                                                          \
      .type = PIN_I32,                                                         \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(int),                                                     \
      .type_name = "int",                                                      \
      .description = text }
#define BOOL_FIELD(owner, field, text)                                         \
    { .name = #field,                                                          \
      .type = PIN_BOOL,                                                        \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(bool),                                                    \
      .type_name = "bool",                                                     \
      .description = text }

static const type_field OBJECT_ROW_FIELDS[] = {
    TEXT_FIELD(studio_object_row, key, "The object's key: its entity as text."),
    TEXT_FIELD(studio_object_row, label, "The object's name."),
    TEXT_FIELD(studio_object_row, icon, "Its prototype's icon, a glyph."),
    TEXT_FIELD(studio_object_row, prototype, "Its prototype's name."),
    TEXT_FIELD(studio_object_row, drop, "The drag types a drop on it takes."),
    INT_FIELD(studio_object_row, depth, "Its depth under the scene's root."),
    INT_FIELD(studio_object_row, behaviors, "How many behaviors it runs."),
    BOOL_FIELD(studio_object_row, active, "It is the active object."),
    BOOL_FIELD(studio_object_row, marked, "It is selected, and not active."),
    BOOL_FIELD(studio_object_row, hidden, "Hidden in the editor."),
    BOOL_FIELD(studio_object_row, locked, "Locked: the Scene cannot pick it."),
    BOOL_FIELD(studio_object_row, renaming, "Its name is being edited in place."),
};
static const type_desc OBJECT_ROW_TYPE = {
    .name = STUDIO_OBJECT_ROW_TYPE,
    .size = sizeof(studio_object_row),
    .fields = OBJECT_ROW_FIELDS,
    .field_count = sizeof OBJECT_ROW_FIELDS / sizeof *OBJECT_ROW_FIELDS,
};

static const type_field CHOICE_FIELDS[] = {
    TEXT_FIELD(studio_choice, value, "What choosing it sends."),
    TEXT_FIELD(studio_choice, label, "Its label."),
    TEXT_FIELD(studio_choice, icon, "Its icon, a glyph."),
};
static const type_desc CHOICE_TYPE = {
    .name = STUDIO_CHOICE_TYPE,
    .size = sizeof(studio_choice),
    .fields = CHOICE_FIELDS,
    .field_count = sizeof CHOICE_FIELDS / sizeof *CHOICE_FIELDS,
};

/* ---------- Calls ---------- */

static const char *read_node(
    const eval_context *ctx,
    studio_node_op op,
    const pin_value *in,
    arena *storage,
    pin_value *out
)
{
    const studio_node_service *service = eval_context_service(ctx, "studio");
    return service ? service->run(service->studio, op, in, storage, out) : MISSING;
}

static const char *effect_node(void *service, studio_node_op op, const pin_value *in)
{
    const studio_node_service *studio = service;
    return studio->run(studio->studio, op, in, NULL, NULL);
}

/* Evaluates an effect's inputs and makes its host call. */
static int effect_flow(node *n, flow_context *ctx)
{
    void *service = flow_context_service(ctx, "studio");
    if (!service) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, MISSING);
    }
    pin_value values[4];
    int count = n->desc->input_count - 1;
    assert(count <= (int)(sizeof values / sizeof *values));
    for (int i = 0; i < count; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &values[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = n->desc->host_call->invoke(service, values, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_ARGUMENT, error)
                 : flow_context_continue(ctx, 0);
}

/* A query runs as its node executes, and its outputs stay for the
   activation, as Unreal's Get All Actors of Class is impure: a pure read
   runs again for each node that takes it. */
static int query_flow(node *n, flow_context *ctx, studio_node_op op)
{
    const studio_node_service *service = flow_context_service(ctx, "studio");
    if (!service) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, MISSING);
    }
    pin_value in[4], out[4];
    int inputs = n->desc->input_count - 1, outputs = n->desc->output_count - 1;
    assert(inputs <= 4 && outputs <= 4);
    for (int i = 0; i < inputs; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &in[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = service->run(service->studio, op, in, flow_context_scratch(ctx), out);
    if (error) {
        return flow_context_fail(ctx, FLOW_ERR_ARGUMENT, error);
    }
    for (int i = 0; i < outputs; i++) {
        *flow_context_output_value(ctx, i + 1) = out[i];
    }
    return flow_context_continue(ctx, 0);
}

/* ---------- Descriptors ---------- */

#define EXEC_IN { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC }
#define TEXT_PIN(label)                                                        \
    { .name = label, .type = PIN_STR, .type_name = "const char *" }
#define INT_PIN(label) { .name = label, .type = PIN_I32, .type_name = "int" }
#define BOOL_PIN(label) { .name = label, .type = PIN_BOOL, .type_name = "bool" }

static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ };
static const node_access WRITE = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec THEN_OUT[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };

/* A read with inputs `in`, or none for NULL, and outputs `out`. */
#define READ_NODE(ident, op, label, words, text, in, in_count, out)            \
    static const char *ident##_read(                                           \
        const eval_context *ctx,                                               \
        const pin_value *inputs,                                               \
        arena *storage,                                                        \
        pin_value *outputs                                                     \
    )                                                                          \
    {                                                                          \
        return read_node(ctx, op, inputs, storage, outputs);                   \
    }                                                                          \
    static node_desc ident = { .name = label,                                  \
                               .category = "Studio",                           \
                               .keywords = words,                              \
                               .description = text,                            \
                               .input_count = in_count,                        \
                               .inputs = in,                                   \
                               .output_count = sizeof out / sizeof *out,       \
                               .outputs = out,                                 \
                               .pure_kind = NODE_PURE_READ,                    \
                               .read_inputs = ident##_read,                    \
                               .access = &READ }
/* A query: Exec and inputs `in`, then Then and outputs `out`. */
#define QUERY_NODE(ident, op, label, words, text, in, out)                     \
    static int ident##_flow(node *n, flow_context *ctx)                        \
    {                                                                          \
        return query_flow(n, ctx, op);                                         \
    }                                                                          \
    static node_desc ident = { .name = label,                                  \
                               .category = "Studio",                           \
                               .keywords = words,                              \
                               .description = text,                            \
                               .input_count = sizeof in / sizeof *in,          \
                               .inputs = in,                                   \
                               .output_count = sizeof out / sizeof *out,       \
                               .outputs = out,                                 \
                               .flow_kind = NODE_FLOW_CUSTOM,                  \
                               .flow = ident##_flow,                           \
                               .display_class = NODE_DISPLAY_FLOW,             \
                               .access = &READ }
#define EFFECT_NODE(ident, op, label, words, text, in)                                     \
    static const char *ident##_invoke(void *service, const pin_value *inputs, pin_value *) \
    {                                                                                      \
        return effect_node(service, op, inputs);                                           \
    }                                                                                      \
    static const node_host_call ident##_CALL = { .service = "studio",                      \
                                                 .invoke = ident##_invoke,                 \
                                                 .missing_service = MISSING };             \
    static node_desc ident = { .name = label,                                              \
                               .category = "Studio",                                       \
                               .keywords = words,                                          \
                               .description = text,                                        \
                               .input_count = sizeof in / sizeof *in,                      \
                               .inputs = in,                                               \
                               .output_count = 1,                                          \
                               .outputs = THEN_OUT,                                        \
                               .flow_kind = NODE_FLOW_CUSTOM,                              \
                               .flow = effect_flow,                                        \
                               .host_call = &ident##_CALL,                                 \
                               .display_class = NODE_DISPLAY_FLOW,                         \
                               .access = &WRITE }

static pin_spec WINDOW_IN[] = { EXEC_IN, INT_PIN("First"), INT_PIN("Count") };
static pin_spec OBJECTS_OUT[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Rows",
      .type = PIN_STRUCT,
      .type_name = STUDIO_OBJECT_ROW_TYPE,
      .container = PIN_CONTAINER_ARRAY },
    INT_PIN("Shown"),
};
static pin_spec INFO_OUT[] = { TEXT_PIN("Title"), INT_PIN("Objects"), TEXT_PIN("Filter") };
static pin_spec SELECTION_OUT[] = { TEXT_PIN("Active"),
                                    INT_PIN("Count"),
                                    TEXT_PIN("Name"),
                                    TEXT_PIN("Prototype") };
static pin_spec OBJECT_IN[] = { TEXT_PIN("Object") };
static pin_spec SELECTED_OUT[] = { BOOL_PIN("Selected") };
static pin_spec ROW_OUT[] = { INT_PIN("Row") };
static pin_spec RENAMING_OUT[] = { TEXT_PIN("Object") };
static pin_spec STATE_OUT[] = { BOOL_PIN("Editable"), BOOL_PIN("Live"), BOOL_PIN("Mac") };
static pin_spec PROTOTYPE_IN[] = { TEXT_PIN("Prototype") };
static pin_spec ON_OUT[] = { BOOL_PIN("On") };
static pin_spec QUERY_IN[] = { EXEC_IN };
static pin_spec CHOICES_OUT[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Options",
      .type = PIN_STRUCT,
      .type_name = STUDIO_CHOICE_TYPE,
      .container = PIN_CONTAINER_ARRAY },
};

QUERY_NODE(
    SCENE_OBJECTS,
    STUDIO_NODE_SCENE_OBJECTS,
    "Scene Objects",
    "outliner hierarchy objects rows list window filter scene",
    "Lists Count objects from First, in scene order, of the Shown objects "
    "the object filter shows, as rows of the scene's hierarchy.",
    WINDOW_IN,
    OBJECTS_OUT
);
READ_NODE(
    SCENE_INFO,
    STUDIO_NODE_SCENE_INFO,
    "Scene Info",
    "scene title name objects count filter",
    "The open scene's Title, how many Objects it holds, and the object "
    "Filter the hierarchy and Up and Down follow.",
    NULL,
    0,
    INFO_OUT
);
READ_NODE(
    SELECTION,
    STUDIO_NODE_SELECTION,
    "Selection",
    "selected active object count name prototype",
    "The Active object's key, Name and Prototype, and how many objects are "
    "selected.",
    NULL,
    0,
    SELECTION_OUT
);
READ_NODE(
    OBJECT_SELECTED,
    STUDIO_NODE_OBJECT_SELECTED,
    "Object Selected",
    "is selected selection contains object",
    "Whether the object is in the selection.",
    OBJECT_IN,
    1,
    SELECTED_OUT
);
READ_NODE(
    OBJECT_ROW,
    STUDIO_NODE_OBJECT_ROW,
    "Object Row",
    "index position find object row filter hierarchy",
    "Where the object is among those the object filter shows, from 0; -1 "
    "when it shows none.",
    OBJECT_IN,
    1,
    ROW_OUT
);
READ_NODE(
    RENAMING,
    STUDIO_NODE_RENAMING,
    "Renaming Object",
    "rename name edit in place field",
    "The object whose name the hierarchy is editing in place (F2), or \"\".",
    NULL,
    0,
    RENAMING_OUT
);
READ_NODE(
    EDITOR_STATE,
    STUDIO_NODE_EDITOR_STATE,
    "Editor State",
    "editable playing live platform mac",
    "Whether the scene is Editable, whether it is Live (playing), and "
    "whether keys are a Mac's.",
    NULL,
    0,
    STATE_OUT
);
READ_NODE(
    SPAWN_BREAK,
    STUDIO_NODE_SPAWN_BREAK,
    "Breaks When Spawned",
    "debug break spawn prototype breakpoint",
    "Whether a run pauses when Prototype spawns (Break When Spawned).",
    PROTOTYPE_IN,
    1,
    ON_OUT
);
QUERY_NODE(
    PROTOTYPE_CHOICES,
    STUDIO_NODE_PROTOTYPE_CHOICES,
    "Prototype Choices",
    "prototypes add object list options icons",
    "Lists the scene's prototypes, with their icons, as options a "
    "ChoiceList lists.",
    QUERY_IN,
    CHOICES_OUT
);

static pin_spec SELECT_IN[] = { EXEC_IN,
                                TEXT_PIN("Object"),
                                BOOL_PIN("Toggle"),
                                BOOL_PIN("Extend") };
static pin_spec RENAME_IN[] = { EXEC_IN, TEXT_PIN("Object"), TEXT_PIN("Name") };
static pin_spec END_IN[] = { EXEC_IN };
static pin_spec HIDDEN_IN[] = { EXEC_IN, TEXT_PIN("Object"), BOOL_PIN("Hidden") };
static pin_spec LOCKED_IN[] = { EXEC_IN, TEXT_PIN("Object"), BOOL_PIN("Locked") };
static pin_spec OBJECT_EXEC_IN[] = { EXEC_IN, TEXT_PIN("Object") };
static pin_spec PROTOTYPE_EXEC_IN[] = { EXEC_IN, TEXT_PIN("Prototype") };
static pin_spec DROP_IN[] = { EXEC_IN,
                              TEXT_PIN("Object"),
                              TEXT_PIN("Type"),
                              TEXT_PIN("Payload") };
static pin_spec FILTER_IN[] = { EXEC_IN, TEXT_PIN("Filter") };

EFFECT_NODE(
    SELECT_OBJECT,
    STUDIO_NODE_SELECT_OBJECT,
    "Select Object",
    "select click selection toggle extend range shift",
    "Selects the object alone, or with Toggle adds it to or takes it out of "
    "the selection, or with Extend selects the objects the object filter "
    "shows from the anchor through it, as a hierarchy row's click does.",
    SELECT_IN
);
EFFECT_NODE(
    RENAME_OBJECT,
    STUDIO_NODE_RENAME_OBJECT,
    "Rename Object",
    "rename name object undo",
    "Renames the object as one Undo step; a name in use is refused with a "
    "notice.",
    RENAME_IN
);
EFFECT_NODE(
    END_RENAME,
    STUDIO_NODE_END_RENAME,
    "End Rename",
    "rename stop cancel name field in place",
    "Ends the in-place rename, so its field closes.",
    END_IN
);
EFFECT_NODE(
    SET_HIDDEN,
    STUDIO_NODE_SET_HIDDEN,
    "Set Object Hidden",
    "hide show eye visible editor view",
    "Hides the object in the editor's Scene, or shows it again (Hide in "
    "Editor).",
    HIDDEN_IN
);
EFFECT_NODE(
    SET_LOCKED,
    STUDIO_NODE_SET_LOCKED,
    "Set Object Locked",
    "lock unlock pick select scene",
    "Locks the object, so the Scene cannot pick it, or unlocks it.",
    LOCKED_IN
);
EFFECT_NODE(
    ISOLATE_OBJECT,
    STUDIO_NODE_ISOLATE,
    "Isolate Object",
    "hide others show only solo alone",
    "Shows the object alone, or every object once it already is, as "
    "Alt+click on an eye does.",
    OBJECT_EXEC_IN
);
EFFECT_NODE(
    ADD_OBJECT,
    STUDIO_NODE_ADD_OBJECT,
    "Add Object",
    "add create spawn place prototype instance",
    "Adds an object of Prototype to the scene, selected, as one Undo step.",
    PROTOTYPE_EXEC_IN
);
EFFECT_NODE(
    DROP_ON_OBJECT,
    STUDIO_NODE_DROP_ON_OBJECT,
    "Drop on Object",
    "drop drag attach behavior assign asset",
    "Drops Payload, of drag type Type, on the object: attaches a behavior or "
    "assigns an asset its fields name, as one Undo step.",
    DROP_IN
);
EFFECT_NODE(
    SET_FILTER,
    STUDIO_NODE_SET_FILTER,
    "Set Object Filter",
    "filter search find objects hierarchy",
    "Sets the object filter the hierarchy shows and Up and Down follow.",
    FILTER_IN
);
EFFECT_NODE(
    TOGGLE_SPAWN_BREAK,
    STUDIO_NODE_TOGGLE_SPAWN_BREAK,
    "Toggle Spawn Break",
    "debug break spawn prototype breakpoint",
    "Turns Break When Spawned on or off for Prototype.",
    PROTOTYPE_EXEC_IN
);

static node_desc *const NODES[] = {
    &SCENE_OBJECTS,     &SCENE_INFO,    &SELECTION,          &OBJECT_SELECTED,
    &OBJECT_ROW,        &RENAMING,      &EDITOR_STATE,       &SPAWN_BREAK,
    &PROTOTYPE_CHOICES, &SELECT_OBJECT, &RENAME_OBJECT,      &END_RENAME,
    &SET_HIDDEN,        &SET_LOCKED,    &ISOLATE_OBJECT,     &ADD_OBJECT,
    &DROP_ON_OBJECT,    &SET_FILTER,    &TOGGLE_SPAWN_BREAK,
};
static const type_desc *const TYPES[] = { &OBJECT_ROW_TYPE, &CHOICE_TYPE };
const registry_pack STUDIO_PACK = { .name = "studio",
                                    .nodes = NODES,
                                    .node_count = sizeof NODES / sizeof *NODES,
                                    .types = TYPES,
                                    .type_count = sizeof TYPES / sizeof *TYPES };

bool studio_node_is(const node_desc *desc)
{
    return desc->category && !strcmp(desc->category, "Studio");
}

bool studio_project_is(const char *root)
{
    char studio[PATH_MAX], project[PATH_MAX];
    return realpath(resource_studio(), studio) && realpath(root, project) &&
           !strcmp(studio, project);
}

int studio_nodes_check(const behavior_asset *asset, compile_report *report)
{
    int found = 0;
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        for (int i = 0; i < acount(graph->nodes); i++) {
            const node *n = graph->nodes[i];
            if (!studio_node_is(n->desc)) {
                continue;
            }
            compile_diagnostic d = { .severity = CMPL_ERROR,
                                     .graph_id = graph->graph_id,
                                     .node_id = n->id,
                                     .rule = CMPL_RULE_EDITOR_NODE };
            snprintf(d.message, sizeof d.message, "%s", MISSING);
            apush(report->items, d);
            report->error_count++;
            found++;
        }
    }
    return found;
}
