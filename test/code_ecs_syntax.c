#include "assets/code_program_internal.h"
#include "assets/code_source.h"
#include "graph/nodes/snapshot_nodes.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int, const char *);

static void parsed(const code_source *source, const char *message)
{
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            const code_source_diagnostic *d = &source->diagnostics[i];
            int length = (int)strlen(source->text);
            int start = d->start >= 0 && d->start <= length ? d->start : 0;
            int end = d->end >= start && d->end <= length ? d->end : start;
            fprintf(
                stderr,
                "%s:%d:%d [%d..%d] %s; source: %.*s\n",
                message,
                d->line,
                d->column,
                d->start,
                d->end,
                d->message,
                end - start,
                source->text + start
            );
        }
    }
    test_check(source->asset != NULL, message);
}

static void eager_local()
{
    code_source *source = code_source_parse(
        "behavior SnapshotValue { state original := 1; state captured := 0; "
        "on Begin { saved := original; original = 2; await delay(Duration: 1); "
        "captured = saved; } }"
    );
    parsed(source, "ordinary inferred locals capture their initializer eagerly");
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "eager locals compile through the native graph backend");
    if (asset) {
        int snapshots = 0;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            snapshots += asset->graph->nodes[i]->desc == &NODE_Snapshot;
        }
        test_check(snapshots == 1, "a local initializer has one explicit execution capture");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Begin", (flow_inputs){ 0 }, &error);
            test_check(instance->variables[1].d == 0, "await suspends the activation before its saved value is consumed");
            behavior_instance_tick(instance, 2);
        }
        test_check(
            instance && !error.message && instance->variables[0].d == 2 &&
                instance->variables[1].d == 1,
            "mutating initializer state does not change its saved local value"
        );
        behavior_instance_destroy(instance);
        char *text = code_source_format(asset);
        test_check(text && strstr(text, "saved :=") && !strstr(text, "await snapshot"), "canonical source preserves an eager local declaration without native node boilerplate");
        code_source *roundtrip = text ? code_source_parse(text) : NULL;
        test_check(roundtrip && roundtrip->asset, "eager local canonical source reparses");
        code_source_free(roundtrip);
        free(text);
    }
    behavior_asset_free(asset);
    code_source_free(source);
}

void test_code_ecs_syntax()
{
    code_source *source = code_source_parse("behavior ImplicitOwner { state elapsed := 0; on Tick(dt) { elapsed += dt; } }");
    parsed(source, "host event payload names infer native types");
    test_check(
        source->asset && !strcmp(source->asset->owner_type_name, "ecs.entity"),
        "behaviors have an implicit entity owner"
    );
    code_source_free(source);
    source = code_source_parse("behavior BooleanEquality { state active := false; on Begin { if self.ActionBinding.Pressed == true { active = true; } } }");
    parsed(source, "boolean equality preserves boolean operands");
    code_source_free(source);
    source = code_source_parse("behavior InvalidBooleanEquality { on Begin { if true == 1 {} } }");
    test_check(!source->asset, "boolean equality rejects a numeric operand");
    code_source_free(source);
    source = code_source_parse("behavior UnusedPayload { on Tick {} }");
    parsed(source, "unused event payloads require no parameter declaration");
    code_source_free(source);
    source = code_source_parse("behavior TooMany { on Tick(dt, extra) {} }");
    test_check(!source->asset, "inferred event payloads reject excess parameters");
    code_source_free(source);
    source = code_source_parse("behavior InvalidShared { shared count := 0; }");
    test_check(!source->asset, "shared state is restricted to systems");
    code_source_free(source);
    source = code_source_parse("system Empty() { shared count := 0; count += dt; }");
    parsed(source, "parameterless systems support an implicit Tick body and shared state");
    test_check(source->asset && source->asset->variables[0].shared, "shared source declarations retain storage scope in the model");
    code_source_free(source);
    source = code_source_parse("system Motion(position: Transform, velocity: Velocity, damping: Igniter?) without ActionBinding { position += velocity * dt; if damping { position *= damping.heat; } }");
    parsed(source, "typed ECS system parameters support required, optional and excluded components");
    test_check(
        source->asset && acount(source->asset->component_parameters) == 3 &&
            source->asset->component_parameters[2].optional,
        "system component aliases survive native graph serialization"
    );
    code_source_free(source);
    source = code_source_parse("system Bad(damping: Igniter?) { damping.heat = 1; }");
    test_check(!source->asset, "optional component fields require a presence guard");
    code_source_free(source);
    source = code_source_parse("system Resume(damping: Igniter?) { if damping { await delay(Duration: 1); damping.heat = 1; } }");
    test_check(!source->asset, "optional component guards expire when an activation suspends");
    code_source_free(source);
    source = code_source_parse("system Reguard(damping: Igniter?) { if damping { await delay(Duration: 1); if damping != nil { damping.heat = 1; } } }");
    parsed(source, "a fresh guard permits optional component access after resuming");
    code_source_free(source);
    source = code_source_parse("system Collision(position: Transform) without Transform { position.x = 0; }");
    test_check(!source->asset, "a system parameter cannot be simultaneously excluded");
    code_source_free(source);
    source = code_source_parse("system NoParameters without ActionBinding { shared count := 0; count += dt; }");
    parsed(source, "systems without component parameters can declare exclusions and an implicit Tick body");
    code_source_free(source);
    source = code_source_parse("behavior Completion { action pending = delay(Duration: 1); after pending {} }");
    parsed(source, "after describes action completion handlers");
    code_source_free(source);
    source = code_source_parse("behavior NotEvent { action pending = delay(Duration: 1); on pending {} }");
    test_check(!source->asset, "on is reserved for actual events");
    code_source_free(source);
    source = code_source_parse("behavior ContactProjection { on ContactBegin { self.ActionBinding.Pressed = event.Other.ActionBinding.Pressed; } }");
    parsed(source, "native event output projections can continue through entity component paths");
    code_source_free(source);
    eager_local();
    source = code_source_parse("behavior PickupFields { value pickup = self.Pickup; on Tick(dt) { self.Pickup.CollectionAge = pickup.CollectionAge + dt; } }");
    parsed(source, "aggregate expressions resolve source-style reflected field names");
    code_source_free(source);
    source = code_source_parse("behavior PickupValue { value pickup = self.Pickup; state score: int = 0; on Begin { score = pickup.Value + 1; } }");
    parsed(source, "a reflected Value member takes precedence over an inferred component output name");
    code_source_free(source);
    source = code_source_parse("behavior Names { function Consume(Prototype: name = \"\") -> () { after entry { return(); } } on Begin { Consume(Prototype: self.SpawnRequest.Prototype); } }");
    parsed(source, "reflected prototype fields retain name coercion at callable boundaries");
    code_source_free(source);
}
