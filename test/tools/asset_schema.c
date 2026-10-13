#include "assets/asset_schema.h"

#include "assets/asset_source.h"
#include "assets/new_asset.h"
#include "authoring/asset_description.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/field_nodes.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stdlib.h>
#include <string.h>

static const registry_pack **previous_packs;
static bool project_types;

static void set_project_types(const type_catalog_pack *pack)
{
    type_catalog_set_project(pack);
    project_types = pack != NULL;
}

static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK, &MODEL_PACK, &FIELD_NODE_PACK };
    registry_set_packs(packs, (int)(sizeof packs / sizeof *packs));
}

static void cleanup()
{
    if (project_types) {
        set_project_types(NULL);
    }
    registry_set_packs(previous_packs, acount(previous_packs));
    afree(previous_packs);
}

static const asset_source_diagnostic *find_problem(
    const asset_source *source,
    const char *code,
    const char *path
)
{
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const asset_source_diagnostic *item = &source->diagnostics[i];
        if (!strcmp(item->code, code) && !strcmp(item->path, path)) {
            return item;
        }
    }
    return NULL;
}
static bool has_completion(const asset_source_completions *out, const char *name)
{
    for (int i = 0; i < acount(out->items); i++) {
        if (!strcmp(out->items[i].label, name)) {
            return true;
        }
    }
    return false;
}

TEST_CASE(schema_reports_property_paths_and_source_ranges)
{
    const char *text = "version 1\nname \"Main\"\npresentation "
                       "\"perspective\"\n(array)entities {\n - name=\"Player\" "
                       "prototype=\"Player\" {\n  (array)behaviors {\n   - "
                       "42\n  }\n  typo #true\n }\n}\n";
    asset_source *source = asset_source_parse_path(text, "content/scenes/Main.kdl");
    REQUIRE(source->document);
    const asset_source_diagnostic *choice = find_problem(source, "INVALID_CHOICE", "presentation");
    REQUIRE(choice);
    REQUIRE(!strcmp(choice->expected, "2d, 3d"));
    REQUIRE(choice->line == 3);
    REQUIRE(choice->end > choice->start);
    REQUIRE(choice->start <= (int)(strstr(text, "perspective") - text));
    const asset_source_diagnostic *number = find_problem(source, "INVALID_TYPE", "entities[0].behaviors[0]");
    REQUIRE(number);
    REQUIRE(!strcmp(number->expected, "string"));
    REQUIRE(number->line == 7);
    REQUIRE(find_problem(source, "UNKNOWN_FIELD", "entities[0].typo"));
    asset_source_free(source);
    return true;
}

TEST_CASE(schema_missing_fields_anchor_to_parent_and_partial_schemas_stay_open)
{
    asset_source *source = asset_source_parse_path("version 1\nname \"Main\"\n", "scenes/Main.kdl");
    REQUIRE(find_problem(source, "MISSING_FIELD", "entities"));
    asset_source_free(source);
    const asset_schema *schema = asset_schema_find("material");
    REQUIRE(schema && !schema->complete);
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_obj_add_int(doc, root, "version", 3);
    av_obj_add_str(doc, root, "name", "Surface");
    av_obj_add_int(doc, root, "id_counter", 1);
    av_obj_add_arr(doc, root, "graphs");
    av_obj_add_bool(doc, root, "domain_specific_metadata", true);
    asset_schema_diagnostic *items = asset_schema_validate(schema, root, "");
    REQUIRE(acount(items) == 0);
    afree(items);
    av_doc_free(doc);
    return true;
}

TEST_CASE(schema_completion_uses_asset_and_nested_context)
{
    const char *draft = "version 1\nname \"Main\"\n";
    asset_source *source = asset_source_parse(draft);
    asset_source_completions out;
    asset_source_complete(source, (int)strlen(draft), "content/scenes/Main.kdl", NULL, 0, &out);
    REQUIRE(has_completion(&out, "entities"));
    REQUIRE(has_completion(&out, "presentation"));
    REQUIRE(!has_completion(&out, "includes"));
    asset_source_completions_free(&out);
    asset_source_free(source);
    draft = "version 1\nname \"Main\"\nphysics {\n ";
    source = asset_source_parse(draft);
    asset_source_complete(source, (int)strlen(draft), "scenes/Main.kdl", NULL, 0, &out);
    REQUIRE(has_completion(&out, "substeps"));
    REQUIRE(!has_completion(&out, "entities"));
    asset_source_completions_free(&out);
    asset_source_free(source);
    draft = "version 1\nname \"Main\"\npresentation \"";
    source = asset_source_parse(draft);
    asset_source_complete(source, (int)strlen(draft), "scenes/Main.kdl", NULL, 0, &out);
    REQUIRE(has_completion(&out, "2d"));
    REQUIRE(has_completion(&out, "3d"));
    asset_source_completions_free(&out);
    asset_source_free(source);
    return true;
}

TEST_CASE(schema_reflected_overrides_remain_sparse_and_report_exact_fields)
{
    static const type_field fields[] = {
        { .name = "enabled", .type = PIN_BOOL, .size = sizeof(bool), .type_name = "bool" },
    };
    static const type_desc type = { .name = "SchemaPayload",
                                    .fields = fields,
                                    .field_count = 1 };
    static const type_desc *types[] = { &type };
    type_catalog_pack pack = { .types = types, .type_count = 1 };
    set_project_types(&pack);
    asset_source *source = asset_source_parse_path(
        "name \"Test\"\ncomponents {\n SchemaPayload enabled=#null\n}\n",
        "prototypes/Test.kdl"
    );
    REQUIRE(source->document);
    REQUIRE(acount(source->diagnostics) == 0);
    asset_source_free(source);
    source = asset_source_parse_path("name \"Test\"\ncomponents {\n SchemaPayload enabled=42 typo=#true\n}\n", "prototypes/Test.kdl");
    REQUIRE(find_problem(source, "INVALID_TYPE", "components.SchemaPayload.enabled"));
    REQUIRE(find_problem(source, "UNKNOWN_FIELD", "components.SchemaPayload.typo"));
    asset_source_free(source);
    set_project_types(NULL);
    return true;
}

TEST_CASE(schema_hover_and_generic_fragments_preserve_their_contracts)
{
    const char *text = "version 1\nname \"Main\"\npresentation "
                       "\"3d\"\n(array)entities\n";
    asset_source *source = asset_source_parse_path(text, "scenes/Main.kdl");
    char *hover = asset_source_hover(source, (int)(strstr(text, "presentation") - text));
    REQUIRE(hover && strstr(hover, "Omission uses 2d"));
    free(hover);
    asset_source_free(source);
    source = asset_source_parse("custom_metadata 42\n");
    REQUIRE(source->document && acount(source->diagnostics) == 0);
    asset_source_free(source);
    source = asset_source_parse_path("version 23\n", "project.kdl");
    REQUIRE(source->document && acount(source->diagnostics) == 0);
    asset_source_free(source);
    REQUIRE(asset_schema_find("behavior")->executable);
    REQUIRE(asset_schema_validate(asset_schema_find("behavior"), NULL, "") == NULL);
    return true;
}

TEST_CASE(schema_descriptions_cover_creation_kinds_and_recursive_entities)
{
    REQUIRE(asset_schema_count() >= NEW_ASSET_KIND_COUNT);
    REQUIRE(!asset_schema_for_path("some-scenes/File.kdl"));
    REQUIRE(
        asset_schema_for_path("/project/content/scenes/Main.kdl") ==
        asset_schema_find("scene")
    );
    REQUIRE(asset_schema_for_path("/project/project.kdl") == asset_schema_find("project"));
    for (int i = 0; i < NEW_ASSET_KIND_COUNT; i++) {
        const asset_schema *schema = asset_schema_at(i);
        REQUIRE(!strcmp(schema->folder, new_asset_folder((new_asset_kind)i)));
        av_doc *doc = av_doc_new();
        av_value *description = asset_schema_describe(doc, schema, true);
        REQUIRE(av_is_str(av_obj_get(description, "example")));
        const char *text = av_get_str(av_obj_get(description, "example"));
        REQUIRE(text && *text);
        if (!schema->executable) {
            char error[256];
            av_doc *parsed = av_doc_read(text, error, sizeof error);
            REQUIRE(parsed);
            asset_schema_diagnostic
                *items = asset_schema_validate(schema, av_doc_root(parsed), text);
            REQUIRE(acount(items) == 0);
            afree(items);
            av_doc_free(parsed);
        }
        av_doc_free(doc);
    }
    return true;
}

TEST_CASE(reflected_descriptions_preserve_units_ranges_colors_and_enum_choices)
{
    static const enum_value_desc choices[] = { { 0, "Off", "Disabled" },
                                               { 1, "On", "Enabled" } };
    static const enum_desc enumeration = { "SchemaTestMode", choices, 2 };
    static const enum_desc *enums[] = { &enumeration };
    type_catalog_pack pack = { .enums = enums, .enum_count = 1 };
    set_project_types(&pack);
    const type_field fields[] = {
        { .name = "speed",
          .type = PIN_F32,
          .type_name = "float",
          .unit = "u/s",
          .min = 0,
          .max = 100,
          .description = "Travel speed." },
        { .name = "tint", .type = PIN_U32, .color = true },
        { .name = "mode", .type = PIN_ENUM, .type_name = "SchemaTestMode" },
    };
    const type_desc type = { .name = "SchemaTest", .fields = fields, .field_count = 3 };
    av_doc *doc = av_doc_new();
    av_value *description = authoring_type_describe(doc, &type);
    av_value *output = av_obj_get(description, "fields");
    REQUIRE(av_arr_size(output) == 3);
    av_value *speed = av_arr_get(output, 0);
    REQUIRE(!strcmp(av_get_str(av_obj_get(speed, "unit")), "u/s"));
    av_value *range = av_obj_get(speed, "editing_range");
    REQUIRE(av_get_num(av_obj_get(range, "max")) == 100);
    REQUIRE(!av_get_bool(av_obj_get(range, "clamp")));
    REQUIRE(av_get_bool(av_obj_get(av_arr_get(output, 1), "color")));
    av_value *values = av_obj_get(av_arr_get(output, 2), "choices");
    REQUIRE(av_arr_size(values) == 2);
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(values, 1), "label")), "Enabled"));
    av_doc_free(doc);
    set_project_types(NULL);
    return true;
}

TEST_SUITE(asset_schema_suite)
{
    RUN_TEST_CASE(schema_reports_property_paths_and_source_ranges);
    RUN_TEST_CASE(schema_missing_fields_anchor_to_parent_and_partial_schemas_stay_open);
    RUN_TEST_CASE(schema_completion_uses_asset_and_nested_context);
    RUN_TEST_CASE(schema_reflected_overrides_remain_sparse_and_report_exact_fields);
    RUN_TEST_CASE(schema_hover_and_generic_fragments_preserve_their_contracts);
    RUN_TEST_CASE(schema_descriptions_cover_creation_kinds_and_recursive_entities);
    RUN_TEST_CASE(reflected_descriptions_preserve_units_ranges_colors_and_enum_choices);
}
void test_asset_schema()
{
    pu_setup(setup, cleanup);
    RUN_TEST_SUITE(asset_schema_suite);
    pu_clear_setup();
}
