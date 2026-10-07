#include "assets/code_source_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/vector_nodes.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <cute_math.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static code_source *method_source(const char *expression)
{
    char text[1024];
    snprintf(
        text,
        sizeof text,
        "behavior Methods { state velocity: struct<\"CF_V2\"> = {x:3,y:4}; "
        "state normalized: struct<\"CF_V2\"> = {x:0,y:0}; "
        "state scalar: float = 0; on Tick() { %s; } }",
        expression
    );
    return code_source_parse(text);
}

static void method_runtime(const char *expression, float x, float y, float scalar)
{
    code_source *source = method_source(expression);
    behavior_asset *asset = code_source_compile_behavior(source);
    for (int i = 0; !asset && i < acount(source->diagnostics); i++) {
        fprintf(
            stderr,
            "Vector method %s: %s\n",
            expression,
            source->diagnostics[i].message
        );
    }
    test_check(asset != NULL, "Static vector methods compile into a native behavior");
    if (asset) {
        bool native_normalize = false;
        for (int i = 0; i < acount(asset->graphs); i++) {
            graph *g = asset->graphs[i];
            for (int j = 0; j < acount(g->nodes); j++) {
                native_normalize |= g->nodes[j]->desc == &NODE_Normalize_Vector_2;
            }
        }
        test_check(native_normalize == (strstr(expression, ".norm()") != NULL), "Vector methods use the existing normalization descriptor and ordinary projections add no normalization");
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        flow_error error = { 0 };
        if (instance) {
            behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        }
        const CF_V2 *result = instance ? instance->variables[1].ptr : NULL;
        test_check(
            result && !behavior_instance_last_failure(instance) &&
                fabsf(result->x - x) < 0.00001f && fabsf(result->y - y) < 0.00001f &&
                fabsf(instance->variables[2].f - scalar) < 0.00001f,
            "Static method receivers, chaining, projections and arithmetic "
            "preserve runtime results"
        );
        behavior_instance_destroy(instance);
        char *formatted = code_source_format(asset);
        code_source *roundtrip = formatted ? code_source_parse(formatted) : NULL;
        behavior_asset *rebuilt = roundtrip ? code_source_compile_behavior(roundtrip)
                                            : NULL;
        for (int i = 0; !rebuilt && roundtrip && i < acount(roundtrip->diagnostics);
             i++) {
            fprintf(
                stderr,
                "Vector method roundtrip %s: %s\n",
                expression,
                roundtrip->diagnostics[i].message
            );
        }
        if (!rebuilt) {
            fprintf(stderr, "Vector method formatted %s: %s\n", expression, formatted ? formatted : "<format failed>");
        }
        test_check(rebuilt != NULL, "Static methods roundtrip through canonical executable source");
        behavior_asset_free(rebuilt);
        code_source_free(roundtrip);
        free(formatted);
        behavior_asset_free(asset);
    }
    code_source_free(source);
}

static void aggregate_capture_projection()
{
    code_source *source = code_source_parse(
        "behavior VectorProjection { state angle: double = 0; on Tick() { "
        "angle = atan2(&component(\"GAME.GameAim\", \"direction\").y, "
        "&component(\"GAME.GameAim\", \"direction\").x); } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    test_check(asset != NULL, "Component vector field projections compile with static method recognition enabled");
    bool preserved = false;
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            const char *formula = n->desc == &NODE_BehaviorExpression
                                    ? behavior_expression_source(asset, n)
                                    : NULL;
            preserved |= formula &&
                         strstr(formula, "atan2(_code_input_1.y, _code_input_1.x)") !=
                             NULL;
        }
    }
    test_check(preserved, "A shared component vector capture retains distinct x and y projections in its expression");
    behavior_asset_free(asset);
    code_source_free(source);
}

static void component_alias_method()
{
    code_source *source = code_source_parse(
        "behavior AliasMethod { state normalized: struct<\"CF_V2\"> = "
        "{x:0,y:0}; "
        "on Tick() { normalized = vec2(1, 2) + &component(GameVelocity).norm() "
        "* "
        "0.5; } }"
    );
    behavior_asset *asset = code_source_compile_behavior(source);
    for (int i = 0; !asset && i < acount(source->diagnostics); i++) {
        fprintf(stderr, "Vector alias method: %s\n", source->diagnostics[i].message);
    }
    test_check(asset != NULL, "A component alias of vec2 supports norm inside a larger vector expression");
    bool native = false;
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            native |= g->nodes[j]->desc == &NODE_Normalize_Vector_2;
        }
    }
    test_check(native, "Component aliases lower norm to the existing native vector normalization operation");
    behavior_asset_free(asset);
    code_source_free(source);
}

void run_code_method_checks()
{
    method_runtime("normalized = velocity.norm()", 0.6f, 0.8f, 0);
    method_runtime("normalized = velocity + velocity.norm()", 3.6f, 4.8f, 0);
    method_runtime("normalized = get_variable<\"velocity\">().Value.norm()", 0.6f, 0.8f, 0);
    method_runtime("normalized = velocity.norm().norm() * 2", 1.2f, 1.6f, 0);
    method_runtime("scalar = velocity.norm().x + 1", 0, 0, 1.6f);
    method_runtime("scalar = atan2(velocity.y, velocity.x)", 0, 0, atan2f(4, 3));
    method_runtime("normalized = (velocity + velocity).norm()", 0.6f, 0.8f, 0);
    method_runtime("normalized = vec2(0, 0).norm()", 0, 0, 0);
    code_source *parameter = code_source_parse(
        "library VectorParameter { pure function Unit(v: struct<\"CF_V2\">) "
        "-> (Result: struct<\"CF_V2\">) { result Result: struct<\"CF_V2\"> = "
        "v.norm(); } }"
    );
    behavior_asset *parameter_asset = code_source_compile_behavior(parameter);
    test_check(parameter_asset != NULL, "Vector methods accept inferred callable parameter receivers");
    behavior_asset_free(parameter_asset);
    code_source_free(parameter);
    code_source *invalid = method_source("scalar = scalar.norm()");
    bool typed_error = false;
    for (int i = 0; i < acount(invalid->diagnostics); i++) {
        typed_error |= strstr(invalid->diagnostics[i].message, "norm() requires a vec2 receiver") !=
                       NULL;
    }
    test_check(typed_error, "A scalar receiver produces a clear typed method diagnostic");
    code_source_free(invalid);
    aggregate_capture_projection();
    component_alias_method();
}
