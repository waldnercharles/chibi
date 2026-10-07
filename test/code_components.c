#include "assets/code_components.h"

#include "assets/code_source.h"
#include "graph/document/library.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static void component_paths(const char *text)
{
    code_source *source = code_source_parse(text);
    test_check(source->asset != NULL, "Entity component chains lower without binding strings or casts");
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(stderr, "Component path: %s\n", source->diagnostics[i].message);
        }
        code_source_free(source);
        return;
    }
    char error[256];
    behavior_asset *asset = behavior_asset_link_libraries(source->asset, NULL, 0, error, sizeof error);
    test_check(asset != NULL, "Component reads and writes lower to registered native accessors");
    if (!asset) {
        code_source_free(source);
        return;
    }
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameParticleEmitter, NULL);
    ecs_world_register_component(world, &GAME_COMPONENT_GameActionBinding, NULL);
    ecs_entity owner = ecs_world_create_entity(world),
               parent = ecs_world_create_entity(world);
    GAME_GameParticleEmitter_add(world, owner, 0);
    GAME_GameActionBinding_add(world, parent, 0);
    ecs_world_apply_commands(world);
    *GAME_GameParticleEmitter_get(world, owner) = (GameParticleEmitter){
        .parent = ecs_entity_to_u64(parent),
        .rate = 2,
        .offset = { 2, 4, 6 }
    };
    GAME_GameActionBinding_get(world, parent)->pressed = true;
    pin_value self = { .type = PIN_HANDLE,
                       .type_name = "ecs.entity",
                       .u64 = ecs_entity_to_u64(owner) };
    behavior_instance *instance = behavior_instance_create(asset, &self, NULL, NULL);
    bool valid = instance &&
                 behavior_instance_bind_service(instance, "ecs.world", world) == 0;
    flow_error failure = { 0 };
    flow_run *run = valid
                      ? behavior_instance_dispatch(instance, "Step", (flow_inputs){ 0 }, &failure)
                      : NULL;
    const behavior_variable *observed = behavior_asset_find_variable_by_name(asset, "observed");
    pin_value value = { 0 };
    bool read = instance && observed &&
                behavior_instance_get_variable(instance, observed->id, &value) == 0;
    test_check(
        run && flow_run_status(run) == FLOW_COMPLETED && read && value.b &&
            GAME_GameParticleEmitter_get_const(world, owner)->active &&
            GAME_GameParticleEmitter_get_const(world, owner)->rate == 5 &&
            GAME_GameParticleEmitter_get_const(world, owner)->offset.x == 5 &&
            GAME_GameParticleEmitter_get_const(world, owner)->offset.y == 4 &&
            GAME_GameParticleEmitter_get_const(world, owner)->offset.z == 6 &&
            !GAME_GameActionBinding_get_const(world, parent)->pressed,
        "Chained reads sample the parent and assignments mutate the exact "
        "entity and field in execution order"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    ecs_world_free(world);
    code_source_free(source);
}

void run_code_components_checks()
{
    bool ambiguous;
    const ecs_component_desc *component = code_component_named("ParticleEmitter", &ambiguous);
    test_check(
        component == &GAME_COMPONENT_GameParticleEmitter && !ambiguous,
        "Short component names resolve their unique registered descriptor"
    );
    const type_field *field = code_component_field(component ? component->type : NULL, "BurstCount", &ambiguous);
    test_check(
        field && !strcmp(field->name, "burst_count") && !ambiguous,
        "PascalCase member spelling resolves the actual reflected field"
    );
    const char
        *text = "behavior Components { state observed:bool=false; "
                "value parent=self.ParticleEmitter.Parent; on Step { "
                "observed=self.ParticleEmitter.Parent.ActionBinding.Pressed; "
                "self.ParticleEmitter.Active=true; "
                "self.ParticleEmitter.Rate+=3; "
                "self.ParticleEmitter.Offset.X+=3; "
                "parent.ActionBinding.Pressed=false; } }";
    component_paths(text);
    code_source *source = code_source_parse(text);
    char *saved = source->asset ? code_source_persist(source) : NULL;
    test_check(saved != NULL, "Canonical component source prints independently of editor metadata");
    if (saved) {
        component_paths(saved);
    }
    free(saved);
    code_source_free(source);
    const char *invalid[] = {
        "behavior Invalid { on Step { self.NotAComponent.Value=1; } }",
        "behavior Invalid { on Step { self.ParticleEmitter.NotAField=1; } }",
        "behavior Invalid { on Step { self.ParticleEmitter.Active.NotAField=1; "
        "} }"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        source = code_source_parse(invalid[i]);
        test_check(!source->asset && acount(source->diagnostics), "Unknown component/member paths reject with a source diagnostic");
        code_source_free(source);
    }
}
