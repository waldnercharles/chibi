#include "assets/field_parameters.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/material_field.h"
#include "graph/document/library.h"
#include "runtime/material_field.h"
#include "test/support/graph_packs.h"
#include "third_party/pico_headers/pico_unit.h"

/* Native field artifacts link pure scalar library graphs before publication.
 * This does not turn a .chibi presentation into executable graph state. */
static behavior_asset *library, *field;
static material_fields *fields;

static void cleanup()
{
    material_fields_free(fields);
    fields = NULL;
    behavior_asset_free(field);
    field = NULL;
    behavior_asset_free(library);
    library = NULL;
    test_graph_packs_clear();
}

static bool publish(char *error, size_t size)
{
    const behavior_asset *catalog[] = { library };
    behavior_asset *linked = behavior_asset_link_libraries(field, catalog, 1, error, size);
    if (!linked) {
        return false;
    }
    bool published = material_field_compile(fields, linked, error, size);
    behavior_asset_free(linked);
    return published;
}

static bool sample(float *value, char *error, size_t size)
{
    return material_field_bake_text(
        fields,
        "LibraryField",
        NULL,
        1,
        (field_grid){ 1, 1, 1 },
        value,
        NULL,
        error,
        size
    );
}

TEST_CASE(field_library_republication_retains_the_last_valid_generation)
{
    fields = material_fields_new();
    REQUIRE(test_graph_packs_set());
    library = behavior_asset_new("ScalarLibrary");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *function = behavior_asset_add_function(library, "Sum", true);
    REQUIRE(function);
    node *result = graph_node_by_id(function, function->return_id);
    REQUIRE(
        graph_add_pin(function, result, false, &(pin_spec){ .name = "Value", .type = PIN_F32 }) == 0
    );
    node *sum = graph_add_node(function, registry_node_by_name("Field Add"));
    REQUIRE(sum);
    REQUIRE(
        !graph_set_pin_default(function, sum, false, 0, &(pin_value){ .type = PIN_F32, .f = 3 })
    );
    REQUIRE(
        !graph_set_pin_default(function, sum, false, 1, &(pin_value){ .type = PIN_F32, .f = 3 })
    );
    REQUIRE(!graph_connect_data(function, sum, 0, result, 0));

    field = behavior_asset_new("LibraryField");
    field->domain = BEHAVIOR_DOMAIN_FIELD;
    node *call = behavior_asset_add_function_call_node(field, function);
    node *output = graph_add_node(field->graph, registry_node_by_name("Field Output"));
    REQUIRE(call && output);
    REQUIRE(!graph_connect_data(field->graph, call, 0, output, 0));
    char error[256] = "";
    float value;
    REQUIRE(publish(error, sizeof error));
    REQUIRE(material_field_revision(fields, "LibraryField") == 1);
    REQUIRE(sample(&value, error, sizeof error) && value == 6);

    /* Relinking publishes the edited library's result as a new generation. */
    REQUIRE(
        !graph_set_pin_default(function, sum, false, 0, &(pin_value){ .type = PIN_F32, .f = 6 })
    );
    REQUIRE(publish(error, sizeof error));
    REQUIRE(material_field_revision(fields, "LibraryField") == 2);
    REQUIRE(sample(&value, error, sizeof error) && value == 9);

    /* An operation outside the pure field language cannot replace that result. */
    REQUIRE(graph_add_node(function, registry_node_by_name("Print String")));
    REQUIRE(!publish(error, sizeof error));
    REQUIRE(error[0]);
    REQUIRE(material_field_revision(fields, "LibraryField") == 2);
    REQUIRE(sample(&value, error, sizeof error) && value == 9);
    return true;
}

void test_field_library()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(field_library_republication_retains_the_last_valid_generation);
    pu_clear_setup();
}
