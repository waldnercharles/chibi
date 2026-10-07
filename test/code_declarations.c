#include "assets/code_source_internal.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

void run_code_declaration_checks()
{
    code_source *source = code_source_parse(
        "behavior Counter { elapsed := 0; enabled := true; title := "
        "\"Counter\"; "
        "samples := [1, 2.5]; exact: int64 = 9007199254740993; "
        "on Tick() { elapsed += 0.25; } }"
    );
    test_check(source->asset != NULL, "Inferred behavior declarations accept numeric, boolean, string and array defaults");
    behavior_asset *asset = code_source_compile_behavior(source);
    if (asset) {
        test_check(
            asset->variables[0].type == PIN_F64 && asset->variables[3].type == PIN_F64 &&
                asset->variables[3].container == PIN_CONTAINER_ARRAY &&
                asset->variables[4].type == PIN_I64 &&
                asset->variables[4].default_value.i64 == INT64_C(9007199254740993),
            "Inferred numbers use double precision while explicit integer "
            "identity remains exact"
        );
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        test_check(instance != NULL, "Inferred declarations construct runtime state");
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
            test_check(!error.message && instance->variables[0].d == 0.5, "An integer-looking inferred default accumulates fractions across event invocations");
        }
        behavior_instance_destroy(instance);
        char *printed = code_source_format(asset);
        code_source *again = printed ? code_source_parse(printed) : NULL;
        test_check(
            again && again->asset && again->asset->variables[0].type == PIN_F64 &&
                again->asset->variables[4].default_value.i64 == INT64_C(9007199254740993),
            "Canonical printing retains inferred numeric storage and explicit "
            "integer precision"
        );
        code_source_free(again);
        free(printed);
    }
    behavior_asset_free(asset);
    code_source_free(source);

    source = code_source_parse("behavior Explicit { elapsed: number = 0; on Tick() { elapsed += 0.25; } }");
    test_check(
        source->asset && source->asset->variables[0].type == PIN_F64,
        "The number spelling selects double precision storage"
    );
    code_source_free(source);
    source = code_source_parse("behavior Empty { values := []; }");
    test_check(!source->asset && acount(source->diagnostics), "Empty collections require an explicit element type");
    code_source_free(source);
    source = code_source_parse("behavior Mixed { values := [true, 1]; }");
    test_check(!source->asset && acount(source->diagnostics), "Inference rejects mixed collection element types");
    code_source_free(source);
}
