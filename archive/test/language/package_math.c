#include "authoring/native_bindings.h"
#include "authoring/package_tests.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/core_definition.h"
#include "runtime/math_definition.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CHIBI_SOURCE_ROOT
#define CHIBI_SOURCE_ROOT "."
#endif
#ifndef CHIBI_CORE_MATH_ROOT
#define CHIBI_CORE_MATH_ROOT CHIBI_SOURCE_ROOT "/packages/core/math"
#endif

/* Read the source being shipped, including when an installed-SDK run overrides
 * its directory. The C cases cover IEEE values and native/compiler boundaries;
 * the package's own @tests own ordinary mathematical behavior. */
static const char *const source_names[] = {
    "Math.chibi",        "Spring.chibi",    "MathTests.chibi",
    "SpringTests.chibi", "ease/Ease.chibi", "ease/EaseTests.chibi"
};
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static char *source_text[6];
static authoring_packages *packages;
static native_capabilities *natives;
static package_test *tests;
static const behavior_function *active_function;
static behavior_function_activation activation;
static behavior_function_context context;
static behavior_managed_heap managed;
static arena storage;
static pin_value output;
static char failure[256];
static double preparation_seconds;

static const char APP[] =
    "package app\nimport math \"core:math\"\nimport ease \"core:math/ease\"\n"
    "pub Inverse :: (a: number, b: number, value: number) -> number { "
    " return math.InverseLerp(a, b, value); }\n"
    "pub Interpolate :: (a: number, b: number, value: number) -> number { "
    " return math.Lerp(a, b, value); }\n"
    "pub Threshold :: (value: number, edge: number) -> number { "
    " return math.Step(value, edge); }\n"
    "pub Finite :: (value: number) -> bool { return math.IsFinite(value); }\n"
    "pub Smooth :: (value: number) -> number { return math.SmoothStep(value); "
    "}\n"
    "pub Quantize :: (value: number, step: number) -> number { "
    " return math.QuantizeAngle(value, step); }\n"
    "pub Angle :: (a: number, b: number, fraction: number) -> number { "
    " return math.AngleLerp(a, b, fraction); }\n"
    "pub Follow :: (value: number, target: number, rate: number, elapsed: "
    "number) -> number { "
    " return math.Damp(value, target, rate, elapsed); }\n"
    "pub SpringField :: (field: int, position: number, velocity: number, "
    "target: number, "
    " frequency: number, damping: number, elapsed: number) -> number { "
    " spring: math.Spring = {position: position, velocity: velocity}; "
    " next := math.AdvanceSpring(spring, target, frequency, damping, elapsed); "
    " if field == 0 { return next.position; } return next.velocity; }\n"
    "pub Ease :: (value: number) -> number { return ease.Linear(value); }\n"
    "pub ImportedLerp :: (count: int) -> number { value: number = 0; index: "
    "int = 0; "
    " while index < count { value = math.Lerp(value, 8, 0.001); index += 1; } "
    "return value; }\n"
    "pub InlineLerp :: (count: int) -> number { value: number = 0; index: int "
    "= 0; "
    " while index < count { span := 8 - value; offset := span * 0.001; "
    " value = value + offset; index += 1; } return value; }\n"
    "pub SpringValue :: (count: int) -> number { "
    " spring: math.Spring = {position: 1, velocity: 0}; index: int = 0; "
    " while index < count { spring = math.AdvanceSpring(spring, 0, 1, 1, 1.0 / "
    "120); "
    " index += 1; } return spring.position + spring.velocity; }\n"
    "pub SpringFields :: (count: int) -> number { "
    " position: number = 1; velocity: number = 0; index: int = 0; "
    " while index < count { spring: math.Spring = {position: position, "
    "velocity: velocity}; "
    " next := math.AdvanceSpring(spring, 0, 1, 1, 1.0 / 120); "
    " position = next.position; velocity = next.velocity; index += 1; } "
    " return position + velocity; }\n";

static double now()
{
    struct timespec value;
    int status = clock_gettime(CLOCK_MONOTONIC, &value);
    assert(!status);
    return value.tv_sec + value.tv_nsec * 1e-9;
}
static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    active_function = NULL;
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    managed = (behavior_managed_heap){ 0 };
}
static void cleanup()
{
    finish_call();
    afree(tests);
    tests = NULL;
    authoring_packages_free(packages);
    packages = NULL;
    native_capabilities_free(natives);
    natives = NULL;
    for (int i = 0; i < 6; i++) {
        free(source_text[i]);
        source_text[i] = NULL;
    }
}
static char *read_source(const char *relative)
{
    const char *directory = getenv("CHIBI_CORE_MATH_ROOT");
    if (!directory || !directory[0]) {
        directory = CHIBI_CORE_MATH_ROOT;
    }
    char path[4096];
    int length = snprintf(path, sizeof path, "%s/%s", directory, relative);
    if (length < 0 || (size_t)length >= sizeof path) {
        return NULL;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "package math cannot read %s\n", path);
        return NULL;
    }
    bool ok = !fseek(file, 0, SEEK_END);
    long size = ok ? ftell(file) : -1;
    ok = size >= 0 && size <= 512 * 1024 && !fseek(file, 0, SEEK_SET);
    char *text = ok ? xmalloc((size_t)size + 1) : NULL;
    if (ok) {
        ok = fread(text, 1, (size_t)size, file) == (size_t)size;
        text[size] = 0;
    }
    fclose(file);
    if (!ok) {
        fprintf(stderr, "package math cannot read source bytes from %s\n", path);
        free(text);
        return NULL;
    }
    return text;
}
static void diagnostics()
{
    const char *error = authoring_packages_error(packages);
    if (error) {
        fprintf(stderr, "package math: %s\n", error);
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *item = authoring_packages_at(packages, i);
        for (int j = 0; j < item->diagnostic_count; j++) {
            const script_diagnostic *d = &item->diagnostics[j];
            fprintf(
                stderr,
                "%s:%d:%d: %s\n",
                item->identity,
                d->range.line,
                d->range.column,
                d->message
            );
        }
    }
}
static bool prepare(const char *app, behavior_backend backend, bool valid)
{
    cleanup();
    double start = now();
    native_definition_pack packs[] = { CORE_DEFINITION_PACK,
                                       NATIVE_MATH_DEFINITION_PACK };
    char error[256];
    natives = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    if (!natives) {
        fprintf(stderr, "package math native catalog: %s\n", error);
    }
    REQUIRE(natives);
    source_package_input files[7];
    char paths[6][128];
    for (int i = 0; i < 6; i++) {
        source_text[i] = read_source(source_names[i]);
        REQUIRE(source_text[i]);
        int length = snprintf(
            paths[i],
            sizeof paths[i],
            "%s/math/%s",
            app ? "/math-sdk/core" : "/math-tests",
            source_names[i]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof paths[i]);
        files[i] = (source_package_input){ paths[i],
                                           source_text[i],
                                           strlen(source_text[i]) };
    }
    files[6] = (source_package_input){ "app/App.chibi", app, app ? strlen(app) : 0 };
    source_package_collection core = { "core", "/math-sdk/core" };
    source_package_snapshot_options options = {
        .project_root = app ? "/math-consumer" : "/math-tests",
        .files = files,
        .file_count = app ? 7 : 6,
        .collections = app ? &core : NULL,
        .collection_count = app ? 1 : 0,
    };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    script_lower_bindings bindings = authoring_native_bindings(natives);
    packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(natives),
        &bindings,
        backend,
        NULL
    );
    preparation_seconds = now() - start;
    REQUIRE(packages);
    if (valid && !authoring_packages_valid(packages)) {
        diagnostics();
    }
    REQUIRE(authoring_packages_valid(packages) == valid);
    return true;
}
static const authoring_package *package(const char *identity)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *item = authoring_packages_at(packages, i);
        if (!strcmp(item->identity, identity)) {
            return item;
        }
    }
    return NULL;
}
static const behavior_function *function(const char *name)
{
    const authoring_package *app = package("project:app");
    uint64_t id = 0;
    for (int i = 0; app && i < app->exports->member_count; i++) {
        const script_package_export *member = &app->exports->members[i];
        if (!strcmp(member->name, name)) {
            id = member->id;
        }
    }
    for (int i = 0; app && id && i < execution_module_function_count(app->module); i++) {
        execution_module_function_info info = execution_module_function_at(app->module, i);
        if (info.id == id) {
            return info.body;
        }
    }
    return NULL;
}
static void observe(const behavior_function *, const behavior_function_context *call, int)
{
    if (!failure[0]) {
        snprintf(
            failure,
            sizeof failure,
            "%s",
            call->error_message ? call->error_message
                                : behavior_function_failure_text(call->failure)
        );
    }
}
static bool bind_call(
    const behavior_function *body,
    const pin_value *arguments,
    int count,
    pin_type type,
    uintptr_t budget
)
{
    REQUIRE(body);
    if (active_function != body) {
        finish_call();
        active_function = body;
        behavior_managed_heap_init(&managed);
        if (behavior_function_resumable(body)) {
            behavior_function_activation_init(body, &activation);
        }
    } else {
        behavior_function_reset_host(&context);
        behavior_function_reset_activation(body, &activation);
        arena_reset(&storage);
    }
    failure[0] = 0;
    output = (pin_value){ .type = type };
    context.activation = behavior_function_resumable(body) ? &activation : NULL;
    context.managed_heap = &managed;
    context.arguments = arguments;
    context.argument_count = count;
    context.results = &output;
    context.budget = budget;
    context.failure_observer = observe;
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(body, &context, NULL, 0));
    behavior_function_prepare_host(body, &context);
    return true;
}
static bool call(
    const char *name,
    const pin_value *arguments,
    int count,
    pin_type type,
    behavior_backend backend,
    const char *rejected
)
{
    const behavior_function *body = function(name);
    /* Failed invocations do not qualify for successful-activation reuse. */
    finish_call();
    REQUIRE(bind_call(body, arguments, count, type, 100000));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(body, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    if (rejected) {
        REQUIRE(status == FLOW_ERR_INVALID_GRAPH);
        REQUIRE(strstr(failure, "Assertion failed: ") && strstr(failure, rejected));
        return true;
    }
    if (status != FLOW_OK) {
        fprintf(stderr, "package math %s status=%d: %s\n", name, status, failure);
    }
    REQUIRE(status == FLOW_OK && context.returned && output.type == type);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? runs.counts.bytecode_entries > 0
                                     : !runs.counts.bytecode_entries
    );
    REQUIRE(backend != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    return true;
}
static pin_value real(float value)
{
    return (pin_value){ .type = PIN_F32, .f = value };
}
static bool same(float actual, float expected)
{
    REQUIRE(isnan(expected) ? isnan(actual) : actual == expected);
    REQUIRE(expected != 0 || !!signbit(actual) == !!signbit(expected));
    return true;
}
TEST_CASE(package_math_runs_shipped_authored_tests)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(NULL, backends[b], true));
        tests = package_tests_find(packages);
        REQUIRE(acount(tests) > 0);
        bool math = false, ease = false;
        for (int i = 0; i < acount(tests); i++) {
            math |= !strcmp(tests[i].package, "math");
            ease |= !strcmp(tests[i].package, "ease");
            package_test_failure reason;
            bool passed = package_test_run(&tests[i], backends[b], PACKAGE_TEST_BUDGET, &reason);
            if (!passed) {
                fprintf(
                    stderr,
                    "%s:%d:%d %s.%s: %s\n",
                    reason.path ? reason.path : tests[i].path,
                    reason.line,
                    reason.column,
                    tests[i].package,
                    tests[i].name,
                    reason.message
                );
            }
            REQUIRE(passed);
        }
        REQUIRE(math && ease);
        printf(
            "MATH_TESTS backend=%s tests=%d prepare_ms=%.3f\n",
            behavior_backend_name(backends[b]),
            acount(tests),
            preparation_seconds * 1e3
        );
    }
    return true;
}
TEST_CASE(package_math_imports_preserve_ieee_and_number_precision)
{
    static const struct
    {
        const char *name;
        float a, b, c, expected;
    } cases[] = {
        { "Inverse", 2, 6, 3, .25f },
        { "Inverse", 6, 2, 3, .75f },
        { "Inverse", 3, 3, 3, NAN },
        { "Inverse", 3, 3, 4, INFINITY },
        { "Inverse", 3, 3, 2, -INFINITY },
        { "Inverse", 0, 1, -0.0f, -0.0f },
        { "Inverse", 1, 0, 1, -0.0f },
        { "Inverse", -FLT_MAX, FLT_MAX, 0, 0 },
        { "Inverse", -FLT_MAX, FLT_MAX, FLT_MAX, NAN },
        { "Inverse", -1, 16777216, 16777215, 1 },
        { "Inverse", NAN, 1, 0, NAN },
        { "Inverse", 0, INFINITY, 1, 0 },
        { "Interpolate", 2, 6, 1.5f, 8 },
        { "Interpolate", 2, 6, -.5f, 0 },
        { "Interpolate", -1, 16777216, 1, 16777215 },
        { "Interpolate", 16777216, 16777218, .5f, 16777216 },
        { "Interpolate", -0.0f, -1, 0, -0.0f },
        { "Interpolate", -FLT_MAX, FLT_MAX, 0, NAN },
        { "Interpolate", -FLT_MAX, FLT_MAX, 1, INFINITY },
        { "Interpolate", 0, 1, NAN, NAN },
        { "Interpolate", INFINITY, INFINITY, 0, NAN },
    };
    static const float special[] = { -0.0f,         0,        FLT_TRUE_MIN,
                                     -FLT_TRUE_MIN, -FLT_MAX, FLT_MAX,
                                     NAN,           INFINITY, -INFINITY };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(APP, backends[b], true));
        REQUIRE(package("core:math") && package("core:math/ease"));
        for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
            pin_value args[] = { real(cases[i].a),
                                 real(cases[i].b),
                                 real(cases[i].c) };
            REQUIRE(call(cases[i].name, args, 3, PIN_F32, backends[b], NULL));
            REQUIRE(same(output.f, cases[i].expected));
        }
        for (int i = 0; i < (int)(sizeof special / sizeof *special); i++) {
            pin_value value = real(special[i]);
            REQUIRE(call("Finite", &value, 1, PIN_BOOL, backends[b], NULL));
            REQUIRE(output.b == !!isfinite(special[i]));
            REQUIRE(call("Ease", &value, 1, PIN_F32, backends[b], NULL));
            REQUIRE(same(output.f, special[i]));
            pin_value args[] = { value, real(0) };
            REQUIRE(call("Threshold", args, 2, PIN_F32, backends[b], NULL));
            REQUIRE(output.f == (special[i] < 0 ? 0 : 1));
        }
    }
    return true;
}
static bool rejected_inputs(
    const char *name,
    const float *values,
    int count,
    int first,
    int end,
    const char *message,
    behavior_backend backend
)
{
    const float invalid[] = { NAN, INFINITY, -INFINITY };
    for (int input = first; input < end; input++) {
        for (int sample = 0; sample < 3; sample++) {
            pin_value args[7];
            REQUIRE(count <= 7);
            for (int i = 0; i < count; i++) {
                args[i] = real(values[i]);
            }
            if (first) {
                args[0] = (pin_value){ .type = PIN_I32, .i = 0 };
            }
            args[input] = real(invalid[sample]);
            REQUIRE(call(name, args, count, PIN_F32, backend, message));
        }
    }
    return true;
}
TEST_CASE(package_math_imports_reject_invalid_domains)
{
    const float smooth[] = { .5f }, quantize[] = { 1, .5f },
                angle[] = { 0, 1, .5f }, damp[] = { 0, 1, 1, .125f },
                spring[] = { 0, 1, 0, 0, 1, 1, .125f };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(APP, backends[b], true));
        REQUIRE(rejected_inputs("Smooth", smooth, 1, 0, 1, "SmoothStep needs", backends[b]));
        REQUIRE(
            rejected_inputs("Quantize", quantize, 2, 0, 2, "QuantizeAngle needs", backends[b])
        );
        REQUIRE(rejected_inputs("Angle", angle, 3, 0, 3, "AngleLerp needs", backends[b]));
        REQUIRE(rejected_inputs("Follow", damp, 4, 0, 4, "Damp needs", backends[b]));
        REQUIRE(
            rejected_inputs("SpringField", spring, 7, 1, 7, "AdvanceSpring needs", backends[b])
        );
        pin_value zero_step[] = { real(1), real(0) };
        REQUIRE(call("Quantize", zero_step, 2, PIN_F32, backends[b], "QuantizeAngle needs"));
        zero_step[1] = real(-0.0f);
        REQUIRE(call("Quantize", zero_step, 2, PIN_F32, backends[b], "QuantizeAngle needs"));
        pin_value overflow_quantize[] = { real(FLT_MAX), real(FLT_MIN) };
        REQUIRE(call("Quantize", overflow_quantize, 2, PIN_F32, backends[b], "QuantizeAngle produced"));
        pin_value overflow_angle[] = { real(0), real(2), real(FLT_MAX) };
        REQUIRE(call("Angle", overflow_angle, 3, PIN_F32, backends[b], "AngleLerp produced"));
        pin_value overflow_spring[] = { { .type = PIN_I32, .i = 0 },
                                        real(-FLT_MAX),
                                        real(0),
                                        real(FLT_MAX),
                                        real(1),
                                        real(0),
                                        real(.25f) };
        REQUIRE(call("SpringField", overflow_spring, 7, PIN_F32, backends[b], "AdvanceSpring produced"));
    }
    return true;
}
TEST_CASE(package_math_imports_keep_identity_and_extreme_finite_state)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(APP, backends[b], true));
        pin_value args[] = { { .type = PIN_I32, .i = 0 },
                             real(-0.0f),
                             real(-FLT_TRUE_MIN),
                             real(FLT_MAX),
                             real(FLT_MAX),
                             real(FLT_MAX),
                             real(0) };
        REQUIRE(call("SpringField", args, 7, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, -0.0f));
        args[0].i = 1;
        REQUIRE(call("SpringField", args, 7, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, -FLT_TRUE_MIN));
        pin_value equilibrium[] = { { .type = PIN_I32, .i = 0 },
                                    real(FLT_MAX),
                                    real(-0.0f),
                                    real(FLT_MAX),
                                    real(FLT_MAX),
                                    real(FLT_MAX),
                                    real(FLT_MAX) };
        REQUIRE(call("SpringField", equilibrium, 7, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, FLT_MAX));
        equilibrium[0].i = 1;
        REQUIRE(call("SpringField", equilibrium, 7, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, -0.0f));
        pin_value damp[] = { real(-FLT_MAX), real(FLT_MAX), real(FLT_MAX), real(FLT_MAX) };
        REQUIRE(call("Follow", damp, 4, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, FLT_MAX));
        damp[0] = real(-0.0f);
        damp[3] = real(0);
        REQUIRE(call("Follow", damp, 4, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, -0.0f));
        pin_value angle[] = { real(-0.0f), real(FLT_MAX), real(0) };
        REQUIRE(call("Angle", angle, 3, PIN_F32, backends[b], NULL));
        REQUIRE(same(output.f, -0.0f));
    }
    return true;
}
TEST_CASE(package_math_number_api_requires_explicit_f64_narrowing)
{
    static const char *const names[] = { "InverseLerp", "Lerp" };
    for (int operation = 0; operation < 2; operation++) {
        char rejected[256], admitted[256];
        snprintf(
            rejected,
            sizeof rejected,
            "package app\nimport math \"core:math\"\n"
            "pub Narrow :: (value: f64) -> number { return math.%s(value, 1, "
            "0.5); }\n",
            names[operation]
        );
        snprintf(
            admitted,
            sizeof admitted,
            "package app\nimport math \"core:math\"\n"
            "pub Narrow :: (value: f64) -> number { return "
            "math.%s(number(value), 1, 0.5); }\n",
            names[operation]
        );
        REQUIRE(prepare(rejected, BEHAVIOR_BYTECODE, false));
        const authoring_package *app = package("project:app");
        REQUIRE(app && !app->valid && app->diagnostic_count > 0);
        bool mismatch = false;
        for (int i = 0; i < app->diagnostic_count; i++) {
            const script_diagnostic *d = &app->diagnostics[i];
            mismatch |= strstr(d->message, "f64") && strstr(d->message, "number") &&
                        strstr(d->message, "convert with") && d->range.line == 3;
        }
        if (!mismatch) {
            diagnostics();
        }
        REQUIRE(mismatch);
        for (int b = 0; b < 2; b++) {
            REQUIRE(prepare(admitted, backends[b], true));
            pin_value value = { .type = PIN_F64, .d = 2.5 };
            REQUIRE(call("Narrow", &value, 1, PIN_F32, backends[b], NULL));
            REQUIRE(same(output.f, operation ? 1.75f : 4.f / 3));
            if (operation) {
                value.d = 16777217;
                REQUIRE(call("Narrow", &value, 1, PIN_F32, backends[b], NULL));
                REQUIRE(same(output.f, 8388608));
            }
        }
    }
    return true;
}
TEST_CASE(package_math_catalog_retires_high_level_native_names)
{
    REQUIRE(prepare(APP, BEHAVIOR_BYTECODE, true));
    static const char *const removed[] = { "Step",
                                           "Step Float",
                                           "Mix",
                                           "Mix Float",
                                           "Smooth Step",
                                           "Smooth Step Float",
                                           "Quantize Angle",
                                           "Quantize Angle Float",
                                           "Advance Damped Spring",
                                           "step",
                                           "mix",
                                           "smooth_step",
                                           "quantize_angle",
                                           "advance_damped_spring" };
    const script_catalog *catalog = native_capabilities_catalog(natives);
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *capability = &catalog->capabilities[i];
        for (int j = 0; j < (int)(sizeof removed / sizeof *removed); j++) {
            REQUIRE(strcmp(capability->name, removed[j]));
            REQUIRE(
                !capability->qualified_name ||
                strcmp(capability->qualified_name, removed[j])
            );
        }
    }
    static const char *const kept[] = { "Sine",
                                        "Sine Float",
                                        "Square Root",
                                        "Square Root Float",
                                        "Exponential",
                                        "Exponential Float",
                                        "Power",
                                        "Power Float",
                                        "Random Real",
                                        "Random Range",
                                        "Random Range Float" };
    for (int i = 0; i < (int)(sizeof kept / sizeof *kept); i++) {
        REQUIRE(script_catalog_find_capability(catalog, kept[i], SCRIPT_CAPABILITY_FUNCTION) >= 0);
    }
    return true;
}

static bool benchmark_setting(const char *name, int fallback, int maximum, int *out)
{
    const char *text = getenv(name);
    if (!text || !text[0]) {
        *out = fallback;
        return true;
    }
    char *end;
    long value = strtol(text, &end, 10);
    REQUIRE(!*end && value > 0 && value <= maximum);
    *out = (int)value;
    return true;
}
static bool measure(
    const char *name,
    behavior_backend backend,
    int loops,
    int invocations,
    int trials,
    float *value
)
{
    finish_call();
    const behavior_function *body = function(name);
    pin_value count = { .type = PIN_I32, .i = loops };
    uintptr_t budget = (uintptr_t)loops * 5000 + 100000;
    REQUIRE(bind_call(body, &count, 1, PIN_F32, budget));
    REQUIRE(behavior_function_run(body, &context, backend != BEHAVIOR_BYTECODE) == FLOW_OK);
    REQUIRE(context.returned && isfinite(output.f));
    float expected = output.f;
    for (int trial = 0; trial < trials; trial++) {
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        uint64_t blocks = arena_block_allocations();
        double start = now();
        for (int i = 0; i < invocations; i++) {
            REQUIRE(bind_call(body, &count, 1, PIN_F32, budget));
            REQUIRE(behavior_function_run(body, &context, backend != BEHAVIOR_BYTECODE) == FLOW_OK);
            REQUIRE(context.returned && output.f == expected);
        }
        double elapsed = now() - start;
        xalloc_stats_get(&after);
        blocks = arena_block_allocations() - blocks;
        size_t used, capacity;
        arena_usage(&storage, &used, &capacity);
        printf(
            "MATH_BENCH backend=%s body=%s trial=%d loops=%d invocations=%d "
            "ns_per_step=%.3f us_per_invocation=%.3f xalloc_calls=%llu "
            "requested_bytes=%llu "
            "arena_blocks=%llu arena_used=%zu arena_capacity=%zu "
            "activation_bytes=%zu "
            "host_bytes=%zu managed_bytes=%zu\n",
            behavior_backend_name(backend),
            name,
            trial,
            loops,
            invocations,
            elapsed * 1e9 / ((double)loops * invocations),
            elapsed * 1e6 / invocations,
            (unsigned long long)(after.calls - before.calls),
            (unsigned long long)(after.requested_bytes - before.requested_bytes),
            (unsigned long long)blocks,
            used,
            capacity,
            behavior_function_activation_storage_bytes(body, &activation),
            behavior_function_host_storage_bytes(&context),
            arena_storage_bytes(&managed.storage)
        );
    }
    *value = expected;
    return true;
}
TEST_CASE(package_math_invocation_and_record_value_benchmark)
{
    int loops, invocations, trials;
    REQUIRE(benchmark_setting("CHIBI_MATH_BENCH_LOOPS", 128, 100000, &loops));
    REQUIRE(benchmark_setting("CHIBI_MATH_BENCH_INVOCATIONS", 16, 1000, &invocations));
    REQUIRE(benchmark_setting("CHIBI_MATH_BENCH_TRIALS", 3, 20, &trials));
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(APP, backends[b], true));
        printf(
            "MATH_PREPARE backend=%s milliseconds=%.3f source_files=6 "
            "invocation_includes=reusable_storage_reset_and_binding "
            "allocation_scope=xalloc_wrappers_and_arena_blocks\n",
            behavior_backend_name(backends[b]),
            preparation_seconds * 1e3
        );
        float imported, inline_value, record, fields;
        REQUIRE(measure("ImportedLerp", backends[b], loops, invocations, trials, &imported));
        REQUIRE(measure("InlineLerp", backends[b], loops, invocations, trials, &inline_value));
        REQUIRE(same(imported, inline_value));
        double ideal_lerp = 8 * (1 - pow(1 - (double).001f, loops));
        REQUIRE(fabs(imported - ideal_lerp) <= .001);
        REQUIRE(measure("SpringValue", backends[b], loops, invocations, trials, &record));
        REQUIRE(measure("SpringFields", backends[b], loops, invocations, trials, &fields));
        REQUIRE(same(record, fields));
        double omega = 6.283185307179586476925286766559;
        double time = loops * (double)(1.f / 120);
        double ideal_spring = exp(-omega * time) *
                              (1 + omega * time - omega * omega * time);
        REQUIRE(fabs(record - ideal_spring) <= .00001);
    }
    return true;
}
void test_language_package_math()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_math_runs_shipped_authored_tests);
    RUN_TEST_CASE(package_math_imports_preserve_ieee_and_number_precision);
    RUN_TEST_CASE(package_math_imports_reject_invalid_domains);
    RUN_TEST_CASE(package_math_imports_keep_identity_and_extreme_finite_state);
    RUN_TEST_CASE(package_math_number_api_requires_explicit_f64_narrowing);
    RUN_TEST_CASE(package_math_catalog_retires_high_level_native_names);
    pu_clear_setup();
}
void test_language_package_math_benchmark()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_math_invocation_and_record_value_benchmark);
    pu_clear_setup();
}
