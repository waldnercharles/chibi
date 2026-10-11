#include "graph/nodes/studio_nodes.h"

#include "authoring/studio_definition.h"
#include "graph/model/native_definition.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>
static const char MISSING[] = "Editor nodes require Studio";
#define BANK(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)                       \
    static pin_spec ID##_inputs[] = { STUDIO_##ID##_INPUTS(GRAPH_NATIVE_PORT){ 0 } }; \
    static pin_spec ID##_outputs[] = { STUDIO_##ID##_OUTPUTS(GRAPH_NATIVE_PORT){ 0 } };
STUDIO_OPERATIONS(BANK)
#undef BANK
#define COUNT(A) ((int)(sizeof A / sizeof *A) - 1)
#define READ_NODE(ident, op, label, words, text)                                           \
    static node_desc ident = { .name = label,                                              \
                               .category = "Studio",                                       \
                               .keywords = words,                                          \
                               .description = text,                                        \
                               .input_count = COUNT(ident##_inputs),                       \
                               .inputs = COUNT(ident##_inputs) ? ident##_inputs : NULL,    \
                               .output_count = COUNT(ident##_outputs),                     \
                               .outputs = COUNT(ident##_outputs) ? ident##_outputs : NULL, \
                               .pure_kind = NODE_PURE_READ,                                \
                               .execution_call = &STUDIO_CALLS[op],                        \
                               .access = &STUDIO_READ_ACCESS }
/* A query: Exec and inputs `in`, then Then and outputs `out`. */
#define QUERY_NODE(ident, op, label, words, text)                                          \
    static node_desc ident = { .name = label,                                              \
                               .category = "Studio",                                       \
                               .keywords = words,                                          \
                               .description = text,                                        \
                               .input_count = COUNT(ident##_inputs),                       \
                               .inputs = COUNT(ident##_inputs) ? ident##_inputs : NULL,    \
                               .output_count = COUNT(ident##_outputs),                     \
                               .outputs = COUNT(ident##_outputs) ? ident##_outputs : NULL, \
                               .flow_kind = NODE_FLOW_CUSTOM,                              \
                               .host_call = &STUDIO_CALLS[op],                             \
                               .display_class = NODE_DISPLAY_FLOW,                         \
                               .access = &STUDIO_READ_ACCESS }
#define EFFECT_NODE(ident, op, label, words, text)                                         \
    static node_desc ident = { .name = label,                                              \
                               .category = "Studio",                                       \
                               .keywords = words,                                          \
                               .description = text,                                        \
                               .input_count = COUNT(ident##_inputs),                       \
                               .inputs = COUNT(ident##_inputs) ? ident##_inputs : NULL,    \
                               .output_count = COUNT(ident##_outputs),                     \
                               .outputs = COUNT(ident##_outputs) ? ident##_outputs : NULL, \
                               .flow_kind = NODE_FLOW_CUSTOM,                              \
                               .host_call = &STUDIO_CALLS[op],                             \
                               .display_class = NODE_DISPLAY_FLOW,                         \
                               .access = &STUDIO_WRITE_ACCESS }

QUERY_NODE(
    SCENE_OBJECTS,
    STUDIO_NODE_SCENE_OBJECTS,
    STUDIO_SCENE_OBJECTS_NAME,
    "outliner hierarchy objects rows list window filter scene",
    "Lists Count objects from First, in scene order, of the Shown objects "
    "the object filter shows, as rows of the scene's hierarchy."
);
READ_NODE(
    SCENE_INFO,
    STUDIO_NODE_SCENE_INFO,
    STUDIO_SCENE_INFO_NAME,
    "scene title name objects count filter",
    "The open scene's Title, how many Objects it holds, and the object "
    "Filter the hierarchy and Up and Down follow."
);
READ_NODE(
    SELECTION,
    STUDIO_NODE_SELECTION,
    STUDIO_SELECTION_NAME,
    "selected active object count name prototype",
    "The Active object's key, Name and Prototype, and how many objects are "
    "selected."
);
READ_NODE(
    OBJECT_SELECTED,
    STUDIO_NODE_OBJECT_SELECTED,
    STUDIO_OBJECT_SELECTED_NAME,
    "is selected selection contains object",
    "Whether the object is in the selection."
);
READ_NODE(
    OBJECT_ROW,
    STUDIO_NODE_OBJECT_ROW,
    STUDIO_OBJECT_ROW_NAME,
    "index position find object row filter hierarchy",
    "Where the object is among those the object filter shows, from 0; -1 "
    "when it shows none."
);
READ_NODE(
    RENAMING,
    STUDIO_NODE_RENAMING,
    STUDIO_RENAMING_NAME,
    "rename name edit in place field",
    "The object whose name the hierarchy is editing in place (F2), or \"\"."
);
READ_NODE(
    EDITOR_STATE,
    STUDIO_NODE_EDITOR_STATE,
    STUDIO_EDITOR_STATE_NAME,
    "editable playing live platform mac",
    "Whether the scene is Editable, whether it is Live (playing), and "
    "whether keys are a Mac's."
);
READ_NODE(
    SPAWN_BREAK,
    STUDIO_NODE_SPAWN_BREAK,
    STUDIO_SPAWN_BREAK_NAME,
    "debug break spawn prototype breakpoint",
    "Whether a run pauses when Prototype spawns (Break When Spawned)."
);
QUERY_NODE(
    PROTOTYPE_CHOICES,
    STUDIO_NODE_PROTOTYPE_CHOICES,
    STUDIO_PROTOTYPE_CHOICES_NAME,
    "prototypes add object list options icons",
    "Lists the scene's prototypes, with their icons, as options a "
    "ChoiceList lists."
);
EFFECT_NODE(
    SELECT_OBJECT,
    STUDIO_NODE_SELECT_OBJECT,
    STUDIO_SELECT_OBJECT_NAME,
    "select click selection toggle extend range shift",
    "Selects the object alone, or with Toggle adds it to or takes it out of "
    "the selection, or with Extend selects the objects the object filter "
    "shows from the anchor through it, as a hierarchy row's click does."
);
EFFECT_NODE(
    RENAME_OBJECT,
    STUDIO_NODE_RENAME_OBJECT,
    STUDIO_RENAME_OBJECT_NAME,
    "rename name object undo",
    "Renames the object as one Undo step; a name in use is refused with a "
    "notice."
);
EFFECT_NODE(
    END_RENAME,
    STUDIO_NODE_END_RENAME,
    STUDIO_END_RENAME_NAME,
    "rename stop cancel name field in place",
    "Ends the in-place rename, so its field closes."
);
EFFECT_NODE(
    SET_HIDDEN,
    STUDIO_NODE_SET_HIDDEN,
    STUDIO_SET_HIDDEN_NAME,
    "hide show eye visible editor view",
    "Hides the object in the editor's Scene, or shows it again (Hide in "
    "Editor)."
);
EFFECT_NODE(
    SET_LOCKED,
    STUDIO_NODE_SET_LOCKED,
    STUDIO_SET_LOCKED_NAME,
    "lock unlock pick select scene",
    "Locks the object, so the Scene cannot pick it, or unlocks it."
);
EFFECT_NODE(
    ISOLATE_OBJECT,
    STUDIO_NODE_ISOLATE,
    STUDIO_ISOLATE_OBJECT_NAME,
    "hide others show only solo alone",
    "Shows the object alone, or every object once it already is, as "
    "Alt+click on an eye does."
);
EFFECT_NODE(
    ADD_OBJECT,
    STUDIO_NODE_ADD_OBJECT,
    STUDIO_ADD_OBJECT_NAME,
    "add create spawn place prototype instance",
    "Adds an object of Prototype to the scene, selected, as one Undo step."
);
EFFECT_NODE(
    DROP_ON_OBJECT,
    STUDIO_NODE_DROP_ON_OBJECT,
    STUDIO_DROP_ON_OBJECT_NAME,
    "drop drag attach behavior assign asset",
    "Drops Payload, of drag type Type, on the object: attaches a behavior or "
    "assigns an asset its fields name, as one Undo step."
);
EFFECT_NODE(
    MOVE_OBJECTS,
    STUDIO_NODE_MOVE_OBJECTS,
    STUDIO_MOVE_OBJECTS_NAME,
    "move reparent reorder hierarchy selection",
    "Moves the dragged Object and its selected group before, inside or after "
    "Target, or to the root with an empty Target and Zone root. Preserves "
    "world "
    "transforms and stable identity in one Undo step."
);
EFFECT_NODE(
    SET_FILTER,
    STUDIO_NODE_SET_FILTER,
    STUDIO_SET_FILTER_NAME,
    "filter search find objects hierarchy",
    "Sets the object filter the hierarchy shows and Up and Down follow."
);
EFFECT_NODE(
    TOGGLE_SPAWN_BREAK,
    STUDIO_NODE_TOGGLE_SPAWN_BREAK,
    STUDIO_TOGGLE_SPAWN_BREAK_NAME,
    "debug break spawn prototype breakpoint",
    "Turns Break When Spawned on or off for Prototype."
);
static node_desc *const NODES[] = {
    &SCENE_OBJECTS,     &SCENE_INFO,    &SELECTION,      &OBJECT_SELECTED,
    &OBJECT_ROW,        &RENAMING,      &EDITOR_STATE,   &SPAWN_BREAK,
    &PROTOTYPE_CHOICES, &SELECT_OBJECT, &RENAME_OBJECT,  &END_RENAME,
    &SET_HIDDEN,        &SET_LOCKED,    &ISOLATE_OBJECT, &ADD_OBJECT,
    &DROP_ON_OBJECT,    &MOVE_OBJECTS,  &SET_FILTER,     &TOGGLE_SPAWN_BREAK,
};
static const type_desc *const TYPES[] = { &STUDIO_OBJECT_ROW_LAYOUT,
                                          &STUDIO_CHOICE_LAYOUT };
const registry_pack STUDIO_PACK = { .name = "studio",
                                    .nodes = NODES,
                                    .node_count = sizeof NODES / sizeof *NODES,
                                    .types = TYPES,
                                    .type_count = sizeof TYPES / sizeof *TYPES };

bool studio_node_is(const node_desc *desc)
{
    return desc->category && !strcmp(desc->category, "Studio");
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
