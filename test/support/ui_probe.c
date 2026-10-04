/* Behaviors that record the UI events their entity receives, so a test sees
 * that a document's activation reached its entity, and with what payload
 * (test/suite_internal.h). */
#include "graph/document/events.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/suite_internal.h"

#include <assert.h>

static pin_spec observe_inputs[] = { { .name = "Exec", .kind = PIN_EXEC } };

/* What the probes made, until test_ui_probes_free. */
static behavior_asset **assets;
static node_desc **descs;

/* Records the event that ran it: its name is the node's, its payload copied,
   strings and a CF_V2 included. */
static int observe(node *n, flow_context *ctx)
{
    ui_probe *probe = flow_context_service(ctx, "test.ui_probe");
    assert(probe);
    flow_inputs inputs = flow_context_inputs(ctx);
    assert(inputs.count <= UI_PROBE_VALUES);
    probe->count++;
    snprintf(probe->event, sizeof probe->event, "%s", n->desc->name);
    probe->values = inputs.count;
    for (int i = 0; i < inputs.count; i++) {
        probe->payload[i] = inputs.values[i];
        if (inputs.values[i].type == PIN_STR) {
            snprintf(
                probe->text[i],
                sizeof probe->text[i],
                "%s",
                inputs.values[i].str
            );
            probe->payload[i].str = probe->text[i];
        } else if (inputs.values[i].type == PIN_STRUCT) {
            const float *v2 = inputs.values[i].ptr;
            probe->pointer[0] = v2[0];
            probe->pointer[1] = v2[1];
            probe->payload[i].ptr = probe->pointer;
        }
    }
    return FLOW_OK;
}

const node_desc *test_ui_probe_node(const char *event)
{
    node_desc *desc = xcalloc(1, sizeof *desc);
    *desc = (node_desc){ .name = xstrdup(event),
                         .inputs = observe_inputs,
                         .input_count = 1,
                         .flow_kind = NODE_FLOW_CUSTOM,
                         .flow = observe };
    apush(descs, desc);
    return desc;
}

void test_ui_probe_attach(game_live_world *live, ecs_entity entity, behavior_asset *asset, ui_probe *probe)
{
    apush(assets, asset);
    assert(game_behavior_register_asset(live->behaviors, asset->name, asset) == 0);
    assert(game_behavior_attach(live->behaviors, entity, asset->name) == 0);
    behavior_instance
        *instance = game_behavior_instance(live->behaviors, entity, asset->name);
    assert(instance && behavior_instance_bind_service(instance, "test.ui_probe", probe) == 0);
}

void test_ui_probe_events(
    game_live_world *live,
    ecs_entity entity,
    const char *const *events,
    int count,
    ui_probe *probe
)
{
    static int made;
    char name[32];
    snprintf(name, sizeof name, "UiProbe%d", ++made);
    behavior_asset *asset = behavior_asset_new(name);
    assert(asset && behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    for (int i = 0; i < count; i++) {
        node *event = graph_add_node(asset->graph, registry_node_by_name("Event"));
        assert(event && graph_set_event_name(asset->graph, event, events[i]) == 0);
        game_live_definition *declared = game_live_find_definition(live, events[i]);
        if (declared) {
            assert(graph_event_apply(asset->graph, event, declared->working) == 0);
        }
        node *record = graph_add_node(asset->graph, test_ui_probe_node(events[i]));
        assert(record && graph_connect_exec(asset->graph, event, 0, record, 0, 0) == 0);
    }
    test_ui_probe_attach(live, entity, asset, probe);
}

void test_ui_probes_free()
{
    for (int i = 0; i < acount(assets); i++) {
        behavior_asset_free(assets[i]);
    }
    afree(assets);
    assets = NULL;
    for (int i = 0; i < acount(descs); i++) {
        free((char *)descs[i]->name);
        free(descs[i]);
    }
    afree(descs);
    descs = NULL;
}
