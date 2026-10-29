#include "execution/function.h"
#include "host/native_default.h"
#include "runtime/components.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"

#ifdef CHIBI_TEST_PREPARATION
#include "authoring/language_catalog.h"
#include "authoring/project_open.h"
#include "support/file_io.h"
static authoring_language_catalog *language;
static char migration_root[256], migration_content[288], migration_behaviors[320];
static char migration_manifest[288], migration_source[352];
static char migration_incomplete[352];
static char migration_canonical[352];
static char *migration_text, *migration_reopened;
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void test_native_catalog_enable();
static native_capabilities *native;
static arena values;
static void cleanup()
{
#ifdef CHIBI_TEST_PREPARATION
    authoring_language_catalog_free(language);
    language = NULL;
    free(migration_text);
    free(migration_reopened);
    migration_text = migration_reopened = NULL;
    if (migration_root[0]) {
        unlink(migration_source);
        unlink(migration_incomplete);
        unlink(migration_canonical);
        unlink(migration_manifest);
        rmdir(migration_behaviors);
        rmdir(migration_content);
        rmdir(migration_root);
        migration_root[0] = 0;
    }
#endif
    native_capabilities_free(native);
    native = NULL;
    arena_free(&values);
}
TEST_CASE(native_application_catalog_links_generated_kernels_and_input_reads)
{
    test_native_catalog_enable();
    arena_init(&values, 4096);
    char error[256] = { 0 };
    native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    if (!native) {
        fprintf(stderr, "native application catalog: %s\n", error);
    }
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    int index = script_catalog_find_capability(catalog, "move_transform", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    const script_catalog_capability *declaration = &catalog->capabilities[index];
    const native_capability_binding *binding = native_capabilities_find(
        native,
        declaration->id,
        script_catalog_capability_abi(catalog, declaration)
    );
    REQUIRE(binding && binding->call && binding->call->context);
    Transform input = { .x = 2, .y = 3 }, output = { 0 };
    const pin_value arguments[] = {
        { .type = PIN_STRUCT, .type_name = "Transform", .ptr = &input },
        { .type = PIN_F32, .f = -5 },
        { .type = PIN_F32, .f = 7 },
    };
    pin_value result = { .type = PIN_STRUCT, .type_name = "Transform", .ptr = &output };
    behavior_function_context context = { .value_storage = &values };
    REQUIRE(!binding->call->context(&context, NULL, arguments, &result));
    input = (Transform){ 99, 99 };
    REQUIRE(output.x == -3 && output.y == 10);
    REQUIRE(result.type == PIN_STRUCT && !strcmp(result.type_name, "Transform"));
    /* Actions are read by name; the input map declares their events. */
    index = script_catalog_find_capability(catalog, "Action Held", SCRIPT_CAPABILITY_FUNCTION);
    REQUIRE(index >= 0);
    declaration = &catalog->capabilities[index];
    REQUIRE(declaration->parameter_count == 1 && declaration->parameters[0].type.storage == PIN_STR);
    /* No host event takes an action's name, so a map may name Fire or Dash. */
    REQUIRE(script_catalog_find_capability(catalog, "Tick", SCRIPT_CAPABILITY_EVENT) >= 0);
    REQUIRE(script_catalog_find_capability(catalog, "Fire", SCRIPT_CAPABILITY_EVENT) < 0);
    /* Tick's payload is the step, dt, measured in seconds. */
    const script_catalog_capability *
        tick = &catalog->capabilities[script_catalog_find_capability(catalog, "Tick", SCRIPT_CAPABILITY_EVENT)];
    REQUIRE(tick->parameter_count == 1 && !strcmp(tick->parameters[0].name, "dt"));
    REQUIRE(tick->parameters[0].unit && !strcmp(tick->parameters[0].unit, "s"));
    REQUIRE(script_catalog_find_capability(catalog, "Dash", SCRIPT_CAPABILITY_EVENT) < 0);
    native_capabilities_free(native);
    native = NULL;
    REQUIRE(output.x == -3 && output.y == 10);
    return true;
}
TEST_CASE(native_catalog_ranges_come_from_validated_target_layouts)
{
    type_field fields[] = {
        { .name = "value",
          .type = PIN_F64,
          .size = sizeof(double),
          .range_contract = true,
          .min = 2,
          .max = 10 },
        { .name = "hint",
          .type = PIN_F64,
          .offset = sizeof(double),
          .size = sizeof(double),
          .min = 0,
          .max = 1 },
    };
    const type_desc layout = { .name = "test.Range",
                               .size = 2 * sizeof(double),
                               .fields = fields,
                               .field_count = 2 };
    const type_desc *layouts[] = { &layout };
    const script_catalog_field declarations[] = {
        { .id = 2, .name = "value", .type = { .storage = PIN_F64 } },
        { .id = 3,
          .name = "hint",
          .type = { .storage = PIN_F64 },
          .offset = sizeof(double),
          .range_contract = true,
          .min = 0,
          .max = 1 },
    };
    const script_catalog_type type = {
        .id = 1,
        .name = "test.Range",
        .type = { .storage = PIN_STRUCT, .nominal = 1 },
        .size = layout.size,
        .fields = declarations,
        .field_count = 2,
    };
    const script_catalog catalog = { .types = &type, .type_count = 1 };
    const native_capability_pack pack = {
        .catalog = &catalog,
        .layouts = layouts,
        .layout_count = 1,
    };
    char error[256];
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(native);
    const script_catalog_type *owned = &native_capabilities_catalog(native)->types[0];
    REQUIRE(owned->fields[0].range_contract);
    REQUIRE(owned->fields[0].min == 2 && owned->fields[0].max == 10);
    REQUIRE(!owned->fields[1].range_contract);
    native_capabilities_free(native);
    native = NULL;
    fields[0].min = NAN;
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(!native);
    REQUIRE(strstr(error, "Invalid native field range contract"));
    return true;
}
TEST_CASE(native_catalog_preserves_builtin_relationship_identity_and_traits)
{
    script_catalog_relationship relationship = { .id = SCRIPT_CHILD_OF,
                                                 .name = "ChildOf",
                                                 .exclusive = true,
                                                 .traversable = true };
    const script_catalog catalog = { .relationships = &relationship,
                                     .relationship_count = 1 };
    const native_capability_pack pack = { .catalog = &catalog };
    char error[256];
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(native);
    REQUIRE(native_capabilities_catalog(native)->relationships[0].id == SCRIPT_CHILD_OF);
    native_capabilities_free(native);
    native = NULL;
    const script_catalog_relationship conflicts[] = {
        { .id = 601, .name = "ChildOf", .exclusive = true, .traversable = true },
        { .id = SCRIPT_CHILD_OF, .name = "Parent", .exclusive = true, .traversable = true },
        { .id = SCRIPT_CHILD_OF, .name = "ChildOf", .exclusive = true },
        { .id = SCRIPT_CHILD_OF, .name = "ChildOf" },
    };
    for (int i = 0; i < 4; i++) {
        relationship = conflicts[i];
        native = native_capabilities_new(&pack, 1, error, sizeof error);
        REQUIRE(!native);
        REQUIRE(strstr(error, "Conflicting built-in ChildOf relationship"));
    }
    return true;
}
/* The first six IDs collide in the initial identity table. Later rows force
 * growth and include distinct high bits and the largest uint64 identity. */
static uint64_t catalog_test_id(int row)
{
    static const uint64_t collisions[] = { 3, 50, 69, 73, 83, 116 };
    return row < 6   ? collisions[row]
         : row == 95 ? UINT64_MAX
                     : UINT64_C(0x8000000000000000) | ((uint64_t)row << 32) | 3;
}
TEST_CASE(native_catalog_identity_lookup_preserves_order_and_duplicate_errors)
{
    enum
    {
        COUNT = 96,
        HALF = COUNT / 2
    };
    char name[] = "Probe";
    const script_catalog_parameter result = {
        .name = "Value",
        .type = { .storage = PIN_HANDLE, .nominal = script_catalog_id("type", "ecs.entity") }
    };
    script_catalog_capability declarations[COUNT];
    native_capability_binding bindings[COUNT];
    for (int i = 0; i < COUNT; i++) {
        declarations[i] = (script_catalog_capability){
            .id = catalog_test_id(i),
            .name = name,
            .kind = SCRIPT_CAPABILITY_FUNCTION,
            .operation = SCRIPT_OPERATION_OWNER,
            .effects = SCRIPT_EFFECT_INVOCATION_READ,
            .pure = true,
            .completion_outcome = -1,
            .results = &result,
            .result_count = 1,
        };
        bindings[i] = (native_capability_binding){ .id = declarations[i].id,
                                                   .intrinsic = BF_OWNER };
    }
    const script_catalog_type type = { .id = 711,
                                       .name = "Alias",
                                       .type = { .storage = PIN_I32 },
                                       .size = sizeof(int) };
    const script_catalog_relationship relation = { .id = SCRIPT_CHILD_OF,
                                                   .name = "ChildOf",
                                                   .exclusive = true,
                                                   .traversable = true };
    script_catalog catalogs[] = {
        { .capabilities = declarations,
          .capability_count = HALF,
          .types = &type,
          .type_count = 1,
          .relationships = &relation,
          .relationship_count = 1 },
        { .capabilities = declarations + HALF,
          .capability_count = HALF,
          .types = &type,
          .type_count = 1,
          .relationships = &relation,
          .relationship_count = 1 },
    };
    native_capability_pack packs[] = {
        { .catalog = &catalogs[0], .bindings = bindings, .binding_count = HALF },
        { .catalog = &catalogs[1], .bindings = bindings + HALF, .binding_count = HALF },
    };
    char error[256];
    native = native_capabilities_new(packs, 2, error, sizeof error);
    REQUIRE(native);
    name[0] = 'X';
    const script_catalog *owned = native_capabilities_catalog(native);
    REQUIRE(
        owned->capability_count == COUNT && owned->type_count == 1 &&
        owned->relationship_count == 1
    );
    for (int i = 0; i < COUNT; i++) {
        const script_catalog_capability *capability = &owned->capabilities[i];
        REQUIRE(capability->id == catalog_test_id(i) && !strcmp(capability->name, "Probe"));
        const native_capability_binding *binding = native_capabilities_find(
            native,
            capability->id,
            script_catalog_capability_abi(owned, capability)
        );
        REQUIRE(binding && binding->intrinsic == BF_OWNER);
    }
    native_capabilities_free(native);
    native = NULL;
    for (int i = 0; i < 2; i++) {
        int row = i ? HALF : 6;
        declarations[row].id = declarations[2].id;
        /* Duplicate detection still precedes schema/default validation. */
        declarations[row].parameter_count = -1;
        native = native_capabilities_new(packs, 2, error, sizeof error);
        REQUIRE(!native && strstr(error, "Duplicate native capability"));
        declarations[row].id = catalog_test_id(row);
        declarations[row].parameter_count = 0;
    }
    declarations[0].id = 0;
    native = native_capabilities_new(packs, 2, error, sizeof error);
    REQUIRE(!native && strstr(error, "Invalid native capability declaration"));
    declarations[0].id = catalog_test_id(0);
    bindings[COUNT - 1].id = UINT64_C(0x7fffffffffffffff);
    native = native_capabilities_new(packs, 2, error, sizeof error);
    REQUIRE(!native && strstr(error, "Invalid native execution binding"));
    return true;
}
#ifdef CHIBI_TEST_PREPARATION
TEST_CASE(language_catalog_identity_lookup_keeps_namespaces_zero_and_order)
{
    enum
    {
        COUNT = 96
    };
    const script_catalog_capability first = { .id = 901,
                                              .name = "Native",
                                              .kind = SCRIPT_CAPABILITY_EVENT };
    const script_catalog base = { .capabilities = &first, .capability_count = 1 };
    const native_capability_pack pack = { .catalog = &base };
    char error[256];
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(native);
    script_catalog_type types[COUNT];
    script_catalog_capability capabilities[COUNT];
    script_catalog_global globals[COUNT];
    for (int i = 0; i < COUNT; i++) {
        uint64_t id = i ? catalog_test_id(i) : 0;
        types[i] = (script_catalog_type){ .id = id,
                                          .name = "Type",
                                          .type = { .storage = PIN_I32 } };
        capabilities[i] = (script_catalog_capability){ .id = id,
                                                       .name = "Event",
                                                       .kind = SCRIPT_CAPABILITY_EVENT };
        globals[i] = (script_catalog_global){ .id = id,
                                              .name = "Global",
                                              .type = { .storage = PIN_I32 } };
    }
    const script_catalog project = { .types = types,
                                     .type_count = COUNT,
                                     .capabilities = capabilities,
                                     .capability_count = COUNT,
                                     .globals = globals,
                                     .global_count = COUNT };
    const authoring_language_catalog_input input = { .native = native,
                                                     .project = &project };
    language = authoring_language_catalog_new(&input, error, sizeof error);
    REQUIRE(language);
    const script_catalog *owned = authoring_language_catalog_view(language);
    REQUIRE(
        owned->type_count == COUNT && owned->capability_count == COUNT + 1 &&
        owned->global_count == COUNT
    );
    REQUIRE(owned->capabilities[0].id == first.id);
    for (int i = 0; i < COUNT; i++) {
        uint64_t id = i ? catalog_test_id(i) : 0;
        REQUIRE(
            owned->types[i].id == id && owned->capabilities[i + 1].id == id &&
            owned->globals[i].id == id
        );
    }
    authoring_language_catalog_free(language);
    language = NULL;
    for (int kind = 0; kind < 3; kind++) {
        uint64_t *id = kind == 0 ? &types[COUNT - 1].id
                     : kind == 1 ? &capabilities[COUNT - 1].id
                                 : &globals[COUNT - 1].id;
        *id = 0;
        language = authoring_language_catalog_new(&input, error, sizeof error);
        REQUIRE(!language && strstr(error, "Conflicting or unretainable"));
        *id = catalog_test_id(COUNT - 1);
    }
    capabilities[0].id = first.id;
    language = authoring_language_catalog_new(&input, error, sizeof error);
    REQUIRE(!language && strstr(error, "Conflicting or unretainable"));
    return true;
}
TEST_CASE(language_catalog_retains_and_validates_merged_relationships)
{
    const script_catalog_relationship base_rows[] = {
        { .id = SCRIPT_CHILD_OF, .name = "ChildOf", .exclusive = true, .traversable = true },
        { .id = 700, .name = "Targets" },
    };
    const script_catalog base = { .relationships = base_rows, .relationship_count = 2 };
    const native_capability_pack pack = { .catalog = &base };
    char error[256];
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(native);
    char name[] = "Partners";
    script_catalog_relationship project_rows[] = {
        base_rows[1],
        { .id = 701, .name = name, .exclusive = true }
    };
    const script_catalog project = { .relationships = project_rows,
                                     .relationship_count = 2 };
    const script_catalog_relationship loaded_rows[] = { base_rows[0],
                                                        { .id = 702, .name = "Edges" } };
    const script_catalog loaded = { .relationships = loaded_rows,
                                    .relationship_count = 2 };
    const authoring_language_catalog_input input = { .native = native,
                                                     .project = &project };
    REQUIRE(!script_catalog_project());
    script_catalog_set_project(&loaded);
    language = authoring_language_catalog_new(&input, error, sizeof error);
    script_catalog_set_project(NULL);
    REQUIRE(language);
    name[0] = 'X';
    const script_catalog *owned = authoring_language_catalog_view(language);
    REQUIRE(owned->relationship_count == 4);
    REQUIRE(
        owned->relationships[0].id == SCRIPT_CHILD_OF &&
        owned->relationships[1].id == 700 &&
        owned->relationships[2].id == 701 && owned->relationships[3].id == 702
    );
    REQUIRE(!strcmp(owned->relationships[2].name, "Partners"));
    REQUIRE(owned->relationships[2].exclusive && !owned->relationships[2].traversable);
    authoring_language_catalog_free(language);
    language = NULL;
    const script_catalog_relationship invalid[] = {
        { .id = 700, .name = "Changed" },
        { .id = 703, .name = "Targets" },
        { .id = 700, .name = "Targets", .exclusive = true },
        { .id = 0, .name = "Invalid" },
        { .id = 704, .name = "" },
        { .id = 704, .name = "Invalid", .traversable = true },
        { .id = 704, .name = "ChildOf", .exclusive = true, .traversable = true },
        { .id = SCRIPT_CHILD_OF, .name = "Other", .exclusive = true, .traversable = true },
        { .id = SCRIPT_CHILD_OF, .name = "ChildOf", .exclusive = true },
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        project_rows[0] = invalid[i];
        language = authoring_language_catalog_new(&input, error, sizeof error);
        REQUIRE(!language && strstr(error, "Conflicting or unretainable"));
    }
    return true;
}
TEST_CASE(language_catalog_preserves_owned_template_and_specialization_metadata)
{
    char error[256], name[] = "Identity", binder[] = "T";
    script_template_parameter parameter = { .name = "value",
                                            .binder = binder,
                                            .binds_type = true };
    script_catalog_template templates[] = {
        { .id = 501,
          .name = name,
          .owner = "Generic",
          .kind = SCRIPT_TEMPLATE_FUNCTION,
          .generic = true,
          .parameters = &parameter,
          .parameter_count = 1 },
        { .id = 502, .name = "Second", .owner = "Generic" }
    };
    script_compile_binding binding = { .name = binder,
                                       .value = { .is_type = true,
                                                  .type = { .storage = PIN_I32 } } };
    script_catalog_specialization specialization = {
        .id = 601,
        .template_id = 501,
        .bindings = &binding,
        .binding_count = 1,
        .result = { .is_type = true, .type = { .storage = PIN_I32 } },
        .executable = true
    };
    script_catalog project = { .templates = templates,
                               .template_count = 2,
                               .specializations = &specialization,
                               .specialization_count = 1 };
    native_capability_pack invalid = { .catalog = &project };
    REQUIRE(!native_capabilities_new(&invalid, 1, error, sizeof error));
    REQUIRE(strstr(error, "Invalid native pack"));
    const script_catalog empty = { 0 };
    const native_capability_pack pack = { .catalog = &empty };
    native = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(native);
    authoring_language_catalog_input input = { .native = native, .project = &project };
    language = authoring_language_catalog_new(&input, error, sizeof error);
    REQUIRE(language);
    name[0] = 'X';
    binder[0] = 'U';
    const script_catalog *owned = authoring_language_catalog_view(language);
    REQUIRE(owned->template_count == 2 && owned->specialization_count == 1);
    REQUIRE(!strcmp(owned->templates[0].name, "Identity"));
    REQUIRE(!strcmp(owned->templates[0].parameters[0].binder, "T"));
    REQUIRE(!strcmp(owned->specializations[0].bindings[0].name, "T"));
    REQUIRE(owned->specializations[0].bindings[0].value.type.storage == PIN_I32);
    authoring_language_catalog_free(language);
    language = NULL;
    templates[1].id = templates[0].id;
    REQUIRE(!authoring_language_catalog_new(&input, error, sizeof error));
    REQUIRE(strstr(error, "Conflicting or unretainable"));
    return true;
}

static bool write_migration_fixture(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool okay = fputs(text, file) >= 0;
    if (fclose(file)) {
        okay = false;
    }
    return okay;
}

TEST_CASE(native_project_migration_finalizes_generated_accessors_retryably)
{
    test_native_catalog_enable();
    char error[512] = { 0 };
    native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    REQUIRE(
        script_catalog_find_capability(catalog, "get_transform", SCRIPT_CAPABILITY_FUNCTION) >= 0
    );
    REQUIRE(
        script_catalog_find_capability(catalog, "set_transform_x", SCRIPT_CAPABILITY_ACTION) >= 0
    );
    snprintf(migration_root, sizeof migration_root, "/tmp/chibi-native-migration.XXXXXX");
    REQUIRE(mkdtemp(migration_root));
    snprintf(migration_content, sizeof migration_content, "%s/content", migration_root);
    snprintf(migration_behaviors, sizeof migration_behaviors, "%s/behaviors", migration_content);
    snprintf(migration_manifest, sizeof migration_manifest, "%s/project.kdl", migration_root);
    snprintf(migration_source, sizeof migration_source, "%s/Demo.chibi", migration_behaviors);
    snprintf(migration_incomplete, sizeof migration_incomplete, "%s/Incomplete.chibi", migration_behaviors);
    snprintf(migration_canonical, sizeof migration_canonical, "%s/Canonical.chibi", migration_behaviors);
    REQUIRE(!mkdir(migration_content, 0700));
    REQUIRE(!mkdir(migration_behaviors, 0700));
    const char
        *incomplete = "Incomplete :: behavior { on MissingEvent :: () {} }\n";
    REQUIRE(write_migration_fixture(migration_incomplete, incomplete));
    const char *canonical = "Canonical :: behavior {\n    count: retain int = "
                            "1\n    on Tick :: () { count = count + 1 }\n}\n";
    REQUIRE(write_migration_fixture(migration_canonical, canonical));
    REQUIRE(
        write_migration_fixture(migration_manifest, "version 38\nname \"Native migration\"\ncontent \"content\"\nstart_scene \"Start.kdl\"\n")
    );
    /* A generated call with an unresolved receiver cannot be proven equivalent.
     * Unlike the unrelated MissingEvent document, it must block finalization. */
    REQUIRE(
        write_migration_fixture(migration_source, "Demo :: behavior { on Tick :: () { set_transform_x(2.0, missing_entity) } }\n")
    );
    REQUIRE(authoring_project_migrate(migration_root, native, error, sizeof error) == -1);
    REQUIRE(error[0]);
    migration_text = file_read_text(migration_manifest);
    REQUIRE(migration_text && strstr(migration_text, "version 38"));
    /* The failed upgrade left every source at the current version and said so
     * in the manifest, so going back to an old source means restoring the
     * manifest too. */
    REQUIRE(strstr(migration_text, "migrated"));
    free(migration_text);
    migration_text = NULL;
    const char
        *input = "Demo :: behavior { on Tick :: () { e := Self()\n"
                 "x := get_transform().Value.x\nset_transform_x(x, self) } }\n";
    REQUIRE(
        write_migration_fixture(migration_manifest, "version 38\nname \"Native migration\"\ncontent \"content\"\nstart_scene \"Start.kdl\"\n")
    );
    REQUIRE(write_migration_fixture(migration_source, input));
    int steps = authoring_project_migrate(migration_root, native, error, sizeof error);
    if (steps < 0) {
        fprintf(stderr, "native project migration: %s\n", error);
    }
    REQUIRE(steps == CHIBI_PROJECT_VERSION - 38);
    migration_text = file_read_text(migration_source);
    REQUIRE(migration_text);
    REQUIRE(strstr(migration_text, "e := self"));
    REQUIRE(strstr(migration_text, "self.Transform.x"));
    REQUIRE(
        !strstr(migration_text, "Self()") && !strstr(migration_text, "get_transform") &&
        !strstr(migration_text, "set_transform")
    );
    migration_reopened = file_read_text(migration_manifest);
    char version[32];
    snprintf(version, sizeof version, "version %d", CHIBI_PROJECT_VERSION);
    REQUIRE(migration_reopened && strstr(migration_reopened, version));
    free(migration_reopened);
    migration_reopened = NULL;
    REQUIRE(authoring_project_migrate(migration_root, native, error, sizeof error) == 0);
    chibi_project project = { 0 };
    REQUIRE(!authoring_project_open(&project, migration_root, native, error, sizeof error));
    chibi_project_free(&project);
    migration_reopened = file_read_text(migration_source);
    REQUIRE(migration_reopened && !strcmp(migration_text, migration_reopened));
    free(migration_reopened);
    migration_reopened = file_read_text(migration_incomplete);
    REQUIRE(migration_reopened && !strcmp(incomplete, migration_reopened));
    free(migration_reopened);
    migration_reopened = file_read_text(migration_canonical);
    REQUIRE(migration_reopened && !strcmp(canonical, migration_reopened));
    return true;
}
#endif
void test_tool_native_catalog()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_application_catalog_links_generated_kernels_and_input_reads);
    RUN_TEST_CASE(native_catalog_ranges_come_from_validated_target_layouts);
    RUN_TEST_CASE(native_catalog_preserves_builtin_relationship_identity_and_traits);
    RUN_TEST_CASE(native_catalog_identity_lookup_preserves_order_and_duplicate_errors);
#ifdef CHIBI_TEST_PREPARATION
    RUN_TEST_CASE(language_catalog_identity_lookup_keeps_namespaces_zero_and_order);
    RUN_TEST_CASE(language_catalog_retains_and_validates_merged_relationships);
    RUN_TEST_CASE(language_catalog_preserves_owned_template_and_specialization_metadata);
    RUN_TEST_CASE(native_project_migration_finalizes_generated_accessors_retryably);
#endif
    pu_clear_setup();
}
