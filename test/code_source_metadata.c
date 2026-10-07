#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_check(int ok, const char *message);

static node *authored_expression(const code_source *source, const char *text)
{
    const code_program *program = source->program;
    for (int i = 0; program && i < acount(program->expressions); i++) {
        if (strstr(program->expressions[i].text, text)) {
            return program->expressions[i].native;
        }
    }
    return NULL;
}

static code_source *reload_source(const code_source *previous, const char *text)
{
    code_source *source = code_source_parse_context(text, NULL, &previous->context);
    char *clean = code_source_persist(previous);
    char *metadata = NULL;
    if (clean && !strcmp(clean, text)) {
        metadata = code_source_metadata_export_source(previous);
    } else {
        char *printed = code_source_format_with_metadata(previous->asset, &metadata);
        if (!printed || strcmp(printed, text)) {
            free(metadata);
            metadata = code_source_metadata_export(previous->asset);
        }
        free(printed);
    }
    bool restored = source->asset && code_source_metadata_restore(source, metadata);
    test_check(restored, "Optional metadata restores onto independently parsed executable source");
    free(clean);
    free(metadata);
    return source;
}

static behavior_asset *reload_asset(const behavior_asset *asset, const char *text)
{
    code_source *source = code_source_parse(text);
    char *metadata = NULL;
    char *printed = code_source_format_with_metadata(asset, &metadata);
    bool valid = printed && !strcmp(printed, text) && source->asset &&
                 code_source_metadata_restore(source, metadata);
    free(printed);
    behavior_asset *result = valid ? code_source_build_behavior(source) : NULL;
    free(metadata);
    code_source_free(source);
    return result;
}

static void annotation_free_sidecar()
{
    const char *text = "behavior Sidecar { state count:float = 0; on Tick { "
                       "count += 1; count += 1; } }";
    code_source *source = code_source_parse(text);
    test_check(source->asset != NULL, "Annotation-free source compiles independently");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    node *n = source->asset->graph->nodes[acount(source->asset->graph->nodes) - 1];
    n->placed = true;
    n->x = 42;
    n->y = 27;
    n->breakpoint = true;
    uint64_t id = n->id;
    char *clean = code_source_persist(source);
    test_check(
        clean && !strstr(clean, "@node") && !strstr(clean, "@authoring"),
        "Canonical code contains executable source without machine annotations"
    );
    const char *path = "build/code-source-sidecar-fixture.code";
    test_check(code_source_metadata_save(source, path), "Optional metadata saves in a separate sidecar");
    code_source *fresh = code_source_parse(clean);
    test_check(fresh->asset && code_source_metadata_load(fresh, path), "Independent source reload accepts matching sidecar");
    node *restored = fresh->asset ? graph_node_by_id(fresh->asset->graph, id) : NULL;
    test_check(
        restored && restored->placed && restored->x == 42 &&
            restored->y == 27 && restored->breakpoint,
        "Repeated statements restore their own layout and debugging identities "
        "from source anchors"
    );
    code_source *changed = code_source_parse_previous("behavior Sidecar { state count:float = 0; on Tick { count += 1; count += 1; count += 1; } }", fresh);
    test_check(changed->asset != NULL, "Ambiguous repeated statements remain valid executable code");
    test_check(!code_source_metadata_load(changed, path), "Stale sidecar is ignored after executable source changes");
    int diagnostics = acount(changed->diagnostics);
    test_check(
        !code_source_metadata_restore(changed, "{\"version\":2,\"id_counter\":0}") &&
            changed->asset && acount(changed->diagnostics) == diagnostics,
        "Malformed optional metadata cannot invalidate source or add "
        "compilation errors"
    );
    remove("build/code-source-sidecar-fixture.code.meta");
    code_source_free(changed);
    code_source_free(fresh);
    free(clean);
    code_source_free(source);
}

static void malformed_optional_metadata()
{
    code_source *source = code_source_parse("behavior MetadataErrors { value first=real(Value:1); value second=real(Value:2); }");
    test_check(source->asset != NULL, "Malformed optional metadata fixture compiles without metadata");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *json = code_source_metadata_export_source(source);
    for (int version = 0; version < 2; version++) {
        av_doc *doc = av_json_read(json);
        av_value *root = av_doc_root(doc);
        if (!version) {
            av_obj_set(doc, root, "id_counter", av_sint(doc, 0));
        } else {
            av_value *graphs = av_obj_get(root, "graphs");
            av_value *nodes = av_obj_get(av_arr_get(graphs, 0), "nodes");
            test_check(av_arr_size(nodes) >= 2, "Duplicate identity fixture has separate native producers");
            if (av_arr_size(nodes) >= 2) {
                av_obj_set(
                    doc,
                    av_arr_get(nodes, 1),
                    "id",
                    av_uint(doc, av_get_uint(av_obj_get(av_arr_get(nodes, 0), "id")))
                );
            }
        }
        char *invalid = av_json_write(root);
        behavior_asset *before = behavior_asset_clone(source->asset);
        int diagnostics = acount(source->diagnostics);
        test_check(
            !code_source_metadata_restore(source, invalid) && source->asset &&
                acount(source->diagnostics) == diagnostics &&
                behavior_asset_equal(before, source->asset),
            "Invalid highwater or duplicate optional identities reject "
            "atomically without invalidating executable source"
        );
        behavior_asset_free(before);
        free(invalid);
        av_doc_free(doc);
    }
    free(json);
    code_source_free(source);
}

static void variable_identity_namespace()
{
    code_source *source = code_source_parse(
        "behavior IdNamespaces { state float count = 0; "
        "on Tick() { count += event.dt; } }"
    );
    test_check(source->asset != NULL, "Identity namespace fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    behavior_asset *asset = source->asset;
    uint64_t old = asset->variables[0].id;
    uint64_t shared = asset->graph->nodes[0]->output_pins[0].id;
    asset->variables[0].id = shared;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (!n->reference_asset && n->reference_id == old &&
                (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable)) {
                n->reference_id = shared;
            }
            for (int k = 0; k < n->input_count; k++) {
                pin_binding *binding = n->input_pins[k].binding;
                if (binding && binding->variable_id == old) {
                    binding->variable_id = shared;
                }
            }
        }
    }
    char *text = code_source_persist(source);
    code_source *restored = reload_source(source, text);
    if (restored->asset && !behavior_asset_equal(asset, restored->asset)) {
        behavior_asset *pair[] = { asset, restored->asset };
        const char *paths[] = { "build/code-namespace-expected.txt",
                                "build/code-namespace-actual.txt" };
        for (int i = 0; i < 2; i++) {
            av_doc *doc = av_doc_new();
            av_doc_set_root(doc, behavior_asset_to_value(doc, pair[i]));
            char *serialized = av_json_write(av_doc_root(doc));
            FILE *file = fopen(paths[i], "w");
            if (file) {
                fputs(serialized, file);
                fclose(file);
            }
            free(serialized);
            av_doc_free(doc);
        }
    }
    test_check(restored->asset && behavior_asset_equal(asset, restored->asset), "State identities may overlap graph pin identities without changing bindings");
    code_source *updated = code_source_parse_previous(text, restored);
    test_check(updated->asset && behavior_asset_equal(asset, updated->asset), "Live source updates retain separate state and graph identity namespaces");
    behavior_asset *compiled = code_source_compile_behavior(updated);
    behavior_instance *instance = compiled
                                    ? behavior_instance_create(compiled, NULL, NULL, NULL)
                                    : NULL;
    pin_value input = { .type = PIN_F32, .f = .25f };
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(
            instance,
            "Tick",
            (flow_inputs){ .values = &input, .count = 1 },
            &error
        );
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[0].f == .25f,
        "Overlapping metadata identities preserve independent event and state "
        "execution"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(compiled);
    code_source_free(updated);
    code_source_free(restored);
    free(text);
    code_source_free(source);
}

static void generated_scope_identities()
{
    code_source *source = code_source_parse(
        "behavior Helpers { state total: double = 0; "
        "value a = (1 + 2); value b = (3 * 4); "
        "on Tick() { total = a + b; } }"
    );
    test_check(
        source->asset && acount(source->asset->graphs) > 2,
        "Generated scope identity fixture constructs independent expressions"
    );
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    source->asset->graph->name = graph_own_str(source->asset->graph, "Events", 6);
    source->asset->graphs[1]->name = graph_own_str(source->asset->graphs[1], "First value", 11);
    source->asset->graphs[2]->name = graph_own_str(source->asset->graphs[2], "Second value", 12);
    for (int i = 0; i < acount(source->asset->graph->nodes); i++) {
        node *n = source->asset->graph->nodes[i];
        for (int j = 0; j < n->input_count; j++) {
            n->input_pins[j].display_name = NULL;
        }
    }
    char *text = code_source_persist(source);
    code_source *restored = reload_source(source, text);
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Generated expression scopes restore their own identities and retain the root graph label");
    code_source_free(restored);
    free(text);
    code_source_free(source);
}

static void authored_scope_order()
{
    code_source *original = code_source_parse(
        "behavior ScopeOrder { events First { on Tick() {} } "
        "events Second { on Tick() {} } }"
    );
    test_check(original->asset != NULL, "Authored event scope order fixture parses");
    if (!original->asset) {
        code_source_free(original);
        return;
    }
    char *metadata = code_source_metadata_export(original->asset);
    const char
        *reordered = "behavior ScopeOrder { events Second { on Tick() {} } "
                     "events First { on Tick() {} } }\n";
    code_source *changed = code_source_parse(reordered);
    int diagnostics = acount(changed->diagnostics);
    code_source_metadata_restore(changed, metadata);
    test_check(
        changed->asset && acount(changed->diagnostics) == diagnostics,
        "Optional metadata cannot invalidate reordered executable scopes"
    );
    test_check(
        changed->asset && acount(changed->asset->graphs) == 3 &&
            !strcmp(changed->asset->graphs[1]->name, "Second") &&
            !strcmp(changed->asset->graphs[2]->name, "First"),
        "Optional presentation metadata cannot override authored event scope "
        "order"
    );
    code_source_free(changed);
    free(metadata);
    code_source_free(original);
}

static void inline_scope_identities()
{
    code_source *source = code_source_parse(
        "behavior InlineHelpers { on Tick() {\n"
        "set_component(Component: \"GAME.GameVelocity\", Entity: 0, "
        "Value.x: float = (event.dt + 1), "
        "Value.y: float = (event.dt * 2)); } }"
    );
    test_check(source->asset && acount(source->asset->graphs) == 3, "Two inline expressions share one authored statement owner");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *saved = code_source_persist(source);
    code_source *restored = reload_source(source, saved);
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Inline expression scopes retain distinct caller identities through optional metadata");
    code_source_free(restored);
    free(saved);
    code_source_free(source);
}

static void callable_inline_identities()
{
    code_source *source = code_source_parse(
        "behavior CallableInline { state amount: float = 0; "
        "function Update() -> () { "
        "action first = set_variable<amount>(Value: float = (&delta + 1)); "
        "action second = set_variable<amount>(Value: float = (&delta + 1)); "
        "after entry { first(); second(); return(); } } on Tick() { Update(); "
        "} }"
    );
    test_check(source->asset != NULL, "Callable inline helper fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *saved = code_source_persist(source);
    code_source *restored = reload_source(source, saved);
    char *again = restored->asset ? code_source_persist(restored) : NULL;
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Repeated inline formulas in callable actions retain exact independent identities");
    test_check(again && !strcmp(saved, again), "Callable inline helper persistence is idempotent");
    free(again);
    code_source_free(restored);
    free(saved);
    code_source_free(source);
}

static void projected_parameter_identities()
{
    code_source *source = code_source_parse(
        "library Projected { pure function Cross(move: struct<\"CF_V2\"> = "
        "null, "
        "aim: struct<\"CF_V2\"> = null) -> (Result: double = 0) { "
        "value left = multiply(A: double = entry.move.y, B: double = "
        "entry.aim.x); "
        "value right = multiply(A: double = entry.move.x, B: double = "
        "entry.aim.y); "
        "result Result: double = subtract(A: double = left, B: double = "
        "right); } }"
    );
    test_check(source->asset != NULL, "Repeated field names under distinct aggregate parameters parse");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *saved = code_source_persist(source);
    code_source *restored = reload_source(source, saved);
    char *again = restored->asset ? code_source_persist(restored) : NULL;
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Aggregate parameter field identities retain their distinct source parents");
    test_check(again && !strcmp(saved, again), "Repeated aggregate fields persist idempotently");
    free(again);
    code_source_free(restored);
    free(saved);
    code_source_free(source);
}
static void nested_parameter_identities()
{
    code_source *source = code_source_parse(
        "library NestedProjected { pure function AddMoves(first: "
        "struct<\"GameInput\"> = null, "
        "second: struct<\"GameInput\"> = null) -> (Result: float = 0) { "
        "result Result: float = add(A: float = entry.first.move.x, "
        "B: float = entry.second.move.x); } }"
    );
    test_check(source->asset != NULL, "Nested repeated projection fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *saved = code_source_persist(source);
    code_source *restored = reload_source(source, saved);
    char *again = restored->asset ? code_source_persist(restored) : NULL;
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Nested identical field names retain their complete distinct parent ancestry");
    test_check(again && !strcmp(saved, again), "Nested field identity persistence is idempotent");
    free(again);
    code_source_free(restored);
    free(saved);
    code_source_free(source);
}

static void unowned_callable_metadata()
{
    const char
        *text = "behavior Unowned { state amount: float = 0; "
                "function Update() -> () { "
                "action assign = set_variable<amount>(Value: float = (&delta + "
                "1)); "
                "after entry { assign(); return(); } } on Tick() { Update(); } "
                "}";
    code_source *source = code_source_parse(text);
    test_check(source->asset != NULL, "Unowned callable metadata fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *metadata = code_source_metadata_export(source->asset);
    av_doc *doc = av_json_read(metadata);
    av_value *graphs = av_obj_get(av_doc_root(doc), "graphs");
    for (size_t i = 0; i < av_arr_size(graphs); i++) {
        av_value *nodes = av_obj_get(av_arr_get(graphs, i), "nodes");
        for (size_t j = 0; j < av_arr_size(nodes); j++) {
            av_obj_remove_key(av_arr_get(nodes, j), "owner");
        }
    }
    char *json = av_json_write(av_doc_root(doc));
    code_source *restored = code_source_parse(text);
    bool applied = code_source_metadata_restore(restored, json);
    test_check(
        applied && restored->asset &&
            behavior_asset_equal(source->asset, restored->asset),
        "Unique unowned callable metadata retains its full authoring identities"
    );
    code_source_free(restored);
    free(json);
    av_doc_free(doc);
    free(metadata);
    code_source_free(source);
}

static void expression_boundary_identity()
{
    code_source *source = code_source_parse(
        "behavior BoundaryIdentity { state input: double = 4; "
        "state duration: double = 8; state output: float = 0;\n"
        "value ratio: float = (cast<double>(&state.input) / "
        "max(cast<double>(&state.duration), 0.001));\n"
        "on Tick() { output = ratio; } }"
    );
    test_check(source->asset != NULL, "A typed expression boundary owns one identity despite sharing source ranges with its computation");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    node *boundary = NULL;
    for (int i = 0; i < acount(source->asset->graph->nodes); i++) {
        node *n = source->asset->graph->nodes[i];
        if (n->desc == &NODE_BehaviorExpression && n->output_count &&
            n->output_pins[0].type == PIN_F32) {
            boundary = n;
        }
    }
    test_check(boundary != NULL, "The authored float boundary has its own native identity");
    char *saved = code_source_persist(source);
    code_source *restored = reload_source(source, saved);
    test_check(restored->asset && behavior_asset_equal(source->asset, restored->asset), "Typed expression identities and executable connections survive independent metadata reload");
    behavior_asset *asset = code_source_compile_behavior(restored);
    behavior_instance *instance = asset
                                    ? behavior_instance_create(asset, NULL, NULL, NULL)
                                    : NULL;
    flow_error error = { 0 };
    if (instance) {
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
    }
    test_check(
        instance && !behavior_instance_last_failure(instance) &&
            instance->variables[2].f == 0.5f,
        "Restored expression boundary evaluates its original computation and "
        "float conversion"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
    code_source_free(restored);
    free(saved);
    code_source_free(source);
}

static void local_identities()
{
    code_source *source = code_source_parse(
        "behavior LocalIdentity { state count: int = 0; "
        "value number = integer(Value: 2); local named = number.Value; "
        "on Tick() { count = named; } }"
    );
    test_check(source->asset != NULL, "Named local value builds from readable source");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *text = code_source_persist(source);
    code_source *fresh = reload_source(source, text);
    test_check(fresh->asset && behavior_asset_equal(source->asset, fresh->asset), "Independent source reload preserves named local identity and its bindings");
    char *again = code_source_persist(fresh);
    test_check(again && !strcmp(text, again), "Named local source persistence is idempotent");
    if (fresh->asset) {
        char error[256];
        const char *at = strstr(text, "local named");
        char *metadata = NULL;
        char *renamed = code_intelligence_rename_with_metadata(
            fresh,
            (int)(at - text) + 6,
            "renamed",
            &metadata,
            error,
            sizeof error
        );
        code_source *changed = renamed ? code_source_parse(renamed) : NULL;
        bool restored = changed && metadata &&
                        code_source_metadata_restore(changed, metadata);
        if (!renamed) {
            fprintf(stderr, "Local rename: %s\n", error);
        }
        if (changed && !changed->asset) {
            for (int i = 0; i < acount(changed->diagnostics); i++) {
                fprintf(
                    stderr,
                    "Local rename reload: %s\n",
                    changed->diagnostics[i].message
                );
            }
        }
        test_check(
            restored && changed->asset && acount(changed->asset->graph->locals) == 1 &&
                changed->asset->graph->locals[0].id ==
                    fresh->asset->graph->locals[0].id &&
                !strcmp(changed->asset->graph->locals[0].name, "renamed"),
            "Resolved source rename restores named local identity from "
            "separate metadata"
        );
        code_source_free(changed);
        free(renamed);
        free(metadata);
        metadata = NULL;
        at = strstr(text, "count: int");
        renamed = code_intelligence_rename_with_metadata(
            fresh,
            (int)(at - text),
            "total",
            &metadata,
            error,
            sizeof error
        );
        changed = renamed ? code_source_parse(renamed) : NULL;
        restored = changed && metadata &&
                   code_source_metadata_restore(changed, metadata);
        behavior_variable
            *total = changed && changed->asset
                       ? behavior_asset_find_variable_by_name(changed->asset, "total")
                       : NULL;
        test_check(restored && total && total->id == fresh->asset->variables[0].id, "Resolved source state rename restores live-state identity from separate metadata");
        code_source_free(changed);
        free(renamed);
        free(metadata);
    }
    free(again);
    free(text);
    code_source_free(fresh);
    code_source_free(source);
}

static void state_layout_edits()
{
    const char *base = "behavior StateEdit { state count:float=0; on Tick "
                       "{count += 1;} }";
    const char *added = "behavior StateEdit { state count:float=0; state "
                        "bonus:float=2; on Tick {count += 1;} }";
    code_source *original = code_source_parse(base);
    test_check(original->asset != NULL, "State metadata fixture constructs a valid native behavior");
    if (!original->asset) {
        code_source_free(original);
        return;
    }
    uint64_t count_id = original->asset->variables[0].id;
    uint64_t highwater = original->asset->graph->id_counter;
    code_source *addition = code_source_parse_previous(added, original);
    behavior_variable *count = addition->asset
                                 ? behavior_asset_find_variable_by_name(addition->asset, "count")
                                 : NULL;
    behavior_variable *bonus = addition->asset
                                 ? behavior_asset_find_variable_by_name(addition->asset, "bonus")
                                 : NULL;
    test_check(count && count->id == count_id && bonus && bonus->id >= highwater, "Adding state preserves known identities and allocates new state beyond the old generation");
    code_source *removal = code_source_parse_previous(base, addition);
    test_check(
        removal->asset && acount(removal->asset->variables) == 1 &&
            removal->asset->variables[0].id == count_id,
        "Removing state through incremental source preserves unrelated "
        "variable identity"
    );
    char *saved = addition->asset ? code_source_persist(addition) : NULL;
    code_source *fresh = saved ? reload_source(addition, saved) : NULL;
    count = fresh && fresh->asset
              ? behavior_asset_find_variable_by_name(fresh->asset, "count")
              : NULL;
    bonus = fresh && fresh->asset
              ? behavior_asset_find_variable_by_name(fresh->asset, "bonus")
              : NULL;
    test_check(count && count->id == count_id && bonus && bonus->id >= highwater, "Independent source and updated sidecar restore retained and newly allocated state identities");
    char *removed_text = removal->asset ? code_source_persist(removal) : NULL;
    code_source *removed_fresh = removed_text ? reload_source(removal, removed_text)
                                              : NULL;
    test_check(
        removed_fresh && removed_fresh->asset &&
            acount(removed_fresh->asset->variables) == 1 &&
            removed_fresh->asset->variables[0].id == count_id,
        "Updated optional metadata preserves unaffected identity after state "
        "removal"
    );
    behavior_asset *compiled = code_source_compile_behavior(original);
    behavior_instance *instance = compiled
                                    ? behavior_instance_create(compiled, NULL, NULL, NULL)
                                    : NULL;
    test_check(
        instance && addition->asset &&
            behavior_instance_can_update(instance, addition->asset) == INSTANCE_UPDATE_RESTART,
        "Adding persistent state requires restart rather than unsafe live "
        "migration"
    );
    behavior_instance_destroy(instance);
    behavior_asset_free(compiled);
    code_source_free(removed_fresh);
    free(removed_text);
    code_source_free(fresh);
    free(saved);
    code_source_free(removal);
    code_source_free(addition);
    code_source_free(original);
}

static void first_save_authoring_details()
{
    behavior_asset *asset = behavior_asset_new("FirstSave");
    behavior_asset_set_owner_type(asset, "ecs.entity");
    node *value = graph_add_node(asset->graph, registry_node_by_name("Add"));
    graph_set_node_comment(asset->graph, value, "First line\nSecond line", false);
    graph *function = behavior_asset_add_function(asset, "Named function", false);
    function->description = graph_own_str(function, "Authored details", strlen("Authored details"));
    char *first = behavior_asset_save(asset);
    behavior_asset *loaded = first ? reload_asset(asset, first) : NULL;
    char *second = loaded ? behavior_asset_save(loaded) : NULL;
    test_check(loaded && behavior_asset_equal(asset, loaded), "First save preserves comments and named graph details");
    test_check(first && second && !strcmp(first, second), "First save with empty root details is byte stable after reload");
    free(first);
    free(second);
    behavior_asset_free(loaded);
    behavior_asset_free(asset);
}

static void generated_layout_refresh(bool debug)
{
    code_source *source = code_source_parse("behavior LayoutRefresh { state position:double=0; on Tick { position += 1; } }");
    test_check(source->asset != NULL, "Generated layout fixture parses");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    graph *body = NULL;
    for (int i = 0; i < acount(source->asset->graphs); i++) {
        if (source->asset->graphs[i]->math_expression) {
            body = source->asset->graphs[i];
            break;
        }
    }
    test_check(body != NULL, "Generated layout fixture has an expression body");
    if (!body) {
        code_source_free(source);
        return;
    }
    node *entry = graph_node_by_id(body, body->entry_id);
    node *ret = graph_node_by_id(body, body->return_id);
    entry->x = 123;
    entry->y = 456;
    entry->placed = false;
    if (debug) {
        entry->presentation = NODE_PRESENTATION_EXPANDED;
        entry->breakpoint = true;
        entry->breakpoint_enabled = true;
        graph_set_node_comment(body, entry, "Retained generated note", true);
    }
    ret->x = 789;
    ret->y = 321;
    ret->placed = true;
    char *saved = behavior_asset_save(source->asset);
    behavior_asset *loaded = saved ? reload_asset(source->asset, saved) : NULL;
    compile_report report;
    compile_report_init(&report);
    test_check(loaded && behavior_asset_compile(loaded, &report) == 0, "Generated layout fixture compiles after reload");
    char *compiled = loaded ? behavior_asset_save(loaded) : NULL;
    test_check(saved && compiled && !strcmp(saved, compiled), "Compiling reloaded expressions preserves exact authored placement and debug metadata");
    test_check(loaded && behavior_asset_equal(source->asset, loaded), "Expression refresh preserves native authoring state with executable graph unchanged");
    compile_report_free(&report);
    free(compiled);
    free(saved);
    behavior_asset_free(loaded);
    code_source_free(source);
}

static void local_dependency_order()
{
    code_source *source = code_source_parse("behavior LocalDependencies { value number = real(Value: 2.0); value sum = add(A: &local.Later, B: 1.0); local First = sum.Result; local Later = number.Value; }");
    test_check(
        source->asset && acount(source->asset->graph->locals) == 2 &&
            !strcmp(source->asset->graph->locals[0].name, "First") &&
            !strcmp(source->asset->graph->locals[1].name, "Later"),
        "Forward local producer dependencies preserve authored declaration "
        "order"
    );
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    char *saved = code_source_format(source->asset);
    code_source *restored = reload_source(source, saved);
    char *again = restored->asset ? code_source_format(restored->asset) : NULL;
    test_check(saved && again && !strcmp(saved, again), "Forward local producer source and metadata save identically after independent reload");
    free(again);
    free(saved);
    code_source_free(restored);
    code_source_free(source);
}

static void promoted_expression_identity()
{
    behavior_asset *asset = behavior_asset_new("Shared formula");
    node *first = behavior_expression_add(asset, asset->graph, "b + a + c", NULL);
    node *second = first ? behavior_asset_add_function_call_node(
                               asset,
                               behavior_asset_find_graph(asset, first->reference_id)
                           )
                         : NULL;
    test_check(first && second, "A generated formula has two native callers");
    if (!first || !second) {
        behavior_asset_free(asset);
        return;
    }
    uint64_t first_id = first->id, second_id = second->id;
    uint64_t first_pin = first->input_pins[0].id,
             second_pin = second->input_pins[0].id;
    char *metadata = NULL;
    char *saved = code_source_format_with_metadata(asset, &metadata);
    code_source *restored = code_source_parse(saved);
    test_check(code_source_metadata_restore(restored, metadata), "Shared formula optional metadata restores");
    free(metadata);
    node *one = restored->asset ? graph_node_by_id(restored->asset->graph, first_id)
                                : NULL;
    node *two = restored->asset ? graph_node_by_id(restored->asset->graph, second_id)
                                : NULL;
    graph *target = one ? behavior_asset_find_graph(restored->asset, one->reference_id)
                        : NULL;
    test_check(
        one && two && one->desc == &NODE_CallPureFunction &&
            two->desc == &NODE_CallPureFunction &&
            one->input_pins[0].id == first_pin && two->input_pins[0].id == second_pin &&
            one->reference_id == two->reference_id && target && !target->math_expression,
        "Shared formula promotion retains caller and pin identities without "
        "restoring generated formula semantics from metadata"
    );
    code_source_free(restored);
    free(saved);
    behavior_asset_free(asset);
}

static void generated_topology_order()
{
    code_source *source = code_source_parse(
        "behavior Topology { value total: double = (1.0 + 2.0 * 3.0); }"
    );
    test_check(source->asset != NULL, "Generated topology fixture parses typed arithmetic");
    if (!source->asset) {
        code_source_free(source);
        return;
    }
    graph *body = NULL;
    for (int i = 0; i < acount(source->asset->graphs); i++) {
        if (source->asset->graphs[i]->math_expression) {
            body = source->asset->graphs[i];
            break;
        }
    }
    test_check(body != NULL, "Typed arithmetic has a generated expression body");
    if (!body) {
        code_source_free(source);
        return;
    }
    for (int i = 0; i < acount(body->nodes); i++) {
        body->nodes[i]->placed = true;
        body->nodes[i]->x = (float)body->nodes[i]->id * 2;
        body->nodes[i]->y = (float)body->nodes[i]->id * 3;
    }
    for (int i = 0; i < acount(body->nodes); i++) {
        if (!strcmp(body->nodes[i]->desc->name, "Real")) {
            node *literal = body->nodes[i];
            memmove(
                &body->nodes[i],
                &body->nodes[i + 1],
                (size_t)(acount(body->nodes) - i - 1) * sizeof(node *)
            );
            body->nodes[acount(body->nodes) - 1] = literal;
            break;
        }
    }
    char *saved = code_source_format(source->asset);
    code_source *restored = reload_source(source, saved);
    char *again = restored->asset ? code_source_format(restored->asset) : NULL;
    bool retained = restored->asset && again && !strcmp(saved, again);
    graph *copy = restored->asset
                    ? behavior_asset_find_graph(restored->asset, body->graph_id)
                    : NULL;
    for (int i = 0; retained && i < acount(body->nodes); i++) {
        const node *original = body->nodes[i];
        const node *n = copy ? graph_node_by_id(copy, original->id) : NULL;
        retained = n && n->placed && n->x == original->x && n->y == original->y;
    }
    test_check(retained, "Generated literal allocation order preserves exact saved source, node identities and placement on reopen");
    free(again);
    free(saved);
    code_source_free(restored);
    code_source_free(source);
}

static void authored_complex_roundtrip()
{
    const char *path = "content/behaviors/ArenaDropBehavior.code";
    char *text = text_file_read(path);
    code_source *source = text ? code_source_parse(text) : NULL;
    test_check(source && source->asset, "The authored multi-function ArenaDrop behavior parses");
    if (!source || !source->asset) {
        code_source_free(source);
        free(text);
        return;
    }
    code_source_metadata_load(source, path);
    char *saved = code_source_persist(source);
    char *metadata = code_source_metadata_export_source(source);
    code_source *fresh = code_source_parse(saved);
    bool restored = fresh->asset && code_source_metadata_restore(fresh, metadata);
    bool equal = restored && behavior_asset_equal(source->asset, fresh->asset);
    if (!equal) {
        fprintf(
            stderr,
            "Authored complex roundtrip: restored=%d exactsource=%d\n",
            restored,
            !strcmp(text, saved)
        );
    }
    test_check(equal && !strcmp(text, saved), "Authored multi-function source preserves the exact native asset through optional metadata reopen");
    code_source_free(fresh);
    code_source_free(source);
    free(metadata);
    free(saved);
    free(text);
}

void run_code_source_metadata_checks()
{
    authored_complex_roundtrip();
    annotation_free_sidecar();
    malformed_optional_metadata();
    local_dependency_order();
    promoted_expression_identity();
    generated_topology_order();
    generated_layout_refresh(false);
    generated_layout_refresh(true);
    first_save_authoring_details();
    variable_identity_namespace();
    generated_scope_identities();
    inline_scope_identities();
    callable_inline_identities();
    projected_parameter_identities();
    nested_parameter_identities();
    unowned_callable_metadata();
    expression_boundary_identity();
    authored_scope_order();
    local_identities();
    state_layout_edits();
    const char *
        text = "behavior Persist { state count:float=0; on Tick(GameProjectile "
               "projectile) {count += projectile.age + event.dt;} }";
    code_source *original = code_source_parse(text);
    test_check(original->asset != NULL, "Persistent authoring metadata starts from valid executable source");
    node *n = original->asset
                ? authored_expression(original, "count += projectile.age + event.dt")
                : NULL;
    test_check(n != NULL, "Parser facts identify the authored statement without annotations");
    if (!n) {
        code_source_free(original);
        return;
    }
    graph *g = original->asset->graph;
    n->placed = true;
    n->x = 123;
    n->y = -456;
    n->breakpoint = true;
    n->breakpoint_enabled = true;
    n->comment = graph_own_str(g, "Comment with */ and /* delimiters", strlen("Comment with */ and /* delimiters"));
    graph_add_comment_box(g, "Authored region", 1, 2, 300, 200);
    if (acount(g->edges)) {
        g->edges[0].reroute_count = 1;
        g->edges[0].reroutes[0].x = 12;
        g->edges[0].reroutes[0].y = -20;
    }
    if (n->input_count) {
        n->input_pins[0].flags |= PIN_FLAG_WATCH;
    }
    char *metadata = code_source_metadata_export_source(original);
    code_source *restored = code_source_parse(text);
    bool applied = code_source_metadata_restore(restored, metadata);
    test_check(applied && behavior_asset_equal(original->asset, restored->asset), "Separate optional metadata restores graph identities, layout, comments, breakpoints and watches");
    test_check(!strstr(metadata, "\"disabled\""), "Authoring metadata cannot disable executable statements");
    n->disabled = true;
    char *disabled_metadata = code_source_metadata_export_source(original);
    test_check(!strcmp(metadata, disabled_metadata), "Execution enablement cannot change optional presentation metadata");
    free(disabled_metadata);
    free(metadata);
    code_source_free(restored);
    code_source_free(original);
    const char
        *before = "behavior Reorder { state first:float=0; state second:int=1; "
                  "on Tick(GameProjectile p) {first += p.age + event.dt; p.age "
                  "+= 2;} "
                  "on ContactBegin(GameProjectile p) {p.age += 3;} }";
    const char
        *after = "behavior Reorder { state second:int=1; state first:float=0; "
                 "on ContactBegin(GameProjectile p) {p.age += 3;} "
                 "on Tick(GameProjectile p) {p.age += 2; first += p.age + "
                 "event.dt;} }";
    original = code_source_parse(before);
    test_check(original->asset != NULL, "Reordering fixture constructs native handlers and state");
    n = original->asset ? authored_expression(original, "first += p.age + event.dt")
                        : NULL;
    if (!n) {
        code_source_free(original);
        return;
    }
    n->placed = true;
    n->x = 45;
    n->y = 67;
    uint64_t node_id = n->id, expression_graph = n->reference_id;
    uint64_t input_pin = n->input_pins[0].id;
    uint64_t first_id = original->asset->variables[0].id;
    uint64_t second_id = original->asset->variables[1].id;
    code_source *reordered = code_source_parse_previous(after, original);
    node *moved = reordered->asset
                    ? authored_expression(reordered, "first += p.age + event.dt")
                    : NULL;
    test_check(
        moved && moved->id == node_id && moved->reference_id == expression_graph &&
            moved->input_pins[0].id == input_pin && moved->placed &&
            moved->x == 45 && moved->y == 67,
        "Incremental source matching preserves moved statement identity before "
        "sidecar export"
    );
    behavior_variable
        *first = reordered->asset
                   ? behavior_asset_find_variable_by_name(reordered->asset, "first")
                   : NULL;
    behavior_variable
        *second = reordered->asset
                    ? behavior_asset_find_variable_by_name(reordered->asset, "second")
                    : NULL;
    test_check(first && second && first->id == first_id && second->id == second_id, "Incremental source matching preserves reordered state before sidecar export");
    char *saved = reordered->asset ? code_source_persist(reordered) : NULL;
    restored = saved ? reload_source(reordered, saved) : NULL;
    node *retained = restored && restored->asset
                       ? graph_node_by_id(restored->asset->graph, node_id)
                       : NULL;
    test_check(
        retained && retained->reference_id == expression_graph &&
            retained->input_pins[0].id == input_pin && retained->placed &&
            retained->x == 45 && retained->y == 67,
        "Known source edits and updated sidecar preserve moved statement, "
        "graph, pin and layout identities"
    );
    test_check(
        restored && restored->asset &&
            behavior_asset_find_variable_by_name(restored->asset, "first")->id == first_id &&
            behavior_asset_find_variable_by_name(restored->asset, "second")->id == second_id,
        "Independent reload of reordered source and updated sidecar preserves "
        "state identity"
    );
    free(saved);
    code_source_free(restored);
    code_source_free(reordered);
    code_source_free(original);
}
