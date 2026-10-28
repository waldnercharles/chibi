#include "native_calls.h"

#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "backend.h"
#include "execution/function_builder.h"
#include "execution/inspection.h"
#include "execution/native.h"
#include "execution/status.h"
#include "runtime/behaviors/source.h"
#include "runtime/native_definitions.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Calls from the VM into a library's native entries. The entries here are
 * C functions with the export ABI, standing in for an object compiled from
 * the library, so each case can compare them with the library's VM code. */
static char root[128];
static authoring_sources *sources;
static execution_natives *natives;
static execution_program *program;
static const script_catalog_parameter cells = { .name = "cells",
                                                .type = { .storage = PIN_F32 } };
static const script_catalog_capability tick = { .id = 10,
                                                .name = "Tick",
                                                .kind = SCRIPT_CAPABILITY_EVENT,
                                                .parameters = &cells,
                                                .parameter_count = 1,
                                                .completion_outcome = -1 };
static const script_catalog catalog = { .capabilities = &tick, .capability_count = 1 };
/* Tick and the text natives, which Label calls. */
static native_capabilities *natives_catalog;
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };

/* The functions the Probe calls, which native code can compile. */
#define PURE_FUNCTIONS                                                         \
    "    Reward :: (points: int, bonus: int = 5) -> int { return points * 2 "  \
    "+ bonus; }\n"                                                             \
    "    Positive :: (x: number) -> bool { return x > 0; }\n"                  \
    "    Wrap :: (x: u8) -> u8 { return x + 200; }\n"                          \
    "    Mix :: (a: int, b: bool, c: u8, d: i64, e: u64, f: number, g: f64) "  \
    "-> f64 {\n"                                                               \
    "        if (b) { return f64(a) + f64(c) + f64(d) + f64(e) + f * g; }\n"   \
    "        return g - f64(a);\n"                                             \
    "    }\n"                                                                  \
    "    Label :: (x: number) -> int { return "                                \
    "text_byte_length(format_text(\"{} cells\", x)); }\n"
const char NATIVE_CALLS_LIBRARY
    [] = "Native :: library {\n" PURE_FUNCTIONS NATIVE_CALLS_WIDE "}\n";
/* With functions that wait, exceed the entry's parameter bound or take a
 * reference, which no export can bind. */
static const char
    LIBRARY[] = "Native :: library {\n" PURE_FUNCTIONS
                "    Pause :: (x: int) -> int { yield; return x; }\n"
                "    Wide :: (a: int, b: int, c: int, d: int, e: int, f: int, "
                "g: int, h: int, i: int) -> int {\n"
                "        return a + i;\n"
                "    }\n"
                "    Bump :: (value: ^int) -> int { value^ += 1; return "
                "value^; }\n"
                "    Read :: (value: ^int) -> int { held := value; return "
                "held^; }\n"
                "    UnusedRef :: (value: ^int) -> int { return 7; }\n"
                "}\n";
static const char PROBE[] = "Probe :: behavior {\n"
                            "    total: int = 1;\n"
                            "    flag: bool = false;\n"
                            "    small: u8 = 0;\n"
                            "    wide: f64 = 0;\n"
                            "    label: int = 0;\n"
                            "    on Tick :: (cells: number) {\n"
                            "        total = Native.Reward(total);\n"
                            "        total = Native.Reward(total, -3);\n"
                            "        flag = Native.Positive(cells);\n"
                            "        small = Native.Wrap(small);\n"
                            "        wide = Native.Mix(total, flag, small, "
                            "i64(total), u64(7), cells, f64(cells));\n"
                            "        label = Native.Label(cells);\n"
                            "    }\n"
                            "}\n";

static int32_t int_word(uint64_t word)
{
    return (int32_t)(uint32_t)word;
}
static float number_word(uint64_t word)
{
    uint32_t bits = (uint32_t)word;
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
static double f64_word(uint64_t word)
{
    double value;
    memcpy(&value, &word, sizeof value);
    return value;
}
static uint64_t f64_bits(double value)
{
    uint64_t word;
    memcpy(&word, &value, sizeof word);
    return word;
}
/* Each entry computes what its library function does, as native code
 * compiled from it would, and counts its calls. */
static int calls;
static void reward(execution_native_context *, const uint64_t *arguments, uint64_t *results)
{
    calls++;
    uint32_t points = (uint32_t)arguments[0], bonus = (uint32_t)arguments[1];
    results[0] = (uint64_t)(int64_t)(int32_t)(points * 2 + bonus);
}
static void positive(execution_native_context *, const uint64_t *arguments, uint64_t *results)
{
    calls++;
    results[0] = number_word(arguments[0]) > 0;
}
static void wrap(execution_native_context *, const uint64_t *arguments, uint64_t *results)
{
    calls++;
    results[0] = (uint8_t)(arguments[0] + 200);
}
static void mix(execution_native_context *, const uint64_t *arguments, uint64_t *results)
{
    calls++;
    double a = int_word(arguments[0]), g = f64_word(arguments[6]);
    double sum = a + (double)(uint8_t)arguments[2] + (double)(int64_t)arguments[3] +
                 (double)arguments[4] + (double)number_word(arguments[5]) * g;
    results[0] = f64_bits(arguments[1] ? sum : g - a);
}
static void label(execution_native_context *context, const uint64_t *arguments, uint64_t *results)
{
    calls++;
    const execution_native_value values[] = {
        { EXECUTION_NATIVE_TEXT, 8, 0, "{} cells" },
        { EXECUTION_NATIVE_NUMBER, 0, arguments[0], NULL },
    };
    pin_string text;
    context->format_text(context, values, 2, &text);
    results[0] = text.length;
}
static const uint8_t REWARD[] = { EXECUTION_NATIVE_INT,
                                  EXECUTION_NATIVE_INT,
                                  EXECUTION_NATIVE_INT };
static const uint8_t POSITIVE[] = { EXECUTION_NATIVE_NUMBER, EXECUTION_NATIVE_BOOL };
static const uint8_t WRAP[] = { EXECUTION_NATIVE_U8, EXECUTION_NATIVE_U8 };
static const uint8_t MIX[] = { EXECUTION_NATIVE_INT, EXECUTION_NATIVE_BOOL,
                               EXECUTION_NATIVE_U8,  EXECUTION_NATIVE_I64,
                               EXECUTION_NATIVE_U64, EXECUTION_NATIVE_NUMBER,
                               EXECUTION_NATIVE_F64, EXECUTION_NATIVE_F64 };
static const uint8_t LABEL[] = { EXECUTION_NATIVE_NUMBER, EXECUTION_NATIVE_INT };
/* A library function's stable ID, as its prepared module records it. */
static uint64_t function_id(const char *name)
{
    char identity[128];
    snprintf(identity, sizeof identity, "libraries/Native.chibi:1:%s:callable", name);
    return script_catalog_id("symbol", identity);
}
static execution_native_export exported(const char *name, const uint8_t *types, int parameters, execution_native_entry entry)
{
    return (execution_native_export){ name,  function_id(name),
                                      types, parameters,
                                      1,     entry };
}
#define RUNTIME ((execution_native_runtime){ NATIVE_FORMAT_TEXT_HOST.value })
static execution_native_export EXPORTS[5];
static const execution_native_manifest MANIFEST = { EXECUTION_NATIVE_ABI, 5, EXPORTS };
/* The synchronous calls one Tick makes. */
enum
{
    TICK_CALLS = 6,
    TICKS = 4
};

static void setup()
{
    strcpy(root, "/tmp/chibi-native-calls-XXXXXX");
    assert(mkdtemp(root));
    const native_capability_pack events = { .catalog = &catalog };
    const native_definition_pack texts[] = { NATIVE_TEXT_DEFINITION_PACK };
    char error[256];
    natives_catalog = native_capabilities_from_definition_packs(&events, 1, texts, 1, error, sizeof error);
    assert(natives_catalog);
    EXPORTS[0] = exported("Reward", REWARD, 2, reward);
    EXPORTS[1] = exported("Positive", POSITIVE, 1, positive);
    EXPORTS[2] = exported("Wrap", WRAP, 1, wrap);
    EXPORTS[3] = exported("Mix", MIX, 7, mix);
    EXPORTS[4] = exported("Label", LABEL, 1, label);
}
static void clear()
{
    execution_program_free(program);
    program = NULL;
    execution_natives_free(natives);
    natives = NULL;
    authoring_sources_free(sources);
    sources = NULL;
}
static void cleanup()
{
    clear();
    native_capabilities_free(natives_catalog);
    natives_catalog = NULL;
    rmdir(root);
}
static const authoring_source *source(const char *name)
{
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *s = authoring_sources_at(sources, i);
        if (s->analysis->name && !strcmp(s->analysis->name, name)) {
            return s;
        }
    }
    return NULL;
}
static bool prepare_library(const char *text, behavior_backend backend)
{
    char library[256], probe[256];
    snprintf(library, sizeof library, "%s/libraries/Native.chibi", root);
    snprintf(probe, sizeof probe, "%s/behaviors/Probe.chibi", root);
    const source_catalog_edit edits[] = { { .path = library, .text = text },
                                          { .path = probe, .text = PROBE } };
    script_lower_bindings bindings = authoring_native_bindings(natives_catalog);
    sources = authoring_sources_prepare_catalog(
        source_catalog_replaced(root, native_capabilities_catalog(natives_catalog), edits, 2),
        &bindings,
        backend
    );
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *s = authoring_sources_at(sources, i);
        for (int j = 0; j < s->diagnostic_count; j++) {
            fprintf(stderr, "%s: %s\n", s->path, s->diagnostics[j].message);
        }
    }
    REQUIRE(authoring_sources_valid(sources));
    natives = execution_natives_new(RUNTIME);
    return true;
}
static bool prepare(behavior_backend backend)
{
    return prepare_library(LIBRARY, backend);
}
static uint64_t state_id(const char *name)
{
    const authoring_source *s = source("Probe");
    for (int i = 0; i < s->analysis->symbol_count; i++) {
        const script_symbol *symbol = &s->analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
/* The Probe's states after each Tick. */
typedef struct
{
    int32_t total;
    bool flag;
    uint8_t small;
    double wide;
    int32_t label;
} probe_states;
static bool states(const execution_instance *instance, probe_states *out)
{
    const pin_value *total = execution_instance_state(instance, state_id("total")),
                    *flag = execution_instance_state(instance, state_id("flag")),
                    *small = execution_instance_state(instance, state_id("small")),
                    *wide = execution_instance_state(instance, state_id("wide")),
                    *label = execution_instance_state(instance, state_id("label"));
    REQUIRE(total && total->type == PIN_I32 && flag && flag->type == PIN_BOOL);
    REQUIRE(small && small->type == PIN_U8 && wide && wide->type == PIN_F64);
    REQUIRE(label && label->type == PIN_I32);
    *out = (probe_states){ total->i, flag->b, small->u8, wide->d, label->i };
    return true;
}
static const float CELLS[TICKS] = { 1.5f, -2.0f, 0.0f, 1e30f };
/* Runs TICKS Ticks of a new Probe program, with the table's natives when
 * `bound`, and an inspector when `inspected`, recording the states after each
 * and the native entries the calls took. */
static bool run(bool bound, bool inspected, probe_states out[TICKS], uint64_t *entries)
{
    program = execution_program_new(source("Probe")->module, NULL, NULL);
    REQUIRE(program);
    if (bound) {
        execution_program_set_natives(program, natives);
    }
    execution_inspector *inspector = inspected ? execution_program_inspector(program)
                                               : NULL;
    execution_instance *instance = execution_program_attach(program, 1);
    REQUIRE(instance);
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    for (int i = 0; i < TICKS; i++) {
        const pin_value arg = { .type = PIN_F32, .f = CELLS[i] };
        int status = execution_instance_dispatch(instance, tick.id, &arg, 1, i, 0, 1000);
        if (status != FLOW_OK || !states(instance, &out[i])) {
            behavior_run_scope_end(&scope);
            REQUIRE(status == FLOW_OK);
            return false;
        }
    }
    behavior_run_scope_end(&scope);
    *entries = scope.counts.native_entries;
    execution_inspector_free(inspector);
    execution_program_free(program);
    program = NULL;
    return true;
}
static bool same_states(const probe_states *a, const probe_states *b)
{
    for (int i = 0; i < TICKS; i++) {
        REQUIRE(a[i].total == b[i].total && a[i].flag == b[i].flag && a[i].small == b[i].small);
        REQUIRE(a[i].label == b[i].label);
        REQUIRE(f64_bits(a[i].wide) == f64_bits(b[i].wide));
    }
    return true;
}

/* Whether the Probe, compiled with `library` on backend b, reaches the
 * same states with `manifest` bound as in the VM, every call running
 * natively; `vm` receives the VM's. */
static bool agrees(
    const char *library,
    const execution_native_manifest *manifest,
    int b,
    probe_states vm[TICKS]
)
{
    char error[256] = "";
    REQUIRE(behavior_backend_available(backends[b]));
    REQUIRE(prepare_library(library, backends[b]));
    bool bound = execution_natives_bind(natives, source("Native")->module, manifest, error, sizeof error);
    if (!bound) {
        fprintf(stderr, "native bind: %s\n", error);
    }
    REQUIRE(bound);
    probe_states native[TICKS];
    uint64_t vm_entries = 1, native_entries = 0;
    REQUIRE(run(false, false, vm, &vm_entries) && !vm_entries);
    REQUIRE(run(true, false, native, &native_entries));
    REQUIRE(native_entries == TICK_CALLS * TICKS);
    REQUIRE(same_states(vm, native));
    clear();
    return true;
}
bool native_calls_agree(const execution_native_manifest *manifest)
{
    setup();
    probe_states vm[TICKS];
    bool agreed = agrees(NATIVE_CALLS_LIBRARY, manifest, 0, vm) &&
                  agrees(NATIVE_CALLS_LIBRARY, manifest, 1, vm);
    cleanup();
    return agreed;
}

TEST_CASE(native_calls_match_the_vm)
{
    for (int b = 0; b < 2; b++) {
        probe_states vm[TICKS];
        calls = 0;
        REQUIRE(agrees(LIBRARY, &MANIFEST, b, vm));
        REQUIRE(calls == TICK_CALLS * TICKS);
        /* Reward's bonus defaults to 5, Positive is false for -2 and 0, Wrap
         * wraps its u8, and Mix takes either branch. */
        REQUIRE(vm[0].total == 11 && vm[0].flag && vm[0].small == 200 && vm[0].wide == 231.25);
        REQUIRE(vm[1].total == 51 && !vm[1].flag && vm[1].small == 144 && vm[1].wide == -53);
        REQUIRE(!vm[2].flag && vm[3].flag);
        /* Label formats each number as the VM does: "1.5 cells" to
         * "1e+30 cells". */
        REQUIRE(vm[0].label == 9 && vm[1].label == 8 && vm[2].label == 7 && vm[3].label == 11);
    }
    return true;
}

TEST_CASE(native_calls_leave_inspected_calls_in_the_vm)
{
    char error[256] = "";
    REQUIRE(prepare(TEST_COMPILED_BACKEND));
    REQUIRE(
        execution_natives_bind(natives, source("Native")->module, &MANIFEST, error, sizeof error)
    );
    probe_states vm[TICKS], inspected[TICKS];
    uint64_t vm_entries = 1, inspected_entries = 1;
    calls = 0;
    REQUIRE(run(false, false, vm, &vm_entries));
    REQUIRE(run(true, true, inspected, &inspected_entries));
    REQUIRE(!inspected_entries && !calls);
    REQUIRE(same_states(vm, inspected));
    clear();
    return true;
}

/* A binding that fails binds nothing, and its reason names the export. */
static bool rejects(const execution_native_manifest *manifest, const char *reason)
{
    char error[256] = "";
    REQUIRE(prepare(BEHAVIOR_BYTECODE));
    bool bound = execution_natives_bind(natives, source("Native")->module, manifest, error, sizeof error);
    if (bound || strcmp(error, reason)) {
        fprintf(stderr, "native bind: %s\n", error);
    }
    REQUIRE(!bound && !strcmp(error, reason));
    for (int i = 0; i < execution_module_function_count(source("Native")->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(source("Native")->module, i);
        REQUIRE(!execution_natives_find(natives, info.body));
    }
    clear();
    return true;
}
TEST_CASE(native_calls_reject_mismatched_exports)
{
    static const uint8_t BOOL_REWARD[] = { EXECUTION_NATIVE_INT,
                                           EXECUTION_NATIVE_BOOL,
                                           EXECUTION_NATIVE_INT };
    static const uint8_t I64_REWARD[] = { EXECUTION_NATIVE_INT,
                                          EXECUTION_NATIVE_INT,
                                          EXECUTION_NATIVE_I64 };
    static const uint8_t INT_INT[] = { EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT };
    static const uint8_t INT9_INT[EXECUTION_NATIVE_MAX_PARAMETERS + 2] = {
        EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT,
        EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT,
        EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT,
        EXECUTION_NATIVE_INT
    };
    /* The first export is good, so a failure after it must undo nothing. */
    const execution_native_export typed[] = { EXPORTS[0],
                                              exported("Positive", BOOL_REWARD, 2, reward) };
    const execution_native_export result[] = { exported("Reward", I64_REWARD, 2, reward) };
    const execution_native_export arity[] = { exported("Reward", INT_INT, 1, reward) };
    const execution_native_export missing[] = { exported("Absent", INT_INT, 1, reward) };
    const execution_native_export waits[] = { exported("Pause", INT_INT, 1, reward) };
    const execution_native_export twice[] = { EXPORTS[0], EXPORTS[1], EXPORTS[0] };
    const execution_native_export wide[] = { exported("Wide", INT9_INT, 9, reward) };
    /* A scoped pointer parameter (Bump) or a managed one (Read) passes an
     * address, not an int. */
    const execution_native_export lent[] = { exported("Bump", INT_INT, 1, reward) };
    const execution_native_export pointed[] = { exported("Read", INT_INT, 1, reward) };
    const execution_native_manifest manifests[] = {
        { EXECUTION_NATIVE_ABI + 1, 4, EXPORTS },
        { EXECUTION_NATIVE_ABI, 2, typed },
        { EXECUTION_NATIVE_ABI, 1, result },
        { EXECUTION_NATIVE_ABI, 1, arity },
        { EXECUTION_NATIVE_ABI, 1, missing },
        { EXECUTION_NATIVE_ABI, 1, waits },
        { EXECUTION_NATIVE_ABI, 3, twice },
        { EXECUTION_NATIVE_ABI, 1, wide },
        { EXECUTION_NATIVE_ABI, 1, lent },
        { EXECUTION_NATIVE_ABI, 1, pointed },
    };
    static const char *const reasons[] = {
        "native library ABI 3 differs from 2",
        "native export Positive has no pure function of its signature in the "
        "library",
        "native export Reward has no pure function of its signature in the "
        "library",
        "native export Reward has no pure function of its signature in the "
        "library",
        "native export Absent has no pure function of its signature in the "
        "library",
        "native export Pause has no pure function of its signature in the "
        "library",
        "native export Reward is exported twice",
        "native export Wide has more parameters or results than a native entry "
        "takes",
        "native export Bump has no pure function of its signature in the "
        "library",
        "native export Read has no pure function of its signature in the "
        "library",
    };
    for (int i = 0; i < (int)(sizeof manifests / sizeof *manifests); i++) {
        REQUIRE(rejects(&manifests[i], reasons[i]));
    }
    return true;
}

/* A reference has integer-width storage, but it is not a scalar value. An
 * unused reference is effect-free, so neither purity nor width rejects it. */
TEST_CASE(native_calls_reject_reference_exports)
{
    static const uint8_t types[] = { sizeof(uintptr_t) == sizeof(uint64_t)
                                         ? EXECUTION_NATIVE_U64
                                         : EXECUTION_NATIVE_U32,
                                     EXECUTION_NATIVE_INT };
    const pin_type storage = sizeof(uintptr_t) == sizeof(uint64_t) ? PIN_U64 : PIN_U32;
    const execution_native_export exports[] = { EXPORTS[0],
                                                exported("UnusedRef", types, 1, reward) };
    const execution_native_manifest manifest = { EXECUTION_NATIVE_ABI, 2, exports };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        const execution_module *library = source("Native")->module;
        const behavior_function *function = NULL;
        for (int i = 0; i < execution_module_function_count(library); i++) {
            execution_module_function_info info = execution_module_function_at(library, i);
            if (info.id == function_id("UnusedRef")) {
                REQUIRE(info.kind == EXECUTION_FUNCTION_AUTHORED && !info.effects);
                function = info.body;
            }
        }
        REQUIRE(function);
        int count;
        const pin_value *arguments = behavior_function_arguments(function, &count);
        REQUIRE(count == 1 && arguments[0].type == storage);
        REQUIRE(behavior_function_kind(function, BEHAVIOR_BANK_ARGUMENT, 0) == BEHAVIOR_REFERENCE);
        char error[256] = "";
        REQUIRE(!execution_natives_bind(natives, library, &manifest, error, sizeof error));
        REQUIRE(
            !strcmp(error, "native export UnusedRef has no pure function of its signature in the library")
        );
        /* The valid first export must not bind when the second fails. */
        for (int i = 0; i < execution_module_function_count(library); i++) {
            execution_module_function_info info = execution_module_function_at(library, i);
            REQUIRE(!execution_natives_find(natives, info.body));
        }
        clear();
    }
    return true;
}

/* A second library object cannot rebind a function, and the first binding
 * of each stays. */
TEST_CASE(native_calls_bind_each_function_once)
{
    char error[256] = "";
    REQUIRE(prepare(BEHAVIOR_BYTECODE));
    const execution_module *library = source("Native")->module;
    REQUIRE(execution_natives_bind(natives, library, &MANIFEST, error, sizeof error));
    const execution_native_manifest again = { EXECUTION_NATIVE_ABI, 1, &EXPORTS[2] };
    REQUIRE(!execution_natives_bind(natives, library, &again, error, sizeof error));
    REQUIRE(!strcmp(error, "native export Wrap is already bound"));
    int bound = 0;
    for (int i = 0; i < execution_module_function_count(library); i++) {
        execution_module_function_info info = execution_module_function_at(library, i);
        const execution_native_export *native = execution_natives_find(natives, info.body);
        for (int k = 0; k < MANIFEST.count; k++) {
            bound += EXPORTS[k].id == info.id && native == &EXPORTS[k];
        }
    }
    REQUIRE(bound == MANIFEST.count);
    clear();
    return true;
}

/* The Probe's states after each Tick, run as a player runs it: the library
 * and the Probe registered as sources, then `count` manifests bound through
 * the registry, which must give `reason` when it is not NULL. */
static bool registered(
    const execution_native_manifest *const *manifests,
    int count,
    const char *reason,
    probe_states out[TICKS]
)
{
    char error[256] = "";
    ecs_world *ecs = ecs_world_new();
    source_registry *registry = source_registry_new(ecs);
    const source_publication entries[] = {
        { .name = "Native", .module = source("Native")->module },
        { .name = "Probe", .module = source("Probe")->module },
    };
    REQUIRE(source_publish(registry, entries, 2, NULL, error, sizeof error) == FLOW_OK);
    int bound = source_bind_natives(registry, RUNTIME, manifests, count, error, sizeof error);
    if (reason ? bound == 0 || strcmp(error, reason) : bound != 0) {
        fprintf(stderr, "registry natives: %s\n", error);
    }
    REQUIRE(reason ? bound < 0 && !strcmp(error, reason) : bound == 0);
    ecs_entity entity = ecs_world_create_entity(ecs);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    REQUIRE(source_attach(registry, entity, "Probe", false, 0, 0) == FLOW_OK);
    const execution_instance *instance = execution_program_instance(
        source_program(registry, "Probe"),
        ecs_entity_to_u64(entity)
    );
    for (int i = 0; i < TICKS; i++) {
        const pin_value arg = { .type = PIN_F32, .f = CELLS[i] };
        REQUIRE(source_dispatch(registry, entity, "Tick", &arg, 1, i, 0) >= 0);
        REQUIRE(source_step(registry, i, 0) == FLOW_OK && states(instance, &out[i]));
    }
    source_registry_free(registry);
    ecs_world_free(ecs);
    return true;
}
/* A host binds manifests through the registry, as the player does: each
 * binds to the registered module that defines its functions, and the
 * programs then call its entries. A manifest that no module defines, or one
 * that does not bind, fails and leaves every call in the VM; one of another
 * ABI is not read past its version. */
TEST_CASE(native_calls_bind_through_the_registry)
{
    static const uint8_t INT_INT[] = { EXECUTION_NATIVE_INT, EXECUTION_NATIVE_INT };
    const execution_native_export absent[] = { exported("Absent", INT_INT, 1, reward) };
    const execution_native_export twice[] = { EXPORTS[0], EXPORTS[0] };
    const execution_native_manifest elsewhere = { EXECUTION_NATIVE_ABI, 1, absent },
                                    unbound = { EXECUTION_NATIVE_ABI, 2, twice },
                                    empty = { EXECUTION_NATIVE_ABI, 0, NULL },
                                    newer = { EXECUTION_NATIVE_ABI + 1, 1, NULL };
    const execution_native_manifest *const whole[] = { &empty, &MANIFEST };
    const execution_native_manifest *const missing[] = { &MANIFEST, &elsewhere };
    const execution_native_manifest *const failing[] = { &unbound };
    const execution_native_manifest *const other[] = { &newer };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b]));
        probe_states vm[TICKS], native[TICKS], rejected[TICKS];
        calls = 0;
        REQUIRE(registered(NULL, 0, NULL, vm) && !calls);
        REQUIRE(registered(whole, 2, NULL, native) && calls == TICK_CALLS * TICKS);
        REQUIRE(same_states(vm, native));
        calls = 0;
        REQUIRE(registered(missing, 2, "native export Absent has no source module", rejected));
        REQUIRE(registered(failing, 1, "native export Reward is exported twice", rejected));
        REQUIRE(registered(other, 1, "native library ABI 3 differs from 2", rejected));
        REQUIRE(!calls && same_states(vm, rejected));
        clear();
    }
    return true;
}

/* A context formats native values as the VM formats the pin_values they
 * stand for: each scalar type from its word, and text, with an embedded NUL,
 * from its bytes. What it gives owns its bytes through the frame's heap,
 * which free releases. */
TEST_CASE(native_calls_format_text_like_the_vm)
{
    static const char
        FORMAT[] = "{} {} {} {} {} {} {} {} {} {} {:.2} [{:4}] {}";
    float number = 2.5f;
    uint32_t number_bits;
    memcpy(&number_bits, &number, sizeof number_bits);
    const execution_native_value values[] = {
        { EXECUTION_NATIVE_TEXT, sizeof FORMAT - 1, 0, FORMAT },
        { EXECUTION_NATIVE_INT, 0, (uint64_t)(int64_t)-7, NULL },
        { EXECUTION_NATIVE_BOOL, 0, 1, NULL },
        { EXECUTION_NATIVE_I8, 0, (uint64_t)(int64_t)-128, NULL },
        { EXECUTION_NATIVE_U8, 0, 255, NULL },
        { EXECUTION_NATIVE_I16, 0, (uint64_t)(int64_t)-300, NULL },
        { EXECUTION_NATIVE_U16, 0, 65535, NULL },
        { EXECUTION_NATIVE_U32, 0, UINT32_MAX, NULL },
        { EXECUTION_NATIVE_I64, 0, (uint64_t)INT64_MIN, NULL },
        { EXECUTION_NATIVE_U64, 0, UINT64_MAX, NULL },
        { EXECUTION_NATIVE_NUMBER, 0, number_bits, NULL },
        { EXECUTION_NATIVE_F64, 0, f64_bits(-0.125), NULL },
        { EXECUTION_NATIVE_TEXT, 3, 0, "a\0b" },
        { EXECUTION_NATIVE_TEXT, 0, 0, NULL },
    };
    enum
    {
        COUNT = sizeof values / sizeof *values
    };
    pin_string format = { FORMAT, sizeof FORMAT - 1 }, nul = { "a\0b", 3 },
               empty = { 0 };
    const pin_value inputs[COUNT] = {
        { .type = PIN_STR, .text = &format },
        { .type = PIN_I32, .i = -7 },
        { .type = PIN_BOOL, .b = true },
        { .type = PIN_I8, .i8 = -128 },
        { .type = PIN_U8, .u8 = 255 },
        { .type = PIN_I16, .i16 = -300 },
        { .type = PIN_U16, .u16 = 65535 },
        { .type = PIN_U32, .u = UINT32_MAX },
        { .type = PIN_I64, .i64 = INT64_MIN },
        { .type = PIN_U64, .u64 = UINT64_MAX },
        { .type = PIN_F32, .f = 2.5f },
        { .type = PIN_F64, .d = -0.125 },
        { .type = PIN_STR, .text = &nul },
        { .type = PIN_STR, .text = &empty },
    };
    arena storage = { 0 };
    pin_string expected = pin_text(native_format_text(inputs, COUNT, &storage));
    execution_native_frame frame;
    execution_native_frame_init(&frame, RUNTIME);
    pin_string text;
    frame.context.format_text(&frame.context, values, COUNT, &text);
    bool same = pin_text_equal(text, expected) && text.owner;
    execution_native_frame_free(&frame);
    arena_free(&storage);
    REQUIRE(same && expected.length > sizeof FORMAT);
    return true;
}

TEST_SUITE(native_calls)
{
    RUN_TEST_CASE(native_calls_format_text_like_the_vm);
    RUN_TEST_CASE(native_calls_match_the_vm);
    RUN_TEST_CASE(native_calls_leave_inspected_calls_in_the_vm);
    RUN_TEST_CASE(native_calls_reject_mismatched_exports);
    RUN_TEST_CASE(native_calls_reject_reference_exports);
    RUN_TEST_CASE(native_calls_bind_each_function_once);
    RUN_TEST_CASE(native_calls_bind_through_the_registry);
}

void test_language_native_calls()
{
    pu_display_colors(true);
    setup();
    RUN_TEST_SUITE(native_calls);
    cleanup();
    pu_print_stats();
}
