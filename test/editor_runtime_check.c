#include "../app/cute_host.h"
#include "../core/blueprint.h"
#include "../core/instance.h"
#include "../core/registry.h"
#include "../editor/editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const editor_event_param EDITOR_TICK_PARAMS[] = {
    { "DeltaSeconds", PIN_F64, "double" },
};
static const editor_event_signature EDITOR_EVENTS[] = {
    { "Begin", NULL, 0 },
    { "Tick", EDITOR_TICK_PARAMS, 1 },
};

int main(int argc, char **argv)
{
    CF_Result result = cf_make_app(
        "editor-runtime-check",
        0,
        0,
        0,
        320,
        240,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_NO_AUDIO_BIT,
        argc ? argv[0] : "editor-runtime-check"
    );
    if (cf_is_error(result)) {
        return 1;
    }
    cf_app_init_imgui();
    blueprint *source = blueprint_new("SourceBehavior");
    blueprint *other = blueprint_new("OtherBehavior");
    graph *function = source ? blueprint_add_function(source, "Function", 0) : NULL;
    node *event = other
                    ? graph_add_node(other->graph, registry_node_by_name("Event"))
                    : NULL;
    editor *editor = source ? editor_new(source) : NULL;
    if (editor) {
        editor_set_event_catalog(editor, EDITOR_EVENTS, 2);
    }
    int ok = editor && function && event &&
             graph_set_event_name(other->graph, event, "Ping") == 0 &&
             editor_open_graph(editor, function->graph_id) == 0 &&
             editor_active_graph(editor) &&
             editor_active_graph(editor)->graph_id == function->graph_id &&
             editor_open_blueprint(editor, other, 0) == 0 &&
             editor_blueprint(editor) &&
             strcmp(editor_blueprint(editor)->name, "OtherBehavior") == 0 &&
             editor_active_graph(editor) == editor_blueprint(editor)->graph;
    printf("%s: asset-correct graph opening\n", ok ? "PASS" : "FAIL");
    if (ok) {
        editor_tick(editor, 0.0);
        unsigned revision = editor_edit_revision(editor);
        node *added = editor_spawn(editor, registry_node_by_name("Event"), 0.0f, 0.0f);
        editor_tick(editor, 0.5);
        node *tick = editor_spawn(editor, registry_node_by_name("Event"), 80.0f, 0.0f);
        editor_tick(editor, 1.0);
        ok = added && tick && editor_edit_revision(editor) > revision &&
             added->reference_name && strcmp(added->reference_name, "Begin") == 0 &&
             added->output_count == 1 && tick->reference_name &&
             strcmp(tick->reference_name, "Tick") == 0 &&
             tick->output_count == 2 && tick->output_pins[1].type == PIN_F64;
        printf("%s: catalog Event creation uses a fixed signature\n", ok ? "PASS" : "FAIL");
        if (ok) {
            editor_preview_create(editor);
            node *literal = editor_spawn(editor, registry_node_by_name("Integer"), 160.0f, 0.0f);
            ok = literal && editor_preview_instance(editor) != NULL;
            editor_preview_reset(editor);
            ok = ok && editor_preview_instance(editor) == NULL;
            printf("%s: preview keeps an applied snapshot while editing\n", ok ? "PASS" : "FAIL");
        }
    }
    blueprint_instance *inspection = NULL;
    if (ok) {
        inspection = blueprint_instance_create((blueprint *)editor_blueprint(editor), NULL, NULL, NULL);
        editor_set_inspection_instance(editor, inspection);
        flow_run *run = editor_dispatch(editor, "Ping", (flow_inputs){ 0 }, NULL);
        ok = inspection && run && acount(inspection->activations) == 1 &&
             editor_preview_instance(editor) == NULL;
        printf("%s: selected entity receives editor dispatch\n", ok ? "PASS" : "FAIL");
        editor_set_inspection_instance(editor, NULL);
    }
    blueprint_instance_destroy(inspection);
    editor_free(editor);
    blueprint_free(other);
    cf_destroy_app();
    return ok ? 0 : 1;
}
