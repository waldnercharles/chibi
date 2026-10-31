#include "compiler_cache.h"
#include "ecs/ecs.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_target.h"
#include "types/catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* Selected oracles from the archived source-system suite, through the
 * authored compiler and a synthetic ECS catalog, without the
 * archived game registry, generated game components or stage-zero compiler. */
static const char SOURCE[] = "Movement :: system(m:^Motion) {\n"
                             "    m.position += m.input * m.speed * dt;\n"
                             "}\n";
static const char EMPTY_ANY[] = "Membership :: system(m:^Motion, if "
                                "any(has(Marker), any())) {\n"
                                "    m.position += 1;\n"
                                "}\n";

enum
{
    MOTION_TYPE = 81001,
    ENTITY_TYPE = 81002,
    MOTION_COMPONENT = 81003,
    TICK_EVENT = 81004,
    TICK_ABI = 81005,
    MARKER_TYPE = 81006,
    MARKER_COMPONENT = 81007,
    COMPILE_BUDGET = 200000000,
    MODULE_BYTES = 65536,
};
typedef struct
{
    float position, speed, input;
} motion;
typedef struct
{
    bool present;
} marker;
static const type_field MOTION_FIELDS[] = {
    { .name = "position",
      .type = PIN_F32,
      .offset = offsetof(motion, position),
      .size = sizeof(float) },
    { .name = "speed", .type = PIN_F32, .offset = offsetof(motion, speed), .size = sizeof(float) },
    { .name = "input", .type = PIN_F32, .offset = offsetof(motion, input), .size = sizeof(float) },
};
static const type_desc MOTION = { .name = "Motion",
                                  .size = sizeof(motion),
                                  .fields = MOTION_FIELDS,
                                  .field_count = 3 };
static const type_field MARKER_FIELD = { .name = "present",
                                         .type = PIN_BOOL,
                                         .offset = offsetof(marker, present),
                                         .size = sizeof(bool) };
static const type_desc MARKER = { .name = "Marker",
                                  .size = sizeof(marker),
                                  .fields = &MARKER_FIELD,
                                  .field_count = 1 };
static const type_desc *const TYPES[] = { &MOTION, &MARKER };
static const type_catalog_pack TYPES_PACK = { .types = TYPES, .type_count = 2 };
static compiler_host *compiler;
static unsigned char *bytes;
static size_t byte_count;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static bool types_registered;

static void clear_runtime()
{
    execution_program_free(program);
    execution_module_free(module);
    ecs_world_free(world);
    program = NULL;
    module = NULL;
    world = NULL;
}

static void cleanup()
{
    clear_runtime();
    compiler_host_free(compiler);
    free(bytes);
    if (types_registered) {
        type_catalog_set_project(NULL);
        types_registered = false;
    }
    compiler = NULL;
    bytes = NULL;
    byte_count = 0;
}

static bool linked_type(void *, uint64_t id, execution_prepared_type *out)
{
    if (id == MOTION_TYPE || id == MARKER_TYPE) {
        const type_desc *type = id == MOTION_TYPE ? &MOTION : &MARKER;
        *out = (execution_prepared_type){ .name = type->name,
                                          .type = type,
                                          .storage = PIN_STRUCT,
                                          .size = type->size,
                                          .layout = type_layout_hash(type) };
        return true;
    }
    if (id == ENTITY_TYPE) {
        *out = (execution_prepared_type){ .name = "ecs.entity",
                                          .storage = PIN_HANDLE,
                                          .size = sizeof(uint64_t) };
        return true;
    }
    return false;
}
static bool linked_event(void *, uint64_t id, uint64_t abi)
{
    return id == TICK_EVENT && abi == TICK_ABI;
}

static char *compile_arguments(const char *source, size_t *length)
{
    const script_catalog empty = { 0 };
    size_t target_length;
    char *target_json = compiler_target_json(&empty, &target_length);
    yyjson_doc *target = target_json ? yyjson_read(target_json, target_length, 0)
                                     : NULL;
    free(target_json);
    if (!target || yyjson_arr_size(yyjson_doc_get_root(target)) != 6) {
        yyjson_doc_free(target);
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "systems/Fixture.chibi");
    yyjson_mut_arr_add_str(doc, arguments, source);
    for (size_t i = 0; i < 6; i++) {
        yyjson_mut_arr_append(
            arguments,
            yyjson_val_mut_copy(doc, yyjson_arr_get(yyjson_doc_get_root(target), i))
        );
    }
    yyjson_doc_free(target);
    yyjson_mut_val *events = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_val *event = yyjson_mut_arr_add_obj(doc, events);
    yyjson_mut_obj_add_str(doc, event, "name", "Tick");
    yyjson_mut_obj_add_uint(doc, event, "id", TICK_EVENT);
    yyjson_mut_obj_add_uint(doc, event, "abi", TICK_ABI);
    yyjson_mut_obj_add_int(doc, event, "first", 0);
    yyjson_mut_obj_add_int(doc, event, "count", 1);
    yyjson_mut_val *event_fields = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_val *step = yyjson_mut_arr_add_obj(doc, event_fields);
    yyjson_mut_obj_add_str(doc, step, "name", "dt");
    yyjson_mut_obj_add_int(doc, step, "storage", PIN_F32);
    yyjson_mut_obj_add_uint(doc, step, "nominal", 0);
    yyjson_mut_obj_add_false(doc, step, "has_default");
    yyjson_mut_obj_add_false(doc, step, "default_owner");
    yyjson_mut_val *types = yyjson_mut_arr_add_arr(doc, arguments);
    const type_desc *descriptors[] = { &MOTION, NULL, &MARKER };
    const uint64_t ids[] = { MOTION_TYPE, ENTITY_TYPE, MARKER_TYPE };
    const uint64_t components[] = { MOTION_COMPONENT, 0, MARKER_COMPONENT };
    for (int i = 0; i < 3; i++) {
        const type_desc *descriptor = descriptors[i];
        yyjson_mut_val *type = yyjson_mut_arr_add_obj(doc, types);
        yyjson_mut_obj_add_true(doc, type, "required");
        yyjson_mut_obj_add_int(doc, type, "underlying", -1);
        yyjson_mut_obj_add_str(doc, type, "name", descriptor ? descriptor->name : "ecs.entity");
        yyjson_mut_obj_add_uint(doc, type, "id", ids[i]);
        yyjson_mut_obj_add_uint(doc, type, "layout", descriptor ? type_layout_hash(descriptor) : 0);
        yyjson_mut_obj_add_int(doc, type, "storage", descriptor ? PIN_STRUCT : PIN_HANDLE);
        yyjson_mut_obj_add_int(doc, type, "size", descriptor ? descriptor->size : sizeof(uint64_t));
        yyjson_mut_obj_add_uint(doc, type, "component", components[i]);
        yyjson_mut_obj_add_int(doc, type, "first", i == 2 ? 3 : 0);
        yyjson_mut_obj_add_int(doc, type, "count", descriptor ? descriptor->field_count : 0);
        yyjson_mut_obj_add_false(doc, type, "relationship");
        yyjson_mut_obj_add_int(doc, type, "traits", 0);
        yyjson_mut_obj_add_false(doc, type, "error");
    }
    yyjson_mut_val *fields = yyjson_mut_arr_add_arr(doc, arguments);
    for (int i = 0; i < 4; i++) {
        const type_field *member = i < 3 ? &MOTION_FIELDS[i] : &MARKER_FIELD;
        yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
        yyjson_mut_obj_add_str(doc, field, "name", member->name);
        yyjson_mut_obj_add_int(doc, field, "storage", member->type);
        yyjson_mut_obj_add_uint(doc, field, "offset", member->offset);
        yyjson_mut_obj_add_true(doc, field, "writable");
        yyjson_mut_obj_add_false(doc, field, "c_string");
        yyjson_mut_obj_add_int(doc, field, "record", -1);
        yyjson_mut_obj_add_false(doc, field, "constant");
        yyjson_mut_obj_add_int(doc, field, "read", -1);
        yyjson_mut_obj_add_str(doc, field, "read_key", "");
    }
    yyjson_mut_arr_add_uint(doc, arguments, ENTITY_TYPE);
    /* No game operators, globals, native record makers or maker fields. */
    for (int i = 0; i < 4; i++) {
        yyjson_mut_arr_add_arr(doc, arguments);
    }
    char *json = yyjson_mut_write(doc, 0, length);
    yyjson_mut_doc_free(doc);
    return json;
}

static bool compile_system(const char *source)
{
    const compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core",
        .cache = test_compiler_cache(),
    };
    char error[256];
    compiler = compiler_bootstrap_load(&options, error, sizeof error);
    if (!compiler) {
        fprintf(stderr, "source system bootstrap: %s\n", error);
    }
    REQUIRE(compiler);
    size_t length;
    char *input = compile_arguments(source, &length);
    REQUIRE(input);
    compiler_host_result result = { 0 };
    bool called = compiler_host_invoke(
        compiler,
        compiler_host_entry("project:package_compiler:1:CompileBehavior:callable"),
        input,
        length,
        COMPILE_BUDGET,
        &result
    );
    free(input);
    yyjson_doc *doc = called ? yyjson_read(result.json, result.length, COMPILER_HOST_JSON_READ_FLAGS)
                             : NULL;
    yyjson_val *root = yyjson_doc_get_root(doc), *payload = yyjson_arr_get(root, 1);
    size_t count = yyjson_arr_size(payload);
    bool valid = called && result.flow_status == FLOW_OK &&
                 yyjson_arr_size(root) == 3 &&
                 yyjson_is_uint(yyjson_arr_get(root, 0)) &&
                 yyjson_get_uint(yyjson_arr_get(root, 0)) &&
                 yyjson_is_null(yyjson_arr_get(root, 2)) && count &&
                 count <= MODULE_BYTES;
    if (valid) {
        bytes = xmalloc(count);
        for (size_t i = 0; i < count; i++) {
            yyjson_val *value = yyjson_arr_get(payload, i);
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) > UINT8_MAX) {
                valid = false;
                break;
            }
            bytes[i] = (unsigned char)yyjson_get_uint(value);
        }
        byte_count = count;
    }
    if (!valid) {
        fprintf(
            stderr,
            "source system compile: %.1500s\n",
            called ? result.json : result.diagnostic
        );
    }
    yyjson_doc_free(doc);
    free(result.json);
    REQUIRE(valid);
    return true;
}

TEST_CASE(authored_system_arithmetic_uses_live_components_and_step)
{
    REQUIRE(compile_system(SOURCE));
    /* Prepared native struct values resolve their descriptor by name too. */
    type_catalog_set_project(&TYPES_PACK);
    types_registered = true;
    const execution_prepared_linker linker = { .type = linked_type,
                                               .event = linked_event };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        char error[256];
        module = execution_prepared_read(bytes, byte_count, &linker, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "source system link: %s\n", error);
        }
        REQUIRE(module);
        REQUIRE(execution_module_backend(module) == backends[b]);
        world = ecs_world_new();
        const ecs_component_desc component = { .id = MOTION_COMPONENT,
                                               .key = "fixture.Motion",
                                               .name = "Motion",
                                               .element_size = sizeof(motion),
                                               .type = &MOTION,
                                               .layout_hash = type_layout_hash(&MOTION) };
        REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
        ecs_entity first = ecs_world_create_entity(world),
                   second = ecs_world_create_entity(world);
        motion a = { .position = 0, .speed = 3, .input = 2 };
        motion z = { .position = 10, .speed = 4, .input = -1 };
        REQUIRE(ecs_world_record_add(world, first, MOTION_COMPONENT, &a, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add(world, second, MOTION_COMPONENT, &z, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 0, 0, 10000) == FLOW_OK);
        REQUIRE(((motion *)ecs_component_data(world, first, MOTION_COMPONENT))->position == 0);
        REQUIRE(((motion *)ecs_component_data(world, second, MOTION_COMPONENT))->position == 10);
        REQUIRE(execution_program_step(program, 1, .1, 10000) == FLOW_OK);
        /* The original oracle multiplies 2 * 3 * .1. The tolerance covers
         * f32 storage, not backend disagreement or a missed step. */
        const float tolerance = 0.000001f;
        REQUIRE(
            fabsf(((motion *)ecs_component_data(world, first, MOTION_COMPONENT))->position - .6f) < tolerance
        );
        REQUIRE(
            fabsf(
                ((motion *)ecs_component_data(world, second, MOTION_COMPONENT))->position - 9.6f
            ) < tolerance
        );
        ((motion *)ecs_component_data(world, first, MOTION_COMPONENT))->speed = 5;
        REQUIRE(execution_program_step(program, 2, .25, 10000) == FLOW_OK);
        REQUIRE(
            fabsf(((motion *)ecs_component_data(world, first, MOTION_COMPONENT))->position - 3.1f) < tolerance
        );
        REQUIRE(
            fabsf(
                ((motion *)ecs_component_data(world, second, MOTION_COMPONENT))->position - 8.6f
            ) < tolerance
        );
        clear_runtime();
    }
    return true;
}

TEST_CASE(authored_system_empty_any_does_not_admit_a_row)
{
    REQUIRE(compile_system(EMPTY_ANY));
    type_catalog_set_project(&TYPES_PACK);
    types_registered = true;
    const execution_prepared_linker linker = { .type = linked_type,
                                               .event = linked_event };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        char error[256];
        module = execution_prepared_read(bytes, byte_count, &linker, backends[b], error, sizeof error);
        if (!module) {
            fprintf(stderr, "source system link: %s\n", error);
        }
        REQUIRE(module);
        REQUIRE(execution_module_backend(module) == backends[b]);
        world = ecs_world_new();
        const uint64_t components[] = { MOTION_COMPONENT, MARKER_COMPONENT };
        for (int i = 0; i < 2; i++) {
            const ecs_component_desc component = {
                .id = components[i],
                .key = TYPES[i]->name,
                .name = TYPES[i]->name,
                .element_size = TYPES[i]->size,
                .type = TYPES[i],
                .layout_hash = type_layout_hash(TYPES[i])
            };
            REQUIRE(ecs_world_register_component(world, &component, NULL) == ECS_OK);
        }
        ecs_entity bare = ecs_world_create_entity(world),
                   marked = ecs_world_create_entity(world);
        const motion a = { .position = 0 }, z = { .position = 10 };
        const marker tag = { .present = true };
        REQUIRE(ecs_world_record_add(world, bare, MOTION_COMPONENT, &a, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add(world, marked, MOTION_COMPONENT, &z, 0) == ECS_OK);
        REQUIRE(ecs_world_record_add(world, marked, MARKER_COMPONENT, &tag, 0) == ECS_OK);
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        REQUIRE(execution_program_step(program, 1, .1, 10000) == FLOW_OK);
        /* Empty any() must not admit the bare row; the sibling has(Marker)
         * must still run the authored body, ruling out a vacuous pass. */
        REQUIRE(((motion *)ecs_component_data(world, bare, MOTION_COMPONENT))->position == 0);
        REQUIRE(((motion *)ecs_component_data(world, marked, MOTION_COMPONENT))->position == 11);
        clear_runtime();
    }
    return true;
}

void test_language_source_systems()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(authored_system_arithmetic_uses_live_components_and_step);
    RUN_TEST_CASE(authored_system_empty_any_does_not_admit_a_row);
    pu_clear_setup();
}
