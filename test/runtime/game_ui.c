#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/ui_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/ui.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "test/suite_internal.h"
#include "types/array.h"
#include "types/value_document.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void element_state()
{
    game_ui *ui = game_ui_new();
    uint64_t revision = game_ui_revision(ui);
    bool set = game_ui_set_text(ui, 7, "score", "12") &&
               game_ui_set_class(ui, 7, "score", "hot", true) &&
               game_ui_set_class(ui, 7, "score", "hot", false) &&
               game_ui_set_visible(ui, 7, "banner", false) &&
               game_ui_set_value(ui, 7, "charge", 0.5f) &&
               game_ui_focus(ui, 8, "resume");
    game_ui_element *score = game_ui_element_at(ui, 0);
    float charge = 0, missing = 0;
    test_check(
        set && game_ui_element_count(ui) == 4 && game_ui_revision(ui) > revision &&
            strcmp(score->text, "12") == 0 && acount(score->classes) == 1 &&
            !score->classes[0].on && game_ui_element_at(ui, 1)->visible == 0 &&
            game_ui_value(ui, 7, "charge", &charge) && charge == 0.5f &&
            !game_ui_value(ui, 7, "score", &missing) &&
            game_ui_element_at(ui, 3)->focus,
        "game UI keeps each element's text, classes, visibility, value and "
        "focus"
    );
    revision = game_ui_revision(ui);
    bool rejected = !game_ui_set_text(ui, 7, "", "x") &&
                    !game_ui_set_text(ui, 7, "score", NULL) &&
                    !game_ui_set_value(ui, 7, "charge", NAN) &&
                    !game_ui_set_class(ui, 7, "score", "", true);
    test_check(rejected && game_ui_revision(ui) == revision, "game UI rejects empty elements and invalid values without a change");
    game_ui_forget(ui, 7);
    test_check(
        game_ui_element_count(ui) == 1 && game_ui_element_at(ui, 0)->entity == 8,
        "forgetting an entity drops only its elements"
    );
    game_ui_reset(ui);
    test_check(game_ui_element_count(ui) == 0, "resetting game UI clears every element");
    game_ui_free(ui);
}

static void nodes()
{
    static const char *const names[] = { "Set UI Text",      "Set UI Class",
                                         "Set UI Visible",   "Set UI Value",
                                         "Set UI Data",      "Set UI Style",
                                         "Focus UI Element", "Get UI Value" };
    bool found = true;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        const node_desc *desc = NULL;
        for (int n = 0; n < GAME_UI_PACK.node_count; n++) {
            desc = strcmp(GAME_UI_PACK.nodes[n]->name, names[i]) == 0
                     ? GAME_UI_PACK.nodes[n]
                     : desc;
        }
        found &= desc && strcmp(desc->category, "UI") == 0 && desc->inputs &&
                 (desc->inputs[desc->pure_kind ? 0 : 1].flags & PIN_FLAG_DEFAULT_SELF);
    }
    test_check(found, "UI nodes target their own entity by default");
}

static bool data_is(const pin_value *value, const char *expected)
{
    char error[256];
    av_doc *doc = av_doc_new();
    av_doc *wanted = av_doc_read(expected, error, sizeof error);
    assert(wanted);
    bool same = av_equals(pin_value_to_document(doc, value), av_obj_get(av_doc_root(wanted), "value"));
    av_doc_free(doc);
    av_doc_free(wanted);
    return same;
}

/* Reflected values retain their native scalar, object and array types. */
static void data_values()
{
    CF_V2 points[2] = { { 1, 2 }, { 3, 4 } };
    pin_array *list = pin_array_new(PIN_STRUCT, "CF_V2", 2);
    for (int i = 0; i < 2; i++) {
        pin_array_push(
            list,
            &(pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &points[i] }
        );
    }
    bool written = data_is(&(pin_value){ .type = PIN_I32, .i = 7 }, "value 7\n") &&
                   data_is(&(pin_value){ .type = PIN_STR, .str = "Gem" }, "value \"Gem\"\n") &&
                   data_is(
                       &(pin_value){ .type = PIN_STRUCT,
                                     .type_name = "CF_V2",
                                     .ptr = &points[0] },
                       "value x=1.0 y=2.0\n"
                   ) &&
                   data_is(
                       &(pin_value){ .type = PIN_STRUCT,
                                     .container = PIN_CONTAINER_ARRAY,
                                     .type_name = "CF_V2",
                                     .ptr = list },
                       "(array)value {\n"
                       "    - x=1.0 y=2.0\n"
                       "    - x=3.0 y=4.0\n"
                       "}\n"
                   );
    pin_array_free(list);
    game_ui *ui = game_ui_new();
    av_doc *data = av_doc_new();
    av_value *items = av_arr(data);
    av_arr_append(items, av_sint(data, 1));
    av_arr_append(items, av_sint(data, 2));
    bool kept = game_ui_set_data(ui, 7, "items", items) &&
                !game_ui_set_data(ui, 7, "2items", av_sint(data, 1)) &&
                !game_ui_set_data(ui, 7, "items", NULL) &&
                game_ui_data_count(ui) == 1 &&
                av_equals(game_ui_data_at(ui, 0)->value, items);
    av_doc_free(data);
    kept &= av_get_int(av_arr_get(game_ui_data_at(ui, 0)->value, 1)) == 2;
    game_ui_forget(ui, 7);
    kept &= game_ui_data_count(ui) == 0;
    bool styled = game_ui_set_style(ui, 7, ".bar", "width", "40%") &&
                  game_ui_set_style(ui, 7, ".bar", "width", "") &&
                  !game_ui_set_style(ui, 7, ".bar", "Width", "1dp") &&
                  !game_ui_set_style(ui, 7, ".bar", "width", NULL) &&
                  acount(game_ui_element_at(ui, 0)->styles) == 1 &&
                  !game_ui_element_at(ui, 0)->styles[0].value;
    test_check(styled, "Set UI Style keeps each property's last value, an empty one returning it to the stylesheets");
    game_ui_free(ui);
    test_check(written && kept, "Set UI Data retains native values for self.<name> independently of their source document");
}

/* A behavior's Set UI Data stores its value, retyped from the wildcard's
   default, for the entity it names. */
static void data_node()
{
    behavior_asset *asset = behavior_asset_new("Scoreboard");
    graph *g = asset->graph;
    node *event = graph_add_node(g, registry_node_by_name("Event"));
    node *set = graph_add_node(g, registry_node_by_name("Set UI Data"));
    assert(event && set && graph_set_event_name(g, event, "Show") == 0);
    pin_value entity = { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 7 };
    pin_value name = { .type = PIN_NAME, .type_name = "name", .str = "score" };
    pin_value value = { .type = PIN_I32, .type_name = "int", .i = 17 };
    assert(
        graph_set_pin_default(g, set, 0, 1, &entity) == 0 &&
        graph_set_pin_default(g, set, 0, 2, &name) == 0 &&
        graph_set_pin_default(g, set, 0, 3, &value) == 0 &&
        graph_connect_exec(g, event, 0, set, 0, 0) == 0
    );
    game_ui *ui = game_ui_new();
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(instance && behavior_instance_bind_service(instance, "game.ui", ui) == 0);
    flow_error error;
    flow_run *run = behavior_instance_dispatch(instance, "Show", (flow_inputs){ 0 }, &error);
    bool stored = run && flow_run_status(run) == FLOW_COMPLETED &&
                  game_ui_data_count(ui) == 1 &&
                  game_ui_data_at(ui, 0)->entity == 7 &&
                  !strcmp(game_ui_data_at(ui, 0)->name, "score") &&
                  av_get_int(game_ui_data_at(ui, 0)->value) == 17;
    test_check(stored, "a behavior's Set UI Data stores its value for the entity it names");
    behavior_instance_destroy(instance);
    game_ui_free(ui);
    behavior_asset_free(asset);
}

void test_game_ui()
{
    element_state();
    nodes();
    data_values();
    data_node();
}
