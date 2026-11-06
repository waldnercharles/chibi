#include "assets/project.h"
#include "assets/project_migrate.h"
#include "backend.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"
#include "types/value_document.h"
#include "types/value_wire.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static ecs_world *world;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_managed_heap heap;
static behavior_run_scope runs;
static arena storage;
static pin_value result;
static const script_catalog_parameter assert_parameters[] = {
    { .name = "Condition", .type = { .storage = PIN_BOOL }, .has_default = true },
    { .name = "Message", .type = { .storage = PIN_STR }, .has_default = true },
};
static const script_catalog_capability capabilities[] = {
    { .id = 10, .name = "Run", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = 11,
      .name = "assert",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = assert_parameters,
      .parameter_count = 2,
      .pure = true },
};
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = 2 };

static void cleanup()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&heap);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&storage);
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    syntax = NULL;
    analysis = NULL;
    module = NULL;
    program = NULL;
    world = NULL;
}
static bool analyze(const char *source)
{
    cleanup();
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "generic %d:%d: %s\n",
            analysis->diagnostics[i].range.line,
            analysis->diagnostics[i].range.column,
            analysis->diagnostics[i].message
        );
    }
    return analysis->valid;
}
static bool compile(const char *source, behavior_backend backend)
{
    if (!analyze(source)) {
        return false;
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "generic lower: %s\n", plan->diagnostics[i].message);
    }
    char error[192];
    module = plan->valid
               ? execution_module_finish(&plan, backend, error, sizeof error)
               : NULL;
    execution_module_plan_free(plan);
    if (!module) {
        return false;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    return program != NULL;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, name)) {
            return analysis->symbols[i].id;
        }
    }
    return 0;
}
static execution_instance *invoke()
{
    ecs_entity owner = ecs_world_create_entity(world);
    execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
    if (!instance ||
        execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) != FLOW_OK) {
        return NULL;
    }
    return instance;
}
TEST_CASE(generic_argument_types_specialize_once_and_execute)
{
    const char *source = "Generic :: behavior { result: retain int = 0; real: "
                         "retain f64 = 0; "
                         "Identity :: (value: $T) -> T { return value } "
                         "on Run :: () { result = Identity(int(7)) + "
                         "Identity(int(8)); "
                         "real = Identity(f64(2.5)); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(compile(source, backends[i]));
        execution_instance *instance = invoke();
        REQUIRE(instance);
        const pin_value *integer = execution_instance_state(instance, state_id("result"));
        const pin_value *real = execution_instance_state(instance, state_id("real"));
        REQUIRE(integer && integer->type == PIN_I32 && integer->i == 15);
        REQUIRE(real && real->type == PIN_F64 && real->d == 2.5);
        int identities = 0;
        for (int j = 0; j < analysis->function_count; j++) {
            int symbol = analysis->functions[j].symbol;
            identities += symbol >= 0 &&
                          !strcmp(analysis->symbols[symbol].name, "Identity");
        }
        if (identities != 2) {
            for (int j = 0; j < analysis->function_count; j++) {
                int symbol = analysis->functions[j].symbol;
                if (symbol >= 0 && !strcmp(analysis->symbols[symbol].name, "Identity")) {
                    int p = analysis->functions[j].parameters[0];
                    script_type t = analysis->symbols[p].type;
                    fprintf(
                        stderr,
                        "Identity specialization id=%llu storage=%d "
                        "nominal=%llu flags=%u collection=%llu\n",
                        (unsigned long long)analysis->symbols[symbol].id,
                        t.storage,
                        (unsigned long long)t.nominal,
                        t.flags,
                        (unsigned long long)t.collection
                    );
                }
            }
        }
        REQUIRE(identities == 2);
    }
    return true;
}
TEST_CASE(generic_constant_parameters_and_authored_compile_time_calls)
{
    const char
        *source = "Constants :: behavior { result: retain int = 0; real:retain "
                  "f64=0; "
                  "Factorial :: (n: int) -> int { if n <= 1 { return 1 } "
                  "return n * Factorial(n - 1) } "
                  "Size :: Factorial(5); "
                  "Scale :: (value: $T, $N: int) -> T { return value * N } "
                  "Gain :: (value:$T,$Amount:f64)->T{return value*Amount} "
                  "on Run :: () { result = Scale(int(2), Size); "
                  "real=Gain(f64(3),0.5); } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(compile(source, backends[i]));
        execution_instance *instance = invoke();
        REQUIRE(instance);
        const pin_value *value = execution_instance_state(instance, state_id("result"));
        REQUIRE(value && value->type == PIN_I32 && value->i == 240);
        const pin_value *real = execution_instance_state(instance, state_id("real"));
        REQUIRE(real && real->type == PIN_F64 && real->d == 1.5);
    }
    return true;
}
/* A call evaluated at compile time computes what it computes at run time:
 * conversions clamp unless they wrap, and no integer operation fails. */
TEST_CASE(compile_time_calls_follow_runtime_semantics)
{
    const char
        *source = "Conversions :: behavior { wrapped: retain int = 0; clamped: "
                  "retain int = 0; exact: retain int = 0; quotient: retain int "
                  "= 1; remainder: retain int = 0; shifted: retain int = 1; "
                  "Wrapped :: (n: int) -> i8 { return i8(wrap: n) } "
                  "Clamped :: (n: int) -> i8 { return i8(n) } "
                  "Quotient :: (a: int, b: int) -> int { return a / b } "
                  "Remainder :: (a: int, b: int) -> int { return a % b } "
                  "Shifted :: (a: int, n: u32) -> int { return a << n } "
                  "W :: Wrapped(300); C :: Clamped(300); E :: Clamped(100); "
                  "Q :: Quotient(7, 0); R :: Remainder(7, 0); S :: Shifted(1, "
                  "40); "
                  "on Run :: () { wrapped = int(W); clamped = int(C); exact = "
                  "int(E); quotient = Q; remainder = R; shifted = S; } }";
    const char *expected[] = { "wrapped",  "clamped",   "exact",
                               "quotient", "remainder", "shifted" };
    const int values[] = { 44, 127, 100, 0, 7, 0 };
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(compile(source, backends[i]));
        execution_instance *instance = invoke();
        REQUIRE(instance);
        for (int j = 0; j < 6; j++) {
            const pin_value *value = execution_instance_state(instance, state_id(expected[j]));
            REQUIRE(value && value->type == PIN_I32 && value->i == values[j]);
        }
    }
    return true;
}
TEST_CASE(generic_explicit_casts_and_typed_locals_infer_the_same_type)
{
    REQUIRE(
        analyze("Casts :: behavior { Identity::(value:$T)->T{return value} on Run :: () { direct:int=int(7); casted:=Identity(int(7)); typed:=Identity(direct); decimal:=Identity(f64(2.5)); } }")
    );
    int identities = 0;
    bool casted = false, typed = false, decimal = false;
    for (int i = 0; i < analysis->function_count; i++) {
        int symbol = analysis->functions[i].symbol;
        identities += symbol >= 0 &&
                      !strcmp(analysis->symbols[symbol].name, "Identity");
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind != SCRIPT_LOCAL) {
            continue;
        }
        if (!strcmp(symbol->name, "casted")) {
            casted = symbol->type.storage == PIN_I32;
        }
        if (!strcmp(symbol->name, "typed")) {
            typed = symbol->type.storage == PIN_I32;
        }
        if (!strcmp(symbol->name, "decimal")) {
            decimal = symbol->type.storage == PIN_F64;
        }
    }
    REQUIRE(casted && typed && decimal && identities == 2);
    return true;
}
TEST_CASE(generic_array_binders_preserve_nested_collection_descriptors)
{
    REQUIRE(
        analyze("Nested :: behavior { First::(values:$T[])->T{return values[0]} on Run :: () { items:[string:int][]=[[\"one\":1]]; first:=First(items); } }")
    );
    bool nested = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_LOCAL && !strcmp(symbol->name, "first")) {
            nested = symbol->type.container == PIN_CONTAINER_MAP &&
                     symbol->type.collection != 0;
        }
    }
    REQUIRE(nested);
    return true;
}
TEST_CASE(generic_reference_arguments_copy_unless_the_parameter_is_a_pointer)
{
    const char
        *source = "Borrowed :: behavior { copied:retain int=0; changed:retain "
                  "int=0; "
                  "Identity::(value:$T)->T{return value} "
                  "Increment::(value:^$T){value^+=1} "
                  "Borrow::(item:^int){copy:=Identity(item^); "
                  "Increment(value: item); copied=copy;} "
                  "on Run :: () { value:int=4; Borrow(item: &value); "
                  "changed=value; } "
                  "}";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(compile(source, backends[backend]));
        bool copy_signature = false, alias_signature = false;
        for (int i = 0; i < analysis->function_count; i++) {
            const script_function *function = &analysis->functions[i];
            if (function->symbol < 0) {
                continue;
            }
            const char *name = analysis->symbols[function->symbol].name;
            if (!strcmp(name, "Identity")) {
                copy_signature = !(analysis->symbols[function->parameters[0]]
                                       .type.flags &
                                   SCRIPT_TYPE_REFERENCE) &&
                                 !(analysis->symbols[function->results[0]].type.flags &
                                   SCRIPT_TYPE_REFERENCE);
            }
            if (!strcmp(name, "Increment")) {
                alias_signature = (analysis->symbols[function->parameters[0]]
                                       .type.flags &
                                   SCRIPT_TYPE_REFERENCE) != 0;
            }
        }
        REQUIRE(copy_signature && alias_signature);
        execution_instance *instance = invoke();
        REQUIRE(instance);
        const pin_value *copied = execution_instance_state(instance, state_id("copied"));
        const pin_value *changed = execution_instance_state(instance, state_id("changed"));
        REQUIRE(copied && copied->type == PIN_I32 && copied->i == 4);
        REQUIRE(changed && changed->type == PIN_I32 && changed->i == 5);
    }
    return true;
}
static const char
    *quantity_source = "Units :: library { "
                       "Quantity :: struct($DataType: Type, $Length: int, "
                       "$Time: int) { amount: DataType } "
                       "ProductType :: (A: Type, B: Type) -> Type { "
                       "return Quantity(A.DataType, A.Length + B.Length, "
                       "A.Time + B.Time) } "
                       "Multiply :: (a: $A, b: $B) -> ProductType(A, B) { "
                       "result: ProductType(A, B) = {amount: a.amount * "
                       "b.amount}; return result } "
                       "Run :: () -> Quantity(f64, 1, 0) { "
                       "speed: Quantity(f64, 1, -1) = {amount: 6}; "
                       "time: Quantity(f64, 0, 1) = {amount: 2}; return "
                       "Multiply(speed, time) } }";
static bool constructed_units(const char *source)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(compile(source, backends[backend]));
        bool found = false;
        for (int i = 0; i < analysis->type_count; i++) {
            const script_catalog_type *type = &analysis->types[i];
            if (type->authored_record) {
                REQUIRE(type->field_count == 1);
                REQUIRE(type->size == sizeof(double));
                REQUIRE(!strcmp(type->fields[0].name, "amount"));
                found = true;
            }
        }
        REQUIRE(found);
        int multiply = -1, entry = -1;
        for (int i = 0; i < analysis->function_count; i++) {
            int symbol = analysis->functions[i].symbol;
            if (symbol >= 0 && !strcmp(analysis->symbols[symbol].name, "Multiply")) {
                multiply = i;
            }
            if (symbol >= 0 && !strcmp(analysis->symbols[symbol].name, "Run")) {
                entry = i;
            }
        }
        REQUIRE(multiply >= 0 && entry >= 0);
        script_type product = analysis
                                  ->symbols[analysis->functions[multiply].results[0]]
                                  .type;
        script_type expected = analysis
                                   ->symbols[analysis->functions[entry].results[0]]
                                   .type;
        REQUIRE(script_type_equal(product, expected));
        uint64_t id = analysis->symbols[analysis->functions[entry].symbol].id;
        const behavior_function *function = NULL;
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info info = execution_module_function_at(module, i);
            if (info.id == id) {
                function = info.body;
            }
        }
        REQUIRE(function && function->result_count == 1);
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = NULL;
        result = function->result_defaults[0];
        REQUIRE(result.record && pin_record_valid(result.record));
        REQUIRE(arena_contains(&function->owned, result.record));
        behavior_function_prepare_results(function, &result, &storage);
        behavior_function_activation_init(function, &activation);
        context = (behavior_function_context){ .activation = &activation,
                                               .results = &result,
                                               .budget = 10000 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(function, &context, NULL, 0));
        behavior_run_scope_reset(&runs);
        behavior_run_scope_begin(&runs);
        REQUIRE(
            behavior_function_run(function, &context, backends[backend] != BEHAVIOR_BYTECODE) == FLOW_OK
        );
        behavior_run_scope_end(&runs);
        REQUIRE(result.record && result.ptr && result.record->type.field_count == 1);
        pin_value amount;
        {
            const char *error = pin_read_field(
                &storage,
                result.ptr,
                &result.record->type.fields[0],
                &amount
            );
            if (error) {
                abort();
            }
        };
        REQUIRE(amount.type == PIN_F64 && amount.d == 12);
    }
    return true;
}
TEST_CASE(generic_constructed_units_compute_return_types_without_stored_dimensions)
{
    return constructed_units(quantity_source);
}
/* Exercise historical generic type arguments through migration, compilation
 * and both execution backends with a literal result oracle. */
TEST_CASE(generic_version_34_quantity_type_arguments_migrate)
{
    const char
        *historical = "Units :: library { "
                      "Quantity :: struct($DataType: Type, $Length: int, "
                      "$Time: int) { amount: DataType } "
                      "ProductType :: (A: Type, B: Type) -> Type { return "
                      "Quantity(A.DataType, "
                      "A.Length + B.Length, A.Time + B.Time) } "
                      "Multiply :: (a: $A, b: $B) -> ProductType(A, B) { "
                      "result: ProductType(A, B) = {amount: a.amount * "
                      "b.amount}; return result } "
                      "Run :: () -> Quantity(double, 1, 0) { "
                      "speed: Quantity(double, 1, -1) = {amount: 6}; "
                      "time: Quantity(double, 0, 1) = {amount: 2}; return "
                      "Multiply(speed, time) } }";
    char error[256] = { 0 }, *migrated = NULL;
    REQUIRE(!chibi_migrate_number_names(historical, &migrated, error, sizeof error));
    const char *actual = migrated ? migrated : historical;
    bool matches = !strcmp(actual, quantity_source);
    if (!matches) {
        fprintf(stderr, "version 34 Quantity result: %s\n", actual);
    }
    bool executed = constructed_units(actual);
    free(migrated);
    REQUIRE(matches && executed);
    return true;
}
TEST_CASE(generic_version_34_argument_roles_preserve_user_names)
{
    const struct
    {
        const char *source, *expected;
    } cases[] = {
        { "L :: library { F :: (x: double | int) {} }",
          "L :: library { F :: (x: f64 | int) {} }" },
        { "L :: library { F :: (p: Pair) -> Type { return pkg.Make(double, "
          "float: float, p.double, pkg.double).field } }",
          "L :: library { F :: (p: Pair) -> Type { return pkg.Make(f64, float: "
          "number, p.double, pkg.double).field } }" },
        { "L :: library { F :: (ordinary: int) -> int { return "
          "Call(double(double(ordinary)), (byte), {double: float}, [double]) } "
          "}",
          "L :: library { F :: (ordinary: int) -> int { return "
          "Call(f64(f64(ordinary)), (u8), {double: number}, [f64]) } }" },
        { "// double stays\nL :: library { Row :: struct { double: int } U :: "
          "union { double | Other } F :: (p: User.double) -> string { double: "
          "return \"double\" + p.double + pkg.double(1) } }",
          NULL },
        { "L :: library { F :: (x: number double/s) -> number double/s { "
          "return x } }",
          NULL },
        { "package p\nimport \"../math\"\nF :: () -> Type { return "
          "Make(double) }",
          "package p\nimport \"../math\"\nF :: () -> Type { return Make(f64) "
          "}" },
        { "package p\nimport \"core:strings\"\nF :: () -> Type { return "
          "Make(float) }",
          "package p\nimport \"core:strings\"\nF :: () -> Type { return "
          "Make(number) }" },
        { "package p\nimport \"../m\\u0061th\"\nF :: () -> Type { return "
          "Make(double) }",
          "package p\nimport \"../m\\u0061th\"\nF :: () -> Type { return "
          "Make(f64) }" },
        { "package p\nimport \"core:\"\nF :: () -> int { return 7 }", NULL },
        { "package p\nimport _ \"core:\"\nF :: () -> Type { return "
          "Make(double) }",
          "package p\nimport _ \"core:\"\nF :: () -> Type { return Make(f64) "
          "}" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char error[256] = { 0 }, *migrated = NULL, *again = NULL;
        int status = chibi_migrate_number_names(cases[i].source, &migrated, error, sizeof error);
        if (status ||
            (cases[i].expected ? !migrated || strcmp(migrated, cases[i].expected)
                               : migrated != NULL)) {
            fprintf(stderr, "argument role case %zu: %s\n%s\n", i, error, migrated ? migrated : "unchanged");
        }
        REQUIRE(!status);
        REQUIRE(cases[i].expected ? migrated && !strcmp(migrated, cases[i].expected) : !migrated);
        REQUIRE(
            !chibi_migrate_number_names(migrated ? migrated : cases[i].source, &again, error, sizeof error)
        );
        REQUIRE(!again);
        free(migrated);
    }
    return true;
}
TEST_CASE(generic_version_34_bindings_and_uncertain_roles_refuse)
{
    const struct
    {
        const char *source, *diagnostic;
    } cases[] = {
        { "L :: library { F :: (xs: int[]) { for (double, item in xs) { "
          "Use(item) } } }",
          "binding is named like a number type; rename it first at 1:40" },
        { "L :: library { F :: (xs: int[]) { for (item, float in xs) { "
          "Use(item) } } }",
          "declaration is named like a number type; rename it first at 1:46" },
        { "L :: library { F :: (x: int) { if (x is int double) { Use(double) } "
          "} }",
          "binding is named like a number type; rename it first at 1:45" },
        { "L :: library { U :: union { A(value: int) } F :: (x: U) { switch x "
          "{ case .A(double): Use(double) } } }",
          "binding is named like a number type; rename it first at 1:78" },
        { "L :: library { F :: (x: $number) -> int { return 1 } }",
          "declaration is named like a number type; rename it first at 1:26" },
        { "L :: library { Quantity :: struct($DataType: Type, $Length: int, "
          "$Time: int) { amount: DataType } F :: ($number: Type) -> "
          "Quantity(float, 1, -1) { return {} } }",
          "declaration is named like a number type; rename it first at 1:106" },
        { "package p\nimport number \"core:math\"\nF :: () -> Type { return "
          "Make(float) }",
          "import alias is named like a number type; use a different alias "
          "before migration at 2:8" },
        { "package p\nimport \"../double\"\nF :: () -> Type { return "
          "Make(double) }",
          "import alias is named like a number type; use a different alias "
          "before migration at 2:8" },
        { "package p\nimport \"core:\"\nF :: () -> Type { return Make(double) "
          "}",
          "unresolved import alias may capture a number-type rewrite; supply "
          "an explicit alias at 2:8" },
        { "L :: library { F :: () -> Type { return double } }", "unsupported number-type expression; use an explicit type argument before migration at 1:41" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char error[256] = { 0 }, *migrated = NULL;
        int status = chibi_migrate_number_names(cases[i].source, &migrated, error, sizeof error);
        fprintf(stderr, "binding case %zu: %d %s\n", i, status, error);
        REQUIRE(status < 0 && !migrated);
        REQUIRE(!strcmp(error, cases[i].diagnostic));
    }
    return true;
}
static bool migration_write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    bool ok = fputs(text, file) >= 0;
    return !fclose(file) && ok;
}
static bool migration_file_is(const char *path, const char *expected)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    char text[2048];
    size_t size = fread(text, 1, sizeof text - 1, file);
    bool complete = feof(file) && !ferror(file);
    fclose(file);
    text[size] = 0;
    return complete && !strcmp(text, expected);
}
static bool migration_directory(char root[PATH_MAX])
{
    const char *temporary = getenv("TMPDIR");
    int length = snprintf(
        root,
        PATH_MAX,
        "%s/chibi-number-bindings-XXXXXX",
        temporary && *temporary ? temporary : "/tmp"
    );
    REQUIRE(length > 0 && length < PATH_MAX);
    REQUIRE(mkdtemp(root));
    return true;
}
TEST_CASE(generic_migration_scratch_is_private_and_owned)
{
    char first[PATH_MAX], second[PATH_MAX], marker[PATH_MAX + 32];
    REQUIRE(migration_directory(first) && migration_directory(second));
    const char *temporary = getenv("TMPDIR");
    const char *parent = temporary && *temporary ? temporary : "/tmp";
    size_t length = strlen(parent);
    REQUIRE(!strncmp(first, parent, length) && first[length] == '/');
    REQUIRE(!strncmp(second, parent, length) && second[length] == '/');
    REQUIRE(strcmp(first, second));
    struct stat info;
    REQUIRE(!stat(first, &info) && S_ISDIR(info.st_mode) && (info.st_mode & 0777) == 0700);
    REQUIRE(!stat(second, &info) && S_ISDIR(info.st_mode) && (info.st_mode & 0777) == 0700);
    snprintf(marker, sizeof marker, "%s/owned", first);
    REQUIRE(migration_write(marker, "first"));
    REQUIRE(!unlink(marker) && !rmdir(first));
    REQUIRE(!stat(second, &info) && S_ISDIR(info.st_mode));
    REQUIRE(!rmdir(second));
    return true;
}
TEST_CASE(generic_version_34_cross_file_collisions_preserve_project)
{
    char root[PATH_MAX], content[PATH_MAX + 32], paths[4][PATH_MAX + 64],
        error[512] = { 0 };
    REQUIRE(migration_directory(root));
    snprintf(content, sizeof content, "%s/content", root);
    REQUIRE(!mkdir(content, 0700));
    snprintf(paths[0], sizeof paths[0], "%s/project.kdl", root);
    snprintf(paths[1], sizeof paths[1], "%s/Use.chibi", content);
    snprintf(paths[2], sizeof paths[2], "%s/Binding.chibi", content);
    snprintf(paths[3], sizeof paths[3], "%s/Event.kdl", content);
    const char *manifest = "version 34\ncontent \"content\"\nstart_scene "
                           "\"Start.kdl\"\n";
    const char *use = "package p\nF :: () -> double { return double(7) }";
    const char *binding = "package p\nnumber :: () -> int { return 3 }";
    const char *event = "domain \"event\"\n(array)variables {\n    - "
                        "name=\"Amount\" type=\"double\"\n}\n";
    const char *original[] = { manifest, use, binding, event };
    for (int i = 0; i < 4; i++) {
        REQUIRE(migration_write(paths[i], original[i]));
    }
    int status = chibi_project_migrate(root, error, sizeof error);
    fprintf(stderr, "cross-file migration: %d %s\n", status, error);
    REQUIRE(
        status < 0 && strstr(error, "Binding.chibi: declaration is named like a number type; rename it first at 2:1")
    );
    for (int i = 0; i < 4; i++) {
        REQUIRE(migration_file_is(paths[i], original[i]));
    }
    /* This unresolved import is file-local and cannot capture Use.chibi's edits. */
    const char
        *safe = "package p\nimport \"core:\"\nG :: () -> int { return 3 }";
    REQUIRE(migration_write(paths[2], safe));
    REQUIRE(chibi_project_migrate(root, error, sizeof error) == CHIBI_PROJECT_VERSION - 34);
    char current[80];
    snprintf(current, sizeof current, "version %d\ncontent \"content\"\nstart_scene \"Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    REQUIRE(migration_file_is(paths[0], current));
    REQUIRE(migration_file_is(paths[1], "package p\nF :: () -> f64 { return f64(7) }"));
    REQUIRE(migration_file_is(paths[2], safe));
    REQUIRE(!chibi_project_migrate(root, error, sizeof error));
    for (int i = 0; i < 4; i++) {
        REQUIRE(!unlink(paths[i]));
    }
    REQUIRE(!rmdir(content) && !rmdir(root));
    return true;
}
void test_language_migration34_generic_repro()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(generic_migration_scratch_is_private_and_owned);
    RUN_TEST_CASE(generic_version_34_quantity_type_arguments_migrate);
    RUN_TEST_CASE(generic_version_34_argument_roles_preserve_user_names);
    RUN_TEST_CASE(generic_version_34_bindings_and_uncertain_roles_refuse);
    RUN_TEST_CASE(generic_version_34_cross_file_collisions_preserve_project);
    pu_clear_setup();
}
static bool reported(const char *message)
{
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        if (!strcmp(analysis->diagnostics[i].message, message)) {
            return true;
        }
    }
    return false;
}
/* assert runs at compile time too, where a false one is a compile error. */
TEST_CASE(generic_compile_time_assert_fails_compilation)
{
    REQUIRE(analyze(
        "Good :: library { Half :: (n: int) -> int { assert(n >= 0); return n "
        "/ 2 } "
        "Size :: Half(4) }"
    ));
    REQUIRE(!analyze(
        "Bad :: library { Half :: (n: int) -> int { assert(n >= 0, \"n is "
        "negative\"); "
        "return n / 2 } Size :: Half(-4) }"
    ));
    REQUIRE(reported("Compile-time evaluation: Assertion failed: n is negative"));
    REQUIRE(!analyze(
        "Bad :: library { Half :: (n: int) -> int { assert(n >= 0); return n / "
        "2 } "
        "Size :: Half(-4) }"
    ));
    REQUIRE(reported("Compile-time evaluation: Assertion failed: n >= 0"));
    /* Arguments bind by name or position; an empty message is no message. */
    REQUIRE(analyze(
        "Good :: library { Half :: (n: int) -> int { "
        "assert(message: \"negative\", condition: n >= 0); return n / 2 } "
        "Size :: Half(4) }"
    ));
    REQUIRE(!analyze(
        "Bad :: library { Half :: (n: int) -> int { "
        "assert(n >= 0, message: \"negative\"); return n / 2 } Size :: "
        "Half(-4) }"
    ));
    REQUIRE(reported("Compile-time evaluation: Assertion failed: negative"));
    REQUIRE(!analyze(
        "Bad :: library { Half :: (n: int) -> int { assert(n >= 0, \"\"); "
        "return n / 2 } Size :: Half(-4) }"
    ));
    REQUIRE(reported("Compile-time evaluation: Assertion failed: n >= 0"));
    return true;
}
TEST_CASE(generic_constraints_and_compile_time_execution_are_bounded)
{
    const char *invalid[] = {
        "Bad :: behavior { Same :: (a: $T, b: T) -> T { return a } on Run :: "
        "() { x "
        ":= Same(int(1), true) } }",
        "Bad :: behavior { n: retain int = 3; Add :: (x: int) -> int { return "
        "x + n } Value :: Add(1) }",
        "Bad :: behavior { Forever :: (n: int) -> int { while true { n += 1 } "
        "return n } Value :: Forever(1) }",
        "Bad :: library { Box :: struct($T: Type, $N: int) { value: T } Make "
        ":: () -> Box(int) { return {value: 1} } }",
        "Bad :: behavior { Forever :: (n: int) -> int { return Forever(n) } "
        "Value :: Forever(1) }",
        "Bad :: behavior { Identity :: (value:$T)->T{return value} "
        "on Run :: () { x:=Identity(missing:true) } }",
        "Bad :: library { Pick :: (A: Type) -> Type { x := i8(clamp: 300); "
        "return A } Box :: () -> Pick(int) { return 1 } }",
        "Bad :: library { Pick :: (A: Type) -> Type { x := i8(round: 300); "
        "return A } Box :: () -> Pick(int) { return 1 } }",
        "Bad :: library { Pick :: (A: Type) -> Type { x := i8(wrap: 1.5); "
        "return A } Box :: () -> Pick(int) { return 1 } }",
    };
    const char *messages[] = { "Conflicting polymorphic argument",
                               "only constant values and types are available",
                               "bounded execution budget exceeded",
                               "each constant parameter exactly once",
                               "call depth exceeded",
                               "Unknown polymorphic argument",
                               "may only be named wrap",
                               "may only be named wrap",
                               "wrap converts between integer types" };
    for (int i = 0; i < 9; i++) {
        REQUIRE(!analyze(invalid[i]));
        REQUIRE(!syntax->diagnostic_count);
        bool expected = false;
        for (int j = 0; j < analysis->diagnostic_count; j++) {
            expected |= strstr(analysis->diagnostics[j].message, messages[i]) != NULL;
        }
        REQUIRE(expected);
    }
    return true;
}
TEST_CASE(generic_empty_record_bindings_specialize_once)
{
    REQUIRE(analyze(
        "Empty :: library { Box :: struct() { value:bool } "
        "Make :: () -> Box() { item:Box()={value:true}; return item } }"
    ));
    REQUIRE(!syntax->diagnostic_count);
    REQUIRE(analysis->type_count == 1);
    REQUIRE(analysis->specialization_count == 1);
    const script_catalog_specialization *s = &analysis->specializations[0];
    REQUIRE(!s->binding_count && !s->bindings);
    REQUIRE(s->result.is_type);
    REQUIRE(s->result.type.storage == PIN_STRUCT);
    REQUIRE(s->result.type.nominal == analysis->types[0].type.nominal);
    REQUIRE(analysis->types[0].field_count == 1);
    REQUIRE(analysis->types[0].fields[0].type.storage == PIN_BOOL);
    return true;
}
TEST_CASE(compile_time_calls_preserve_numeric_names_and_owned_strings)
{
    REQUIRE(analyze(
        "Checks :: behavior { "
        "Half :: () -> number { return 1 / 2 } "
        "IntegerHalf :: () -> int { return 1 / 2 } "
        "TypedLocal :: () -> int { n:int=5/2*2; return n } "
        "TypedAssign :: () -> int { n:int=0; n=5/2*2; return n } "
        "Count :: (n:int)->int { if n==0 { return 0 } return Count(n-1)+1 } "
        "Subtract :: (/* left */ a:int, /* right */ b:int)->int { return a-b } "
        "Echo :: (value:string)->string { return value } "
        "Label :: (value:$T,$Text:string)->T { return value } "
        "Fraction :: Half(); Whole :: IntegerHalf(); Local :: TypedLocal(); "
        "Assigned :: TypedAssign(); Diff :: Subtract(b:2,a:10); Deep :: "
        "Count(120); "
        "Text :: Echo(\"owned text\"); "
        "on Run :: () { result := Label(1, \"owned binding\") } }"
    ));
    bool fraction = false, whole = false, difference = false, text = false;
    bool local = false, assigned = false;
    bool deep = false;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind != SCRIPT_CONSTANT || symbol->initializer < 0) {
            continue;
        }
        pin_value value = analysis->expressions[symbol->initializer].constant;
        if (!strcmp(symbol->name, "Fraction")) {
            fraction = value.type == PIN_F32 && value.f == 0.5f;
        }
        if (!strcmp(symbol->name, "Whole")) {
            whole = value.type == PIN_I32 && value.i == 0;
        }
        if (!strcmp(symbol->name, "Local")) {
            local = value.type == PIN_I32 && value.i == 4;
        }
        if (!strcmp(symbol->name, "Assigned")) {
            assigned = value.type == PIN_I32 && value.i == 4;
        }
        if (!strcmp(symbol->name, "Diff")) {
            difference = value.type == PIN_I32 && value.i == 8;
        }
        if (!strcmp(symbol->name, "Deep")) {
            deep = value.type == PIN_I32 && value.i == 120;
        }
        if (!strcmp(symbol->name, "Text")) {
            text |= value.type == PIN_STR &&
                    !strcmp(pin_text(value).bytes, "owned text");
        }
    }
    REQUIRE(fraction && whole && difference && text && local && assigned && deep);
    cleanup(); /* Arena-backed compile values must not be individually freed. */
    REQUIRE(
        !analyze("Bad :: library { Box :: struct($T:Type) { items:T[] } Make :: ()->Box(int) { return {items:[]} } }")
    );
    REQUIRE(!syntax->diagnostic_count);
    bool layout = false;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        layout |= strstr(analysis->diagnostics[i].message, "collections and references are not supported") !=
                  NULL;
    }
    REQUIRE(layout);
    return true;
}
static const pin_record *generic_wire_record(execution_wire *, void *user, const pin_value *, bool authored)
{
    return authored ? user : NULL;
}
TEST_CASE(generic_constructed_types_preserve_array_optional_and_reference_modifiers)
{
    const char
        *source = "Composed :: library { "
                  "Box::struct($T:Type){value:T} "
                  "seed:retain Box(int)[][]=[]; "
                  "Bump::(value:^Box(int)){value.value+=1} "
                  "Run::()->int { values:Box(int)[]=[{value:4}]; "
                  "item:Box(int)={value:5}; "
                  "Bump(&item); maybe:Box(int)?=item; "
                  "nested:Box(int)[][]=[values]; "
                  "return item.value+values[0].value+nested[0][0].value; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(compile(source, backends[backend]));
        bool array = false, optional = false, reference = false, nested = false;
        uint64_t collection = 0;
        for (int i = 0; i < analysis->symbol_count; i++) {
            const script_symbol *symbol = &analysis->symbols[i];
            if (!strcmp(symbol->name, "values")) {
                array = symbol->type.container == PIN_CONTAINER_ARRAY &&
                        symbol->type.nominal;
            } else if (!strcmp(symbol->name, "maybe")) {
                optional = (symbol->type.flags &
                            (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL)) ==
                           (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
            } else if (symbol->kind == SCRIPT_PARAMETER && !strcmp(symbol->name, "value")) {
                reference = symbol->type.nominal &&
                            (symbol->type.flags & SCRIPT_TYPE_REFERENCE);
            } else if (!strcmp(symbol->name, "nested")) {
                nested = symbol->type.container == PIN_CONTAINER_ARRAY &&
                         symbol->type.collection;
                collection = symbol->type.collection;
            }
        }
        REQUIRE(array && optional && reference && nested);
        const script_lower_bindings bindings = { 0 };
        execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
        REQUIRE(plan->valid);
        const pin_record *record = NULL;
        for (int i = 0; i < plan->type_count; i++) {
            if (plan->types[i].record) {
                record = plan->types[i].record;
            }
        }
        const char *name = NULL;
        for (int i = 0; i < analysis->type_count; i++) {
            if (analysis->types[i].id == collection) {
                name = analysis->types[i].name;
            }
        }
        REQUIRE(record && record->type.size == sizeof(int) && name);
        uint64_t record_id = record->id, record_layout = record->layout;
        int seed = 9;
        pin_value leaf = { .type = PIN_STRUCT,
                           .type_name = record->type.name,
                           .record = record,
                           .ptr = &seed };
        pin_array inner = { .element_type = PIN_STRUCT,
                            .type_name = record->type.name,
                            .record = record,
                            .items = &leaf,
                            .count = 1,
                            .capacity = 1 };
        pin_value inner_value = leaf;
        inner_value.container = PIN_CONTAINER_ARRAY;
        inner_value.ptr = &inner;
        pin_array outer = { .element_type = PIN_STRUCT,
                            .type_name = name,
                            .items = &inner_value,
                            .count = 1,
                            .capacity = 1 };
        REQUIRE(plan->state_count == 1);
        uint64_t saved_id = plan->states[0].id;
        plan->states[0].initial = (pin_value){ .type = PIN_STRUCT,
                                               .container = PIN_CONTAINER_ARRAY,
                                               .type_name = name,
                                               .ptr = &outer };
        pin_value mixed[] = { inner_value, inner_value };
        mixed[1].container = PIN_CONTAINER_SET;
        outer.items = mixed;
        outer.count = outer.capacity = 2;
        execution_wire invalid = { 0 };
        execution_wire_value_owned(&invalid, &plan->states[0].initial, generic_wire_record, (void *)record);
        REQUIRE(invalid.error && !strcmp(invalid.error, "Prepared nested collection headers differ"));
        afree(invalid.data);
        outer.items = &inner_value;
        outer.count = outer.capacity = 1;
        execution_program_free(program);
        program = NULL;
        execution_module_free(module);
        char error[192];
        module = execution_module_finish(&plan, backends[backend], error, sizeof error);
        execution_module_plan_free(plan);
        if (!module) {
            fprintf(stderr, "generic prepared finish: %s\n", error);
        }
        REQUIRE(module);
        unsigned char *bytes = NULL;
        size_t length = 0;
        bool encoded = execution_prepared_write(module, &bytes, &length, error, sizeof error);
        if (!encoded) {
            fprintf(stderr, "generic prepared write: %s\n", error);
        }
        REQUIRE(encoded);
        execution_module *decoded = execution_prepared_read(
            bytes,
            length,
            NULL,
            backends[backend],
            error,
            sizeof error
        );
        free(bytes);
        if (!decoded) {
            fprintf(stderr, "generic prepared read: %s\n", error);
        }
        REQUIRE(decoded);
        execution_module_free(module);
        module = decoded;
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        ecs_entity owner = ecs_world_create_entity(world);
        execution_instance *instance = execution_program_attach(program, ecs_entity_to_u64(owner));
        REQUIRE(instance);
        const pin_value *saved = execution_instance_state(instance, saved_id);
        REQUIRE(saved && saved->container == PIN_CONTAINER_ARRAY && !saved->record);
        pin_value decoded_inner = pin_array_get(saved->ptr, 0);
        REQUIRE(decoded_inner.container == PIN_CONTAINER_ARRAY && decoded_inner.record);
        REQUIRE(
            decoded_inner.record->id == record_id && decoded_inner.record->layout == record_layout
        );
        pin_value decoded_leaf = pin_array_get(decoded_inner.ptr, 0);
        REQUIRE(decoded_leaf.record == decoded_inner.record);
        REQUIRE(decoded_leaf.ptr && *(int *)decoded_leaf.ptr == seed);
        av_doc *document = av_doc_new();
        av_value *projected = pin_value_to_document(document, saved);
        REQUIRE(projected && av_is_obj(projected));
        const av_value *items = av_obj_get(projected, "items");
        REQUIRE(items && av_is_arr(items) && av_arr_size(items) == 1);
        const av_value *row = av_arr_get(items, 0);
        REQUIRE(row && av_is_arr(row) && av_arr_size(row) == 1);
        REQUIRE(av_get_int(av_obj_get(av_arr_get(row, 0), "value")) == seed);
        av_doc_free(document);
        uint64_t entry = 0;
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_FUNCTION &&
                !strcmp(analysis->symbols[i].name, "Run")) {
                entry = analysis->symbols[i].id;
            }
        }
        const behavior_function *function = NULL;
        for (int i = 0; i < execution_module_function_count(module); i++) {
            execution_module_function_info info = execution_module_function_at(module, i);
            if (info.id == entry) {
                function = info.body;
            }
        }
        REQUIRE(function && function->result_count == 1);
        result = function->result_defaults[0];
        behavior_function_prepare_results(function, &result, &storage);
        behavior_function_activation_init(function, &activation);
        pin_value variables[] = { *saved };
        /* Run's dynamic arrays live in the caller's managed heap. */
        behavior_managed_heap_init(&heap);
        context = (behavior_function_context){ .activation = &activation,
                                               .variables = variables,
                                               .results = &result,
                                               .managed_heap = &heap,
                                               .budget = 10000 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(function, &context, NULL, 0));
        behavior_run_scope_reset(&runs);
        behavior_run_scope_begin(&runs);
        REQUIRE(
            behavior_function_run(function, &context, backends[backend] != BEHAVIOR_BYTECODE) == FLOW_OK
        );
        behavior_run_scope_end(&runs);
        context.variables = NULL;
        REQUIRE(result.type == PIN_I32 && result.i == 14);
    }
    return true;
}
TEST_CASE(compile_time_string_alias_survives_analysis_destruction)
{
    const char *source = "Strings :: behavior { result:retain string=\"\"; "
                         "Echo::(value:string)->string{return value} "
                         "Text::Echo(\"owned after analysis\"); on Run :: () "
                         "{result=Text} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(compile(source, backends[i]));
        uint64_t retained = state_id("result");
        REQUIRE(retained);
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = NULL;
        execution_instance *instance = invoke();
        REQUIRE(instance);
        const pin_value *value = execution_instance_state(instance, retained);
        REQUIRE(
            value && value->type == PIN_STR &&
            !strcmp(pin_text(*value).bytes, "owned after analysis")
        );
    }
    return true;
}
TEST_CASE(legacy_bounded_generic_aliases_migrate_but_unbound_require_binders)
{
    char error[256], *migrated = NULL, *again = NULL;
    REQUIRE(
        !chibi_migrate_syntax("library Fixed { Identity :: <T: plain int>(item:T)->T { return item } }", &migrated, error, sizeof error)
    );
    REQUIRE(migrated && !strstr(migrated, "<T"));
    REQUIRE(analyze(migrated));
    REQUIRE(!chibi_migrate_syntax(migrated, &again, error, sizeof error));
    REQUIRE(!again);
    free(migrated);
    migrated = NULL;
    REQUIRE(chibi_migrate_syntax(
        "library Open { Identity :: <T>(item:T)->T { return item } }",
        &migrated,
        error,
        sizeof error
    ));
    REQUIRE(!migrated && strstr(error, "argument binders"));
    return true;
}
TEST_SUITE(language_generic_suite)
{
    RUN_TEST_CASE(generic_argument_types_specialize_once_and_execute);
    RUN_TEST_CASE(generic_constant_parameters_and_authored_compile_time_calls);
    RUN_TEST_CASE(compile_time_calls_follow_runtime_semantics);
    RUN_TEST_CASE(generic_explicit_casts_and_typed_locals_infer_the_same_type);
    RUN_TEST_CASE(generic_array_binders_preserve_nested_collection_descriptors);
    RUN_TEST_CASE(generic_reference_arguments_copy_unless_the_parameter_is_a_pointer);
    RUN_TEST_CASE(generic_constructed_units_compute_return_types_without_stored_dimensions);
    RUN_TEST_CASE(generic_compile_time_assert_fails_compilation);
    RUN_TEST_CASE(generic_constraints_and_compile_time_execution_are_bounded);
    RUN_TEST_CASE(generic_empty_record_bindings_specialize_once);
    RUN_TEST_CASE(compile_time_calls_preserve_numeric_names_and_owned_strings);
    RUN_TEST_CASE(generic_constructed_types_preserve_array_optional_and_reference_modifiers);
    RUN_TEST_CASE(compile_time_string_alias_survives_analysis_destruction);
    RUN_TEST_CASE(legacy_bounded_generic_aliases_migrate_but_unbound_require_binders);
}
void test_language_generic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_SUITE(language_generic_suite);
    pu_clear_setup();
}
