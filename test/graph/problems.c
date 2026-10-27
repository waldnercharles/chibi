#include "graph/document/problems.h"

#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/graph_signature.h"
#include "support/array.h"
#include "test/graph/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <string.h>

/* A shader library function returning time, with a disabled node, called
   from another shader library's function that has one too. Disabled nodes
   are warnings. */
static behavior_asset *library, *caller;
static graph *value, *use;
static node *muted, *idle;
static catalog_problem *problems;

static behavior_asset *shader_library(const char *name, const char *function, graph **out, node **disabled)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *g = behavior_asset_add_function(asset, function, 1);
    pin_spec result = { .name = "Value",
                        .kind = PIN_DATA,
                        .type = PIN_F32,
                        .type_name = "number" };
    int declared = graph_signature_add(asset, g, true, &result);
    assert(declared == 0);
    (void)declared;
    *disabled = graph_add_node(g, graph_palette_node("Shader Time"));
    graph_set_node_disabled(g, *disabled, true);
    *out = g;
    return asset;
}

/* Wires `source` into the function's result. */
static void returns(graph *g, node *source)
{
    int wired = graph_connect(g, source, 0, graph_node_by_id(g, g->return_id), 0);
    assert(wired == 0);
    (void)wired;
}

static void setup()
{
    library = shader_library("CheckedMath", "Value", &value, &muted);
    returns(value, graph_add_node(value, graph_palette_node("Shader Time")));
    caller = shader_library("CheckedCaller", "Use", &use, &idle);
    returns(use, behavior_asset_add_function_call_node_to_graph(caller, use, value));
}

static void cleanup()
{
    afree(problems);
    problems = NULL;
    behavior_asset_free(caller);
    behavior_asset_free(library);
    caller = library = NULL;
}

static bool warning_at(
    const catalog_problem *p,
    const behavior_asset *asset,
    const graph *g,
    const node *n
)
{
    return p->asset == asset && p->graph_id == g->graph_id &&
           p->node_id == n->id && p->severity == CMPL_WARNING;
}

/* A project's problems: every asset validated linked with the others, each
   problem once, where it was authored, by asset in catalog order. */
TEST_CASE(library_problems_show_once_under_the_library)
{
    const behavior_asset *catalog[] = { caller, library };
    problems = behavior_catalog_problems(catalog, 2);
    REQUIRE(acount(problems) == 2);
    REQUIRE(warning_at(&problems[0], caller, use, idle));
    REQUIRE(warning_at(&problems[1], library, value, muted));
    return true;
}

TEST_CASE(calls_into_missing_libraries_are_one_error)
{
    const behavior_asset *alone[] = { caller };
    problems = behavior_catalog_problems(alone, 1);
    REQUIRE(acount(problems) == 1 && problems[0].asset == caller);
    REQUIRE(problems[0].severity == CMPL_ERROR && problems[0].node_id == 0);
    REQUIRE(strstr(problems[0].message, "CheckedMath"));
    return true;
}

TEST_CASE(validation_compiles_with_the_libraries_called)
{
    const behavior_asset *catalog[] = { caller, library };
    compile_report report;
    compile_report_init(&report);
    behavior_asset *linked = behavior_asset_validate(caller, catalog, 2, &report);
    bool compiled = linked && report.error_count == 0 && report.warning_count == 2;
    behavior_asset_free(linked);
    compile_report_free(&report);
    REQUIRE(compiled);
    return true;
}

void test_graph_problems()
{
    graph_palette_open();
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(library_problems_show_once_under_the_library);
    RUN_TEST_CASE(calls_into_missing_libraries_are_one_error);
    RUN_TEST_CASE(validation_compiles_with_the_libraries_called);
    pu_clear_setup();
    graph_palette_close();
}
