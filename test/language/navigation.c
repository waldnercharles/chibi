#include "assets/input_map.h"
#include "authoring/source_catalog.h"
#include "authoring/source_prototype_flow.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static authoring_document documents[3];
static script_analysis *declarations[3];
static script_catalog_capability capabilities[10];
static script_catalog_parameter parameters[10][4], results[10][4];
static char qualified[10][128];
static authoring_navigation *navigation;
static int document_count;
static char error[192];
static char *edited;
static source_catalog *input_catalog;
static const script_catalog_field fields[] = {
    { .id = 20, .name = "x", .type = { .storage = PIN_I32 }, .writable = true },
    { .id = 21, .name = "y", .type = { .storage = PIN_I32 }, .offset = sizeof(int), .writable = true },
};
static const script_catalog_type types[] = {
    { .id = 10,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 10 },
      .fields = fields,
      .field_count = 2,
      .size = 2 * sizeof(int) },
};
static script_catalog catalog;
static const script_catalog_global globals[] = {
    { .id = 300, .name = "hostValue", .type = { .storage = PIN_I32 } },
};

static void cleanup()
{
    authoring_navigation_free(navigation);
    free(edited);
    for (int i = 0; i < document_count; i++) {
        script_analysis_free((script_analysis *)documents[i].analysis);
        script_analysis_free(declarations[i]);
        script_syntax_free((script_syntax *)documents[i].syntax);
    }
    memset(documents, 0, sizeof documents);
    memset(declarations, 0, sizeof declarations);
    source_catalog_free(input_catalog);
    input_catalog = NULL;
    navigation = NULL;
    edited = NULL;
    document_count = 0;
}

/* The test catalog uses actual discovered declaration IDs and signatures. No
 * executable backend, graph compiler or runtime is constructed. */
static void base_catalog()
{
    cleanup();
    memset(capabilities, 0, sizeof capabilities);
    capabilities[0] = (script_catalog_capability){ .id = 100,
                                                   .name = "Tick",
                                                   .kind = SCRIPT_CAPABILITY_EVENT };
    catalog = (script_catalog){ .types = types,
                                .type_count = 1,
                                .capabilities = capabilities,
                                .capability_count = 1,
                                .tick_event = 100,
                                .globals = globals,
                                .global_count = 1 };
}

/* Adds the sources' library functions to the catalog, then analyzes them. */
static bool load(const char *const *source, int count)
{
    document_count = count;
    static const char *paths[] = { "lib.chibi", "app.chibi", "other.chibi" };
    for (int d = 0; d < count; d++) {
        documents[d].path = paths[d];
        documents[d].syntax = script_parse(source[d]);
        declarations[d] = script_declarations(documents[d].syntax, &catalog);
        const script_analysis *a = declarations[d];
        for (int i = 0; i < a->function_count; i++) {
            const script_function *f = &a->functions[i];
            if (f->kind != SCRIPT_FUNCTION_AUTHORED || f->symbol < 0) {
                continue;
            }
            int at = catalog.capability_count++;
            if (at >= 10 || f->parameter_count > 4 || f->result_count > 4) {
                return false;
            }
            const script_symbol *s = &a->symbols[f->symbol];
            snprintf(qualified[at], sizeof qualified[at], "%s.%s", a->name, s->name);
            for (int j = 0; j < f->parameter_count; j++) {
                const script_symbol *p = &a->symbols[f->parameters[j]];
                parameters[at][j] = (script_catalog_parameter){ .name = p->name,
                                                                .type = p->type };
                /* Constant defaults travel with the signature, as in a project catalog. */
                if (p->initializer >= 0 &&
                    a->expressions[p->initializer].kind == SCRIPT_EXPR_CONSTANT) {
                    parameters[at][j].has_default = true;
                    parameters[at][j]
                        .default_value = a->expressions[p->initializer].constant;
                }
            }
            for (int j = 0; j < f->result_count; j++) {
                const script_symbol *r = &a->symbols[f->results[j]];
                results[at][j] = (script_catalog_parameter){ .name = r->name,
                                                             .type = r->type };
            }
            capabilities[at] = (script_catalog_capability){
                .id = s->id,
                .name = s->name,
                .qualified_name = qualified[at],
                .parameters = parameters[at],
                .parameter_count = f->parameter_count,
                .results = results[at],
                .result_count = f->result_count,
                .pure = f->pure,
                .completion_outcome = -1
            };
        }
    }
    bool valid = true;
    for (int d = 0; d < count; d++) {
        documents[d].analysis = script_analyze(documents[d].syntax, &catalog);
        const script_analysis *a = documents[d].analysis;
        valid &= a->valid && !documents[d].syntax->diagnostic_count;
        for (int i = 0; i < a->diagnostic_count; i++) {
            fprintf(stderr, "navigation: %s\n", a->diagnostics[i].message);
        }
    }
    navigation = authoring_navigation_new(documents, count, &catalog);
    return valid;
}

static bool project(const char *const *source, int count)
{
    base_catalog();
    return load(source, count);
}

static const authoring_reference *at(int document, const char *token)
{
    const char *text = documents[document].syntax->text;
    const char *found = strstr(text, token);
    return found ? authoring_navigation_at(navigation, document, (int)(found - text))
                 : NULL;
}

static int references(authoring_target target)
{
    int count = 0;
    for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
        count += authoring_target_equal(
            authoring_navigation_reference_at(navigation, i)->target,
            target
        );
    }
    return count;
}

/* Completion offers the payload fields a handler lists, and nothing for the
 * fields it leaves out. */
/* A character literal highlights as a string, and char names its type. */
static void diagnose_syntax(TSNode node, int depth, int *remaining)
{
    if (!*remaining || depth > 6) {
        return;
    }
    (*remaining)--;
    fprintf(
        stderr,
        "%*s%s %u..%u\n",
        depth * 2,
        "",
        ts_node_type(node),
        ts_node_start_byte(node),
        ts_node_end_byte(node)
    );
    for (uint32_t i = 0; i < ts_node_named_child_count(node) && *remaining; i++) {
        diagnose_syntax(ts_node_named_child(node, i), depth + 1, remaining);
    }
}

static bool external_project()
{
    cleanup();
    static script_catalog_parameter spawn_parameter = {
        .name = "prototype",
        .type = { .storage = PIN_STR },
        .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
    };
    static script_catalog_parameter component_parameter = {
        .name = "component",
        .type = { .storage = PIN_STR },
        .reference_kind = SCRIPT_REFERENCE_COMPONENT
    };
    static const script_catalog_type component_type = { .id = 10,
                                                        .name = "Position",
                                                        .type = { .storage = PIN_STRUCT,
                                                                  .nominal = 10 },
                                                        .fields = fields,
                                                        .field_count = 2,
                                                        .size = 2 * sizeof(int),
                                                        .component = true,
                                                        .component_id = 900 };
    static const script_catalog_capability caps[] = {
        { .id = 400,
          .name = "Spark",
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .completion_outcome = -1 },
        { .id = 401, .name = "Pulse", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
        { .id = 402,
          .name = "Spawn",
          .parameters = &spawn_parameter,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 403,
          .name = "Has",
          .parameters = &component_parameter,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 404, .name = "Taken", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    };
    static const source_catalog_definition definitions[] = {
        { .id = 400,
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .name = "Spark",
          .path = "prototypes/Spark.kdl",
          .text = "name \"Spark\"\n",
          .range = { 5, 12, 1, 6, 0 } },
        { .id = 401,
          .kind = SCRIPT_CAPABILITY_EVENT,
          .name = "Pulse",
          .path = "events/Pulse.kdl",
          .text = "name \"Pulse\"\n",
          .range = { 5, 12, 1, 6, 0 } },
    };
    static const source_catalog_reference references[] = {
        { .id = 400,
          .kind = SOURCE_REFERENCE_PROTOTYPE,
          .name = "Spark",
          .path = "scenes/Start.kdl",
          .text = "prototype \"Spark\"\nnote \"Spark\"\n",
          .range = { 10, 17, 1, 11, 0 } },
    };
    catalog = (script_catalog){ .types = &component_type,
                                .type_count = 1,
                                .capabilities = caps,
                                .capability_count = 5 };
    spawn_parameter.type = script_reference_type(SCRIPT_REFERENCE_PROTOTYPE);
    component_parameter.type = script_reference_type(SCRIPT_REFERENCE_COMPONENT);
    document_count = 1;
    documents[0] = (authoring_document){
        .path = "behaviors/Consumer.chibi",
        .syntax = script_parse("Consumer :: behavior { p:Position={x:1,y:2}; label:=\"Spark\"; event pulse=Pulse; on pulse :: () { Spawn(Spark); Has(Position); } }")
    };
    documents[0].analysis = script_analyze_document(
        documents[0].syntax,
        &catalog,
        documents[0].path
    );
    const script_analysis *analysis = documents[0].analysis;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "external navigation: %s\n", analysis->diagnostics[i].message);
    }
    navigation = authoring_navigation_new_project(documents, 1, &catalog, definitions, 2, references, 1);
    return analysis->valid && !documents[0].syntax->diagnostic_count;
}
/* Spawn takes a prototype by name; Spark and Ember are prototypes. */
static bool spawn_project(const char *const *source, int count)
{
    static script_catalog_parameter spawn_parameter = {
        .name = "prototype",
        .type = { .storage = PIN_NAME },
        .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
    };
    static const source_catalog_definition definitions[] = {
        { .id = 400,
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .name = "Spark",
          .path = "prototypes/Spark.kdl",
          .text = "name \"Spark\"\n",
          .range = { 5, 12, 1, 6, 0 } },
    };
    base_catalog();
    spawn_parameter.type = script_reference_type(SCRIPT_REFERENCE_PROTOTYPE);
    capabilities[1] = (script_catalog_capability){ .id = 400,
                                                   .name = "Spark",
                                                   .kind = SCRIPT_CAPABILITY_PROTOTYPE,
                                                   .completion_outcome = -1 };
    capabilities[2] = (script_catalog_capability){ .id = 401,
                                                   .name = "Ember",
                                                   .kind = SCRIPT_CAPABILITY_PROTOTYPE,
                                                   .completion_outcome = -1 };
    capabilities[3] = (script_catalog_capability){ .id = 402,
                                                   .name = "Spawn",
                                                   .parameters = &spawn_parameter,
                                                   .parameter_count = 1,
                                                   .completion_outcome = -1 };
    catalog.capability_count = 4;
    bool valid = load(source, count);
    authoring_navigation_free(navigation);
    navigation = authoring_navigation_new_project(documents, count, &catalog, definitions, 1, NULL, 0);
    return valid;
}

static bool names_prototype(int document, const char *token, uint64_t id)
{
    const authoring_reference *reference = at(document, token);
    return reference && reference->target.kind == AUTHORING_PROTOTYPE &&
           reference->target.id == id && reference->kind == AUTHORING_HANDLE;
}

/* A call that omits a defaulted parameter references the prototype only at its default. */
/* The project catalog reads content/input.kdl, so `input.Jump`, `on Jump` and
 * the map's `Jump` node name one action. */
static bool input_map_project()
{
    static const char source[] = "Jumper :: behavior {\n"
                                 "    held: bool = false;\n"
                                 "    on Jump :: () {\n"
                                 "        held = input.Jump.held;\n"
                                 "        held = input.jump.pressed;\n"
                                 "    }\n"
                                 "}\n";
    static const script_catalog_parameter action = { .name = "Action",
                                                     .type = { .storage = PIN_STR } };
    static const script_catalog_parameter value = { .name = "Value",
                                                    .type = { .storage = PIN_BOOL } };
    for (int i = 0; i < 2; i++) {
        const char *name = i ? INPUT_ACTION_PRESSED : INPUT_ACTION_HELD;
        capabilities[i] = (script_catalog_capability){
            .id = script_catalog_id("capability", name),
            .name = name,
            .kind = SCRIPT_CAPABILITY_FUNCTION,
            .parameters = &action,
            .parameter_count = 1,
            .results = &value,
            .result_count = 1,
            .effects = SCRIPT_EFFECT_READ,
            .completion_outcome = -1,
        };
    }
    catalog = (script_catalog){ .capabilities = capabilities, .capability_count = 2 };
    char root[] = "/tmp/chibi-input-navigation-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    fputs("Jump {\n    (array)sources \"Space\"\n}\nDash {\n    (array)sources \"X\"\n}\n", file);
    fclose(file);
    input_catalog = source_catalog_new(root, &catalog);
    unlink(path);
    rmdir(root);
    REQUIRE(!source_catalog_diagnostic_count(input_catalog));
    const script_catalog *view = source_catalog_view(input_catalog);
    document_count = 1;
    documents[0] = (authoring_document){ .path = "behaviors/Jumper.chibi",
                                         .syntax = script_parse(source) };
    documents[0].analysis = script_analyze_document(
        documents[0].syntax,
        view,
        documents[0].path
    );
    for (int i = 0; i < documents[0].analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "input navigation: %s\n",
            documents[0].analysis->diagnostics[i].message
        );
    }
    REQUIRE(documents[0].analysis->valid);
    navigation = authoring_navigation_new_project(
        documents,
        1,
        view,
        source_catalog_definitions(input_catalog),
        source_catalog_definition_count(input_catalog),
        source_catalog_references(input_catalog),
        source_catalog_reference_count(input_catalog)
    );
    return true;
}
TEST_CASE(navigation_connects_input_actions_to_the_input_map)
{
    REQUIRE(input_map_project());
    const authoring_reference *use = at(0, "Jump.held");
    REQUIRE(use && use->kind == AUTHORING_READ);
    REQUIRE(
        use->target.kind == AUTHORING_CALLABLE &&
        use->target.id == script_catalog_id("event", "Jump")
    );
    REQUIRE(use->location.range.end - use->location.range.start == 4);
    const authoring_reference *handler = at(0, "Jump ::"),
                              *other = at(0, "jump.pressed");
    REQUIRE(handler && authoring_target_equal(handler->target, use->target));
    REQUIRE(other && authoring_target_equal(other->target, use->target));
    const authoring_reference *definition = authoring_navigation_definition(navigation, use->target);
    REQUIRE(definition && definition->location.document == 1);
    const char *map = authoring_navigation_document_text(navigation, 1);
    REQUIRE(authoring_navigation_document_kind(navigation, 1) == SOURCE_TEXT_KDL);
    REQUIRE(definition->location.range.start == 0 && definition->location.range.end == 4);
    REQUIRE(definition->location.range.line == 1 && definition->location.range.column == 1);
    REQUIRE(!strncmp(map, "Jump {", 6));
    const authoring_reference
        *dash = authoring_navigation_at(navigation, 1, (int)(strstr(map, "Dash") - map));
    REQUIRE(dash && dash->kind == AUTHORING_DEFINE && dash->location.range.line == 4);
    REQUIRE(references(use->target) == 4);
    return true;
}
/* Scene binders can define one global name in multiple scenes. Each declaration
 * and qualified use must share the same field identity. */
static bool scene_globals_project()
{
    static const script_catalog_parameter key = { .name = "key",
                                                  .type = { .storage = PIN_STR } };
    static const script_catalog_parameter entity = { .name = "entity",
                                                     .type = { .storage = PIN_HANDLE,
                                                               .nominal = 31 } };
    static const script_catalog_field bindings[] = {
        { .id = 40,
          .name = "Level",
          .type = { .storage = PIN_HANDLE, .nominal = 31 },
          .read = 200,
          .read_key = "Level" },
        { .id = 41,
          .name = "Camera",
          .type = { .storage = PIN_HANDLE, .nominal = 31 },
          .read = 200,
          .read_key = "Camera" },
    };
    static const script_catalog_type scene_types[] = {
        { .id = 30,
          .name = "SceneGlobals",
          .type = { .storage = PIN_STR, .nominal = 30, .flags = SCRIPT_TYPE_NAMESPACE },
          .fields = bindings,
          .field_count = 2 },
        { .id = 31, .name = "entity", .type = { .storage = PIN_HANDLE, .nominal = 31 } },
    };
    static const script_catalog_global scene_globals[] = {
        { .id = 301,
          .name = "globals",
          .type = { .storage = PIN_STR, .nominal = 30, .flags = SCRIPT_TYPE_NAMESPACE } },
    };
    static const source_catalog_definition definitions[] = {
        { .id = 40,
          .field = true,
          .owner = 30,
          .name = "Level",
          .path = "scenes/First.kdl",
          .text = "GlobalBinder {\n    Name \"Level\"\n}\n",
          .range = { 24, 31, 2, 10, 0 } },
        { .id = 40,
          .field = true,
          .owner = 30,
          .name = "Level",
          .path = "scenes/Second.kdl",
          .text = "GlobalBinder {\n    Name \"Level\"\n}\n",
          .range = { 24, 31, 2, 10, 0 } },
        { .id = 41,
          .field = true,
          .owner = 30,
          .name = "Camera",
          .path = "scenes/Camera.kdl",
          .text = "GlobalBinder {\n    Name \"Camera\"\n}\n",
          .range = { 24, 32, 2, 10, 0 } },
    };
    const char *sources[] = {
        "App :: behavior { on Tick :: () { level:=globals.Level; "
        "other:=globals.level; label:=\"Level\"; } }"
    };
    base_catalog();
    capabilities[1] = (script_catalog_capability){ .id = 200,
                                                   .name = "GetGlobal",
                                                   .parameters = &key,
                                                   .parameter_count = 1,
                                                   .results = &entity,
                                                   .result_count = 1,
                                                   .effects = SCRIPT_EFFECT_READ,
                                                   .completion_outcome = -1 };
    catalog.capability_count = 2;
    catalog.types = scene_types;
    catalog.type_count = 2;
    catalog.globals = scene_globals;
    catalog.global_count = 1;
    catalog.entity_type = 31;
    bool valid = load(sources, 1);
    authoring_navigation_free(navigation);
    navigation = authoring_navigation_new_declarations(documents, 1, &catalog, definitions, 3);
    return valid;
}

TEST_CASE(catalog_publishes_union_payloads_from_later_libraries)
{
    base_catalog();
    char root[] = "/tmp/chibi-union-catalog-XXXXXX", first[256], second[256];
    REQUIRE(mkdtemp(root));
    snprintf(first, sizeof first, "%s/libraries/First.chibi", root);
    snprintf(second, sizeof second, "%s/libraries/Second.chibi", root);
    const source_catalog_edit edits[] = {
        { .path = first, .text = "First :: library { Envelope :: union { Some(value: Second.Payload) | Empty }; Pass :: (value: Envelope) -> Envelope { return value; } }" },
        { .path = second, .text = "Second :: library { Payload :: union { Ready(label: string) | \"missing\" }; Identity :: (value: int | Cancelled) -> int | Cancelled { return value; } }" },
    };
    input_catalog = source_catalog_replaced(root, &catalog, edits, 2);
    rmdir(root);
    for (int i = 0; i < source_catalog_diagnostic_count(input_catalog); i++) {
        fprintf(
            stderr,
            "union catalog: %s\n",
            source_catalog_diagnostic_at(input_catalog, i)->message
        );
    }
    REQUIRE(!source_catalog_diagnostic_count(input_catalog));
    const script_catalog *view = source_catalog_view(input_catalog);
    int envelope_index = script_catalog_find_type(view, "First.Envelope");
    int payload_index = script_catalog_find_type(view, "Second.Payload");
    REQUIRE(envelope_index >= 0 && payload_index >= 0);
    const script_catalog_type *envelope = &view->types[envelope_index];
    const script_catalog_type *payload = &view->types[payload_index];
    REQUIRE(envelope->variant_count == 2 && payload->variant_count == 2);
    REQUIRE(!strcmp(envelope->source_name, "Envelope"));
    REQUIRE(!strcmp(envelope->variants[0].name, "Some"));
    REQUIRE(envelope->variants[0].field_count == 1);
    REQUIRE(envelope->variants[0].fields[0].type.nominal == payload->id);
    REQUIRE(!strcmp(payload->variants[0].fields[0].name, "label"));
    REQUIRE(payload->variants[0].fields[0].type.storage == PIN_STR);
    REQUIRE(payload->variants[1].is_literal);
    REQUIRE(!strcmp(pin_text(payload->variants[1].literal).bytes, "missing"));
    int pass = script_catalog_find_capability(view, "First.Pass", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(pass >= 0);
    REQUIRE(view->capabilities[pass].parameter_count == 1);
    REQUIRE(view->capabilities[pass].result_count == 1);
    REQUIRE(view->capabilities[pass].parameters[0].type.nominal == envelope->id);
    REQUIRE(view->capabilities[pass].results[0].type.nominal == envelope->id);
    bool internal = false;
    for (int i = 0; i < view->type_count; i++) {
        const script_catalog_type *type = &view->types[i];
        if (!type->internal) {
            continue;
        }
        internal = true;
        REQUIRE(!type->source_name);
        REQUIRE(strncmp(type->name, "First.", 6) && strncmp(type->name, "Second.", 7));
        REQUIRE(script_catalog_type_by_id(view, type->id) == type);
        REQUIRE(script_catalog_find_type(view, type->name) < 0);
    }
    REQUIRE(internal);
    return true;
}

TEST_CASE(catalog_preserves_signature_only_union_and_task_exports)
{
    base_catalog();
    char root[] = "/tmp/chibi-signature-catalog-XXXXXX";
    char library[256], behavior[256], system[256];
    REQUIRE(mkdtemp(root));
    snprintf(library, sizeof library, "%s/libraries/Values.chibi", root);
    snprintf(behavior, sizeof behavior, "%s/behaviors/Owner.chibi", root);
    snprintf(system, sizeof system, "%s/systems/Active.chibi", root);
    const source_catalog_edit edits[] = {
        { .path = library, .text = "Values :: library { Identity :: (value: int | Cancelled) -> int | Cancelled { return value; } Keep :: (value: task(int)) -> task(int) { return value; } Twice :: (value: int) -> int { return value * 2; } }" },
        { .path = behavior,
          .text = "// Library types remain exported.\n"
                  "Owner :: behavior { score: int = 1; }" },
        { .path = system, .text = "Active :: system() { count: retain int = 0; }" },
    };
    input_catalog = source_catalog_replaced(root, &catalog, edits, 3);
    REQUIRE(!rmdir(root));
    REQUIRE(!source_catalog_diagnostic_count(input_catalog));
    REQUIRE(source_catalog_source_count(input_catalog) == 3);
    const script_catalog *view = source_catalog_view(input_catalog);
    int identity = script_catalog_find_capability(view, "Values.Identity", SCRIPT_CAPABILITY_FUNCTION);
    int keep = script_catalog_find_capability(view, "Values.Keep", SCRIPT_CAPABILITY_FUNCTION);
    int twice = script_catalog_find_capability(view, "Values.Twice", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(identity >= 0 && keep >= 0 && twice >= 0);
    const script_catalog_capability *functions[] = { &view->capabilities[identity],
                                                     &view->capabilities[keep] };
    for (int i = 0; i < 2; i++) {
        REQUIRE(functions[i]->parameter_count == 1 && functions[i]->result_count == 1);
        script_type parameter = functions[i]->parameters[0].type;
        REQUIRE(script_type_equal(parameter, functions[i]->results[0].type));
        const script_catalog_type *type = script_catalog_type_by_id(
            view,
            i ? parameter.parameter : parameter.nominal
        );
        REQUIRE(type && type->internal);
        if (i) {
            REQUIRE(parameter.flags & SCRIPT_TYPE_TASK);
            REQUIRE(type->type.storage == PIN_I32);
        } else {
            REQUIRE(parameter.flags & SCRIPT_TYPE_UNION);
            REQUIRE(type->variant_count == 2);
        }
    }
    return true;
}

TEST_CASE(catalog_preserves_invalid_behavior_diagnostics)
{
    base_catalog();
    char root[] = "/tmp/chibi-invalid-catalog-XXXXXX", invalid[256], incomplete[256];
    REQUIRE(mkdtemp(root));
    snprintf(invalid, sizeof invalid, "%s/behaviors/Invalid.chibi", root);
    snprintf(incomplete, sizeof incomplete, "%s/behaviors/Incomplete.chibi", root);
    const source_catalog_edit edits[] = {
        { .path = invalid, .text = "Invalid :: behavior { value: Missing; }" },
        { .path = incomplete, .text = "Incomplete :: behavior { value: int = 1;" },
    };
    input_catalog = source_catalog_replaced(root, &catalog, edits, 2);
    REQUIRE(!rmdir(root));
    bool invalid_reported = false, incomplete_reported = false, recovered = false;
    for (int i = 0; i < source_catalog_diagnostic_count(input_catalog); i++) {
        const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(input_catalog, i);
        invalid_reported |= !strcmp(diagnostic->path, invalid) &&
                            !strcmp(diagnostic->message, "Unknown type 'Missing'");
        incomplete_reported |= !strcmp(diagnostic->path, incomplete);
    }
    for (int i = 0; i < source_catalog_source_count(input_catalog); i++) {
        const source_catalog_source *source = source_catalog_source_at(input_catalog, i);
        if (strcmp(source->path, incomplete)) {
            continue;
        }
        REQUIRE(source->syntax->diagnostic_count && !source->declarations->valid);
        for (int j = 0; j < source->declarations->symbol_count; j++) {
            recovered |= !strcmp(source->declarations->symbols[j].name, "value");
        }
    }
    REQUIRE(invalid_reported && incomplete_reported && recovered);
    return true;
}

TEST_CASE(catalog_owns_union_variant_names_fields_and_literals)
{
    base_catalog();
    char name[] = "Decision", variant[] = "Ready", field[] = "payload";
    char unit[] = "seconds", read_key[] = "read", literal[] = "done";
    script_catalog_field payload = { .name = field,
                                     .unit = unit,
                                     .read_key = read_key,
                                     .type = { .storage = PIN_STR } };
    script_catalog_variant variants[] = {
        { .name = variant, .tag = 901, .tagged = true, .fields = &payload, .field_count = 1 },
        { .name = literal,
          .tag = 902,
          .is_literal = true,
          .literal = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = literal,
                                                    .length = (uint32_t)strlen(literal) } } },
    };
    script_catalog_type type = {
        .id = 900,
        .name = name,
        .source_name = name,
        .type = { .storage = PIN_UNION, .nominal = 900, .flags = SCRIPT_TYPE_UNION },
        .variants = variants,
        .variant_count = 2,
        .size = sizeof(void *)
    };
    script_catalog native = { .types = &type, .type_count = 1 };
    char root[] = "/tmp/chibi-union-snapshot-XXXXXX";
    REQUIRE(mkdtemp(root));
    input_catalog = source_catalog_new(root, &native);
    rmdir(root);
    REQUIRE(!source_catalog_diagnostic_count(input_catalog));
    script_catalog_parameter result = { .type = type.type };
    script_catalog_capability callable = { .kind = SCRIPT_CAPABILITY_FUNCTION,
                                           .results = &result,
                                           .result_count = 1 };
    uint64_t abi = script_catalog_capability_abi(&native, &callable);
    payload.type.storage = PIN_I32;
    REQUIRE(script_catalog_capability_abi(&native, &callable) != abi);
    payload.type.storage = PIN_STR;
    name[0] = variant[0] = field[0] = unit[0] = read_key[0] = literal[0] = '!';
    variants[0].field_count = 0;
    payload.type.storage = PIN_I32;
    const script_catalog_type *owned = &source_catalog_view(input_catalog)->types[0];
    REQUIRE(!strcmp(owned->name, "Decision") && !strcmp(owned->source_name, "Decision"));
    REQUIRE(owned->variant_count == 2 && owned->variants[0].field_count == 1);
    REQUIRE(!strcmp(owned->variants[0].name, "Ready"));
    REQUIRE(!strcmp(owned->variants[0].fields[0].name, "payload"));
    REQUIRE(!strcmp(owned->variants[0].fields[0].unit, "seconds"));
    REQUIRE(!strcmp(owned->variants[0].fields[0].read_key, "read"));
    REQUIRE(owned->variants[0].fields[0].type.storage == PIN_STR);
    REQUIRE(!strcmp(owned->variants[1].name, "done"));
    REQUIRE(!strcmp(pin_text(owned->variants[1].literal).bytes, "done"));
    REQUIRE(script_catalog_capability_abi(source_catalog_view(input_catalog), &callable) == abi);
    return true;
}

TEST_CASE(catalog_rejects_duplicate_exported_union_names)
{
    base_catalog();
    char root[] = "/tmp/chibi-union-duplicate-XXXXXX", first[256], second[256];
    REQUIRE(mkdtemp(root));
    snprintf(first, sizeof first, "%s/libraries/First.chibi", root);
    snprintf(second, sizeof second, "%s/libraries/Second.chibi", root);
    const source_catalog_edit edits[] = {
        { .path = first, .text = "Same :: library { Choice :: union { Left }; }" },
        { .path = second, .text = "Same :: library { Choice :: union { Right }; }" },
    };
    input_catalog = source_catalog_replaced(root, &catalog, edits, 2);
    rmdir(root);
    bool duplicate = false;
    for (int i = 0; i < source_catalog_diagnostic_count(input_catalog); i++) {
        duplicate |= strstr(
                         source_catalog_diagnostic_at(input_catalog, i)->message,
                         "Duplicate exported type name 'Same.Choice'"
                     ) != NULL;
    }
    REQUIRE(duplicate);
    return true;
}

void test_language_navigation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(navigation_connects_input_actions_to_the_input_map);
    RUN_TEST_CASE(catalog_publishes_union_payloads_from_later_libraries);
    RUN_TEST_CASE(catalog_preserves_signature_only_union_and_task_exports);
    RUN_TEST_CASE(catalog_preserves_invalid_behavior_diagnostics);
    RUN_TEST_CASE(catalog_owns_union_variant_names_fields_and_literals);
    RUN_TEST_CASE(catalog_rejects_duplicate_exported_union_names);
    pu_setup(NULL, NULL);
}
