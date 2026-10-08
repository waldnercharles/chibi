#include "authoring/ui_events_adapter.h"

#include "authoring/preview.h"
#include "runtime/behaviors/source.h"
#include "support/array.h"

static int payload(void *context, const char *name, render_ui_event_field *fields, int capacity)
{
    game_preview_world *world = context;
    game_preview_runtime *live = world->runtime;
    game_source_registry *sources = live->sources;
    execution_module_event_info event;
    if (game_source_event(sources, name, &event)) {
        if (event.field_count > capacity) {
            return -1;
        }
        for (int i = 0; i < event.field_count; i++) {
            execution_module_event_field_info field;
            if (!game_source_event_field(sources, name, i, &field)) {
                return -1;
            }
            fields[i] = (render_ui_event_field){ .name = field.name,
                                                 .type = field.initial };
        }
        return event.field_count;
    }
    game_preview_definition *definition = game_preview_find_definition(world, name);
    const behavior_asset *asset = definition ? definition->applied : NULL;
    if (!asset || asset->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION) {
        return 0;
    }
    int count = acount(asset->variables);
    if (count > capacity) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        const behavior_variable *v = &asset->variables[i];
        fields[i] = (render_ui_event_field){ .name = v->name,
                                             .type = { .type = v->type,
                                                       .type_name = v->type_name,
                                                       .container = v->container } };
    }
    return count;
}
static int dispatch(void *context, uint64_t owner, const char *event, execution_arguments arguments)
{
    game_preview_world *world = context;
    game_preview_runtime *live = world->runtime;
    game_source_registry *sources = live->sources;
    double now = live->now;
    float delta = (float)live->delta;
    return owner ? game_source_dispatch(
                       sources,
                       ecs_entity_from_u64(owner),
                       event,
                       arguments.values,
                       arguments.count,
                       now,
                       delta
                   )
                 : game_source_dispatch_all(
                       sources,
                       event,
                       arguments.values,
                       arguments.count,
                       now,
                       delta
                   );
}
static bool initializing(void *context)
{
    return game_entities_initializing(((game_preview_world *)context)->runtime->entities);
}
render_ui_host authoring_ui_events(game_preview_world *world)
{
    return world ? (render_ui_host){ .context = world,
                                     .payload = payload,
                                     .dispatch = dispatch,
                                     .initializing = initializing }
                 : (render_ui_host){ 0 };
}
