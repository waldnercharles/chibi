#include "assets/code_components.h"
#include "assets/code_source.h"
#include "assets/code_source_intelligence.h"
#include "graph/model/registry.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int, const char *);

static int completion_index(const asset_source_completions *items, const char *name)
{
    for (int i = 0; i < acount(items->items); i++) {
        if (!strcmp(items->items[i].label, name)) {
            return i;
        }
    }
    return -1;
}
static code_source *complete_path(
    const char *path,
    const code_source_context *context,
    asset_source_completions *items
)
{
    char text[1024];
    snprintf(text, sizeof text, "behavior Probe { on Tick { %s", path);
    code_source *source = code_source_parse_context(text, NULL, context);
    code_intelligence_complete(source, (int)strlen(text), items);
    return source;
}
void run_code_ecs_intelligence_checks()
{
    asset_source_completions items;
    code_source_context empty = { 0 };
    code_source *source = complete_path("self.", &empty, &items);
    int components = 0;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        if (desc->component_kind != NODE_COMPONENT_GET_SELF) {
            continue;
        }
        bool ambiguous;
        const char *name = code_component_spelling(desc->component);
        if (code_component_named(name, &ambiguous) == desc->component && !ambiguous) {
            components++;
            test_check(completion_index(&items, name) >= 0, "An unknown entity offers every unambiguous registered component");
        }
    }
    test_check(components > 0, "ECS completion test covers the registered game components");
    asset_source_completions_free(&items);
    code_source_free(source);

    const char *common[] = { "GAME.GameVelocity" };
    const char *possible[] = { "GAME.GameVelocity", "GAME.GameParticleEmitter" };
    const code_prototype prototypes[] = { { "Player", common, 1 },
                                          { "Emitter", possible, 2 } };
    const char *owners[] = { "Player", "Emitter" };
    code_source_context context = { .prototypes = prototypes,
                                    .prototype_count = 2,
                                    .owner_prototypes = owners,
                                    .owner_prototype_count = 2 };
    source = complete_path("self.", &context, &items);
    int velocity = completion_index(&items, "Velocity");
    int emitter = completion_index(&items, "ParticleEmitter");
    int collider = completion_index(&items, "Collider");
    test_check(
        velocity >= 0 && emitter >= 0 && collider >= 0 && velocity < emitter &&
            emitter < collider,
        "Common owner components rank above possible components and other "
        "registered components"
    );
    asset_source_completions_free(&items);
    code_source_free(source);

    const char *spawned = "behavior Probe { on Begin { emitter := await "
                          "spawn(Emitter); emitter.";
    const code_reference references[] = { { CODE_REFERENCE_PROTOTYPE, "Emitter" } };
    context.references = references;
    context.reference_count = 1;
    source = code_source_parse_context(spawned, NULL, &context);
    code_intelligence_complete(source, (int)strlen(spawned), &items);
    emitter = completion_index(&items, "ParticleEmitter");
    collider = completion_index(&items, "Collider");
    test_check(emitter >= 0 && collider >= 0 && emitter < collider, "A spawned prototype ranks its components while retaining other entity components");
    asset_source_completions_free(&items);
    code_source_free(source);

    source = complete_path("self.ParticleEmitter.", &context, &items);
    int active = completion_index(&items, "Active");
    test_check(active >= 0 && completion_index(&items, "Parent") >= 0, "A component offers reflected boolean and entity fields with source spellings");
    if (active >= 0) {
        test_check(strstr(items.items[active].detail, "Active:") != NULL, "Field completion displays a code declaration");
    }
    asset_source_completions_free(&items);
    code_source_free(source);

    source = complete_path("self.ParticleEmitter.Parent.", &context, &items);
    test_check(completion_index(&items, "Velocity") >= 0, "Chained entity fields offer components on the referenced entity");
    asset_source_completions_free(&items);
    code_source_free(source);

    const char *text = "behavior Probe { on Tick { self.ParticleEmitter.Active "
                       "= true; } }";
    source = code_source_parse_context(text, NULL, &context);
    int offset = (int)(strstr(text, ".Active") - text) + 2;
    const code_semantic_symbol *field = code_intelligence_symbol_at(source, offset);
    test_check(field && field->type.type == PIN_BOOL && !field->renameable, "Field navigation resolves native boolean identity without allowing source rename");
    code_source_free(source);

    const char *event_draft = "behavior Probe { on "
                              "self.ParticleEmitter.Parent.ActionBinding.";
    source = code_source_parse_context(event_draft, NULL, &context);
    code_intelligence_complete(source, (int)strlen(event_draft), &items);
    int changed = completion_index(&items, "PressedChanged");
    test_check(changed >= 0 && !strcmp(items.items[changed].detail, "PressedChanged(pressed: bool)"), "An incomplete chained event path offers the native event's code signature");
    asset_source_completions_free(&items);
    code_source_free(source);

    const char
        *bind_draft = "behavior Probe { bind self.ParticleEmitter.Active = "
                      "self.ActionBinding.";
    source = code_source_parse_context(bind_draft, NULL, &context);
    code_intelligence_complete(source, (int)strlen(bind_draft), &items);
    test_check(completion_index(&items, "Pressed") >= 0 && completion_index(&items, "Source") < 0, "An incomplete bind value offers fields compatible with the destination type");
    asset_source_completions_free(&items);
    code_source_free(source);

    const char *native_event = "behavior Probe { on "
                               "self.ActionBinding.PressedChanged(pressed) { "
                               "self.ParticleEmitter.Active = pressed; } }";
    source = code_source_parse_context(native_event, NULL, &context);
    offset = (int)(strstr(native_event, "PressedChanged") - native_event) + 5;
    field = code_intelligence_symbol_at(source, offset);
    test_check(
        field && field->kind == CODE_SYMBOL_FIELD &&
            field->type.type == PIN_BOOL && field->node_id && field->pin_id,
        "Native event navigation resolves its watched component field identity"
    );
    code_source_free(source);
}
