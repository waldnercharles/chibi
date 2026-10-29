#include "authoring/package_tests.h"
#include "authoring/source_package_prepare.h"
#include "capabilities/catalog.h"
#include "capabilities/catalog_snapshot.h"
#include "execution/managed.h"
#include "execution/status.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fixed in-memory package corpus, no filesystem/processes/frames. Each source
 * is below1KiB and each snapshot contains at most five source documents. */
enum
{
    MAX_OWNERS = 4,
    MAX_FILES = 5
};
static authoring_packages *owners[MAX_OWNERS];
static execution_module *retained;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_managed_heap managed;
static arena storage, metadata_storage;
static pin_value argument, result;
static int default_host_calls;
static script_syntax *direct_syntax, *definition_syntax;
static script_analysis *direct_analysis;
static const script_catalog native = { 0 };
static const script_lower_bindings bindings = { .generation = 1 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
static const source_package_input chain[] = {
    { "app/main.chibi", "package app\nimport middle \"../middle\"\nMain :: (value: int) -> int { return middle.Value(value) + 1; }\n", 0 },
    { "middle/main.chibi", "package middle\nimport math \"../math\"\npub Value :: (value: int) -> int { return math.Value(value) * 2; }\n", 0 },
    { "math/main.chibi", "package numbers\npub Value :: (value: int) -> int { return value + 2; }\n", 0 },
    { "blank/main.chibi", "package blank\nimport _ \"../math\"\nUnused :: (value: int) -> int { return value + 7; }\n", 0 },
    { "other/main.chibi",
      "package other\nUnused :: (value: int) -> int { return value + 99; }\n",
      0 },
};
static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}
static void cleanup()
{
    finish_call();
    script_analysis_free(direct_analysis);
    script_syntax_free(direct_syntax);
    script_syntax_free(definition_syntax);
    definition_syntax = NULL;
    direct_analysis = NULL;
    direct_syntax = NULL;
    arena_free(&metadata_storage);
    execution_module_free(retained);
    retained = NULL;
    for (int i = 0; i < MAX_OWNERS; i++) {
        authoring_packages_free(owners[i]);
        owners[i] = NULL;
    }
}
static bool prepare_catalog(
    int slot,
    const source_package_input *files,
    int count,
    behavior_backend backend,
    int previous,
    const char *root,
    const script_catalog *catalog,
    const script_lower_bindings *binding
)
{
    REQUIRE(slot >= 0 && slot < MAX_OWNERS && count > 0 && count <= MAX_FILES && previous != slot);
    source_package_input inputs[MAX_FILES];
    for (int i = 0; i < count; i++) {
        inputs[i] = files[i];
        inputs[i].size = strlen(inputs[i].text);
        REQUIRE(inputs[i].size < 1024);
    }
    source_package_snapshot_options options = {
        .project_root = root,
        .files = inputs,
        .file_count = count,
    };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot);
    authoring_packages_free(owners[slot]);
    owners[slot] = authoring_packages_prepare(
        snapshot,
        catalog,
        binding,
        backend,
        previous >= 0 ? owners[previous] : NULL
    );
    REQUIRE(owners[slot]);
    return true;
}
static bool prepare(
    int slot,
    const source_package_input *files,
    int count,
    behavior_backend backend,
    int previous,
    const char *root
)
{
    return prepare_catalog(slot, files, count, backend, previous, root, &native, &bindings);
}
static const authoring_package *package(int slot, const char *identity)
{
    for (int i = 0; i < authoring_packages_count(owners[slot]); i++) {
        const authoring_package *item = authoring_packages_at(owners[slot], i);
        if (item && item->identity && !strcmp(item->identity, identity)) {
            return item;
        }
    }
    return NULL;
}
static bool valid(int slot)
{
    if (authoring_packages_valid(owners[slot])) {
        return true;
    }
    const char *error = authoring_packages_error(owners[slot]);
    if (error) {
        fprintf(stderr, "package owner: %s\n", error);
    }
    for (int i = 0; i < authoring_packages_count(owners[slot]); i++) {
        const authoring_package *item = authoring_packages_at(owners[slot], i);
        for (int j = 0; item && j < item->diagnostic_count; j++) {
            fprintf(
                stderr,
                "%s %d:%d: %s\n",
                item->identity,
                item->diagnostics[j].range.document,
                item->diagnostics[j].range.line,
                item->diagnostics[j].message
            );
        }
    }
    return false;
}
static uint64_t function_id(const authoring_package *item, const char *name)
{
    for (int i = 0; item && item->analysis && i < item->analysis->symbol_count; i++) {
        const script_symbol *symbol = &item->analysis->symbols[i];
        if (symbol->kind == SCRIPT_FUNCTION && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
static bool call(execution_module *module, uint64_t id, behavior_backend backend, int value, int expected)
{
    finish_call();
    REQUIRE(module && id && execution_module_backend(module) == backend);
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    argument = (pin_value){ .type = PIN_I32, .i = value };
    result = (pin_value){ .type = PIN_I32, .i = -999 };
    behavior_managed_heap_init(&managed);
    managed.call_collection_bytes = BEHAVIOR_CALL_COLLECTION_BYTES;
    context = (behavior_function_context){
        .managed_heap = &managed,
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = &argument,
        .argument_count = 1,
        .results = &result,
        .budget = 2000
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = behavior_function_run(function, &context, backend == BEHAVIOR_SLJIT);
    behavior_run_scope_end(&scope);
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(result.type == PIN_I32 && result.i == expected);
    REQUIRE(!scope.counts.overflow && !scope.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        !scope.counts.inspected_sljit_entries &&
        !scope.counts.inspected_wasm_entries && !scope.counts.wasm_entries
    );
    REQUIRE(
        backend == BEHAVIOR_SLJIT
            ? scope.counts.sljit_entries > 0 && !scope.counts.bytecode_entries
            : scope.counts.bytecode_entries > 0 && !scope.counts.sljit_entries
    );
    finish_call();
    return true;
}
static bool run(int slot, const char *identity, const char *name, behavior_backend backend, int value, int expected)
{
    const authoring_package *item = package(slot, identity);
    REQUIRE(item && item->valid && item->module);
    return call(item->module, function_id(item, name), backend, value, expected);
}

TEST_CASE(package_imports_transitive_calls_execute_on_each_backend)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, chain, 5, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 3, 11));
        REQUIRE(run(0, "project:middle", "Value", backends[b], 3, 10));
    }
    return true;
}
TEST_CASE(package_imports_reused_alias_spelling_remains_file_local)
{
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport lib \"../left\"\nA :: (value: int) -> int { return lib.Value(value); }\n", 0 },
        { "app/b.chibi", "package app\nimport lib \"../right\"\nB :: (value: int) -> int { return lib.Value(value); }\n", 0 },
        { "left/a.chibi", "package left\npub Value :: (value: int) -> int { return value + 2; }\n", 0 },
        { "right/a.chibi", "package right\npub Value :: (value: int) -> int { return value + 5; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 4, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "A", backends[b], 1, 3));
        REQUIRE(run(0, "project:app", "B", backends[b], 1, 6));
    }
    return true;
}
TEST_CASE(package_imports_self_alias_resolves_sibling_function)
{
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport self \".\"\nMain :: (value: int) -> int { return self.Add(value); }\n", 0 },
        { "app/b.chibi", "package app\nAdd :: (value: int) -> int { return value + 3; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 2, 5));
    }
    return true;
}
TEST_CASE(package_imports_do_not_leak_unqualified_names_package_names_or_blank_bindings)
{
    static const char *invalid[] = {
        "package app\nimport lib \"../math\"\nMain :: (value: int) -> int { "
        "return Value(value); }\n",
        "package app\nimport lib \"../math\"\nMain :: (value: int) -> int { "
        "return numbers.Value(value); }\n",
        "package app\nimport _ \"../math\"\nMain :: (value: int) -> int { "
        "return math.Value(value); }\n",
        "package app\nMain :: (value: int) -> int { return "
        "math.Value(value); }\n",
        "package app\nMain :: (value: int) -> int { return lib.Value(value); "
        "}\n",
    };
    const source_package_input imported = {
        "app/a.chibi",
        "package app\nimport lib \"../math\"\nA :: (value: int) -> int { "
        "return lib.Value(value); }\n",
        0
    };
    for (int b = 0; b < 2; b++) {
        for (int sample = 0; sample < 5; sample++) {
            const source_package_input files[] = { imported,
                                                   { "app/b.chibi", invalid[sample], 0 },
                                                   chain[2] };
            REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
            REQUIRE(!authoring_packages_valid(owners[0]));
            const authoring_package *app = package(0, "project:app");
            REQUIRE(app && !app->valid && !app->module && app->diagnostic_count > 0);
            bool local = false;
            for (int i = 0; i < app->diagnostic_count; i++) {
                local |= app->diagnostics[i].range.document == 1;
            }
            REQUIRE(local);
        }
    }
    return true;
}
TEST_CASE(package_imports_own_record_union_and_aggregate_constant_values)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport data \"../data\"\nTyped :: (value: int) -> "
          "int { "
          "pair: data.Pair = data.Make(value); choice: data.Choice = "
          "data.Choice.None; "
          "if (value != 0) { choice = data.Choice.Amount(value); } "
          "if (pair.tag != data.Words[1] || data.Words[0] != \"alpha\") { "
          "return -1; } "
          "extra: int = 0; switch data.Seed { case .None: extra = -100; case "
          ".Amount(amount): extra = amount; } "
          "switch choice { case .None: return pair.value + extra; case "
          ".Amount(amount): return amount + pair.value + extra; } }\n",
          0 },
        { "data/a.chibi",
          "package data\npub Pair :: struct { value: int; tag: string; }\n"
          "pub Choice :: union { None | Amount(value: int) };\nBase: "
          "int : 4;\n"
          "pub Words :: [\"alpha\", \"beta\"];\npub Seed :: "
          "Choice.Amount(Base);\n",
          0 },
        { "data/b.chibi", "package data\npub Make :: (value: int) -> Pair { return {value: value + Base, tag: Words[1]}; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Typed", backends[b], 0, 8));
        REQUIRE(run(0, "project:app", "Typed", backends[b], 3, 14));
        const authoring_package *app = package(0, "project:app");
        uint64_t typed = function_id(app, "Typed");
        retained = app->module;
        execution_module_retain(retained);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(call(retained, typed, backends[b], 5, 18));
        execution_module_free(retained);
        retained = NULL;
    }
    return true;
}
TEST_CASE(package_imports_ids_and_results_survive_input_order_and_root_relocation)
{
    const source_package_input files[] = { chain[0], chain[1], chain[2] };
    const source_package_input reverse[] = { chain[2], chain[1], chain[0] };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
        REQUIRE(prepare(1, reverse, 3, backends[b], -1, "/project"));
        REQUIRE(prepare(2, files, 3, backends[b], -1, "/relocated/project"));
        for (int slot = 0; slot < 3; slot++) {
            REQUIRE(valid(slot));
        }
        uint64_t id = function_id(package(0, "project:app"), "Main");
        REQUIRE(id && function_id(package(1, "project:app"), "Main") == id);
        REQUIRE(function_id(package(2, "project:app"), "Main") == id);
        for (int slot = 0; slot < 3; slot++) {
            REQUIRE(run(slot, "project:app", "Main", backends[b], 3, 11));
        }
    }
    return true;
}
TEST_CASE(package_imports_incremental_reuse_follows_all_edges_and_retains_old_valid_generation)
{
    static const char *identities[] = { "project:app",
                                        "project:middle",
                                        "project:math",
                                        "project:blank",
                                        "project:other" };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, chain, 5, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(prepare(1, chain, 5, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 5);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, identities[0], "Main", backends[b], 3, 11));
        source_package_input changed[5];
        memcpy(changed, chain, sizeof changed);
        changed[0].text = "package app\nimport middle \"../middle\"\nMain :: "
                          "(value: int) -> int { return middle.Value(value) "
                          "+ 5; }\n";
        REQUIRE(prepare(0, changed, 5, backends[b], 1, "/project"));
        REQUIRE(valid(0) && authoring_packages_reused(owners[0]) == 4);
        REQUIRE(
            package(0, identities[0])->generation != package(1, identities[0])->generation
        );
        REQUIRE(run(0, identities[0], "Main", backends[b], 3, 15));
        changed[2].text = "package numbers\npub Value :: (value: int) -> int { "
                          "return value + 4; }\n";
        REQUIRE(prepare(2, changed, 5, backends[b], 0, "/project"));
        REQUIRE(valid(2) && authoring_packages_reused(owners[2]) == 1);
        for (int i = 0; i < 4; i++) {
            REQUIRE(
                package(2, identities[i])->generation !=
                package(0, identities[i])->generation
            );
        }
        REQUIRE(
            package(2, identities[4])->generation == package(0, identities[4])->generation
        );
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        authoring_packages_free(owners[1]);
        owners[1] = NULL;
        REQUIRE(run(2, identities[0], "Main", backends[b], 3, 19));
        changed[2].text = "package numbers\npub Value :: (value: int) -> int { "
                          "return missing; }\n";
        REQUIRE(prepare(3, changed, 5, backends[b], 2, "/project"));
        REQUIRE(!authoring_packages_valid(owners[3]));
        REQUIRE(authoring_packages_reused(owners[3]) == 1);
        for (int i = 0; i < 4; i++) {
            const authoring_package *failed = package(3, identities[i]);
            REQUIRE(failed && !failed->valid && failed->diagnostic_count > 0);
            REQUIRE(!failed->module && !failed->exports);
            REQUIRE(failed->generation != package(2, identities[i])->generation);
        }
        REQUIRE(
            package(3, identities[4])->generation == package(2, identities[4])->generation
        );
        REQUIRE(run(2, identities[0], "Main", backends[b], 3, 19));

        REQUIRE(prepare(0, changed, 5, backends[b], 3, "/project"));
        REQUIRE(!authoring_packages_valid(owners[0]));
        REQUIRE(authoring_packages_reused(owners[0]) == 1);
        for (int i = 0; i < 4; i++) {
            const authoring_package *failed = package(0, identities[i]);
            REQUIRE(failed && !failed->valid && failed->diagnostic_count > 0);
            REQUIRE(!failed->module && !failed->exports);
            REQUIRE(failed->generation != package(3, identities[i])->generation);
        }
        REQUIRE(
            package(0, identities[4])->generation == package(3, identities[4])->generation
        );

        const authoring_package *old = package(2, identities[0]);
        uint64_t main = function_id(old, "Main");
        retained = old->module;
        execution_module_retain(retained);
        authoring_packages_free(owners[2]);
        owners[2] = NULL;
        authoring_packages_free(owners[3]);
        owners[3] = NULL;
        changed[2].text = "package numbers\npub Value :: (value: int) -> int { "
                          "return value + 4; }\n";
        REQUIRE(prepare(1, changed, 5, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 1);
        for (int i = 0; i < 4; i++) {
            REQUIRE(
                package(1, identities[i])->generation !=
                package(0, identities[i])->generation
            );
        }
        REQUIRE(
            package(1, identities[4])->generation == package(0, identities[4])->generation
        );
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, identities[0], "Main", backends[b], 3, 19));
        REQUIRE(call(retained, main, backends[b], 3, 19));
        execution_module_free(retained);
        retained = NULL;
    }
    return true;
}
TEST_CASE(package_imports_cached_closure_tracks_sibling_membership_and_paths)
{
    static const char *identities[] = { "project:app",
                                        "project:middle",
                                        "project:math",
                                        "project:other" };
    const source_package_input files[] = { chain[0], chain[1], chain[2], chain[4] };
    const source_package_input reverse[] = { files[3], files[2], files[1], files[0] };
    source_package_input expanded[] = {
        files[0],
        files[1],
        files[2],
        files[3],
        { "math/extra.chibi", "package numbers\nExtra :: (value: int) -> int { return value + 40; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        expanded[4].path = "math/extra.chibi";
        REQUIRE(prepare(0, files, 4, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(prepare(1, reverse, 4, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 4);
        for (int i = 0; i < 4; i++) {
            REQUIRE(
                package(1, identities[i])->generation ==
                package(0, identities[i])->generation
            );
        }
        REQUIRE(run(1, identities[0], "Main", backends[b], 3, 11));

        REQUIRE(prepare(2, expanded, 5, backends[b], 1, "/project"));
        REQUIRE(valid(2) && authoring_packages_reused(owners[2]) == 1);
        for (int i = 0; i < 3; i++) {
            REQUIRE(
                package(2, identities[i])->generation !=
                package(1, identities[i])->generation
            );
        }
        REQUIRE(
            package(2, identities[3])->generation == package(1, identities[3])->generation
        );
        REQUIRE(run(2, identities[0], "Main", backends[b], 3, 11));
        REQUIRE(run(2, identities[2], "Extra", backends[b], 3, 43));

        expanded[4].path = "math/extra-renamed.chibi";
        REQUIRE(prepare(3, expanded, 5, backends[b], 2, "/project"));
        REQUIRE(valid(3) && authoring_packages_reused(owners[3]) == 1);
        for (int i = 0; i < 3; i++) {
            REQUIRE(
                package(3, identities[i])->generation !=
                package(2, identities[i])->generation
            );
        }
        REQUIRE(
            package(3, identities[3])->generation == package(2, identities[3])->generation
        );
        REQUIRE(run(3, identities[0], "Main", backends[b], 3, 11));

        REQUIRE(prepare(0, files, 4, backends[b], 3, "/project"));
        REQUIRE(valid(0) && authoring_packages_reused(owners[0]) == 1);
        for (int i = 0; i < 3; i++) {
            REQUIRE(
                package(0, identities[i])->generation !=
                package(3, identities[i])->generation
            );
        }
        REQUIRE(
            package(0, identities[3])->generation == package(3, identities[3])->generation
        );
        REQUIRE(!function_id(package(0, identities[2]), "Extra"));
        for (int i = 1; i < MAX_OWNERS; i++) {
            authoring_packages_free(owners[i]);
            owners[i] = NULL;
        }
        REQUIRE(run(0, identities[0], "Main", backends[b], 3, 11));
    }
    return true;
}
TEST_CASE(package_imports_export_generic_functions)
{
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport math \"../math\"\nMain :: (value: int) -> int { return math.Twice(value); }\n", 0 },
        { "math/a.chibi",
          "package numbers\npub Identity :: (value: $T) -> T { return value; "
          "}\npub Twice :: (value: int) -> int { return Identity(value) * 2; "
          "}\n",
          0 },
    };
    const source_package_input direct[] = {
        { "app/a.chibi", "package app\nimport math \"../math\"\nMain :: (value: int) -> int { return math.Identity(value); }\n", 0 },
        files[1],
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        const authoring_package *math = package(0, "project:math");
        REQUIRE(math && math->exports);
        bool exported = false;
        for (int i = 0; i < math->exports->member_count; i++) {
            exported |= !strcmp(math->exports->members[i].name, "Identity");
        }
        REQUIRE(exported);
        REQUIRE(run(0, "project:app", "Main", backends[b], 7, 14));
        REQUIRE(prepare(1, direct, 2, backends[b], -1, "/project"));
        REQUIRE(valid(1));
        REQUIRE(run(1, "project:app", "Main", backends[b], 7, 7));
    }
    return true;
}
TEST_CASE(package_imports_generic_definition_scope_and_transitive_requests)
{
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport api \"../api\"\nHelper :: (value: int) -> int { return 999; }\nMain :: (value: int) -> int { return api.Twice(value) + api.Twice(value); }\n", 0 },
        { "api/a.chibi", "package api\nimport impl \"../impl\"\nHelper :: (value: int) -> int { return value + 1; }\npub Twice :: (value: $T) -> T { return impl.Double(Helper(value)); }\n", 0 },
        { "impl/a.chibi", "package impl\npub Double :: (value: $T) -> T { return value * 2; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 4, 20));
        REQUIRE(package(0, "project:api")->analysis->specialization_count == 1);
        REQUIRE(package(0, "project:impl")->analysis->specialization_count == 1);
        REQUIRE(prepare(1, files, 3, backends[b], 0, "/project"));
        REQUIRE(valid(1));
        REQUIRE(authoring_packages_reused(owners[1]) == 3);
        REQUIRE(run(1, "project:app", "Main", backends[b], 7, 32));
    }
    return true;
}
TEST_CASE(package_imports_callable_arguments_use_the_definition_environment)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\nHidden :: (value: int) -> int { "
          "return -999; }\nPositive :: (value: int) -> bool { return value > "
          "0; }\nMain :: (value: int) -> int { if (api.Any([value, -1], "
          "Positive) && api.Any([value, -1], (item: int) => item > 0)) { "
          "return value; } return -1; }\n",
          0 },
        { "api/a.chibi",
          "package api\nimport adjust \"../adjust\"\nimport own "
          "\"../api\"\nBias :: 1;\nHidden :: "
          "(value: int) -> int { return value + Bias; }\nTest :: (value: $T, "
          "predicate: fn(item: T) -> bool) -> bool { return predicate(value); "
          "}\npub Any :: (values: $T[], predicate: fn(item: T) -> bool) -> "
          "bool { for (value in values) { if "
          "(Test(adjust.Value(own.Hidden(value) + Bias - 1), predicate)) { "
          "return true; } } "
          "return false; }\n",
          0 },
        { "adjust/a.chibi", "package adjust\npub Value :: (value: int) -> int { return value - 1; }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 5, 5));
        REQUIRE(run(0, "project:app", "Main", backends[b], -5, -1));
        REQUIRE(prepare(1, files, 3, backends[b], 0, "/project"));
        REQUIRE(valid(1));
        REQUIRE(authoring_packages_reused(owners[1]) == 3);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, "project:app", "Main", backends[b], 8, 8));
    }
    return true;
}
TEST_CASE(package_imports_private_aggregate_constants_survive_callable_generation_teardown)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\nMain :: (value: int) -> int { "
          "return api.Read(value, (item: int) => item); }\n",
          0 },
        { "api/a.chibi",
          "package api\nHidden :: [2, 4];\npub Read :: (value: int, "
          "transform: fn(item: int) -> int) -> int { values := Hidden; "
          "values[0] = 9; return transform(value) + Hidden[0] + values[1]; }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 3, 9));
        REQUIRE(run(0, "project:app", "Main", backends[b], 5, 11));
        REQUIRE(prepare(1, files, 2, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 2);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, "project:app", "Main", backends[b], 7, 13));
        const authoring_package *app = package(1, "project:app");
        uint64_t id = function_id(app, "Main");
        retained = app->module;
        execution_module_retain(retained);
        authoring_packages_free(owners[1]);
        owners[1] = NULL;
        REQUIRE(call(retained, id, backends[b], 9, 15));
        execution_module_free(retained);
        retained = NULL;
    }
    return true;
}
TEST_CASE(package_imports_generic_views_preserve_foreign_nominals_and_pointer_identity)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\nRow :: struct { value: int; }\n"
          "Read :: (row: Row) -> int { return row.value; }\n"
          "Main :: (value: int) -> int { values: int[] = [value]; "
          "pointer := &values; window := pointer.view(0..<1); scalar := 0; "
          "if window { scalar = api.First(window); } "
          "row: Row = {value: value + 1}; rows: Row[] = [row]; owner := &rows; "
          "selected := owner.view(0..<1); if selected { "
          "picked := api.First(selected); before := api.Read(selected, Read); "
          "rows[0].value += 1; if selected { "
          "return scalar + picked.value + before + api.Read(selected, Read); } "
          "} "
          "return -1; }\n",
          0 },
        { "api/a.chibi",
          "package api\npub First :: (values: view $T[]) -> T { return "
          "values[0]; }\n"
          "pub Read :: (values: view $T[], read: fn(value: T) -> int) -> int { "
          "return read(values[0]); }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 3, 16));
        REQUIRE(prepare(1, files, 2, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 2);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, "project:app", "Main", backends[b], 5, 24));
    }
    return true;
}
TEST_CASE(package_imports_optional_binders_preserve_array_fallback_values)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\n"
          "Keep :: (value: int[]) -> int[] { return value; }\n"
          "Plain :: (value: int) -> int { values: int[] = [value, 4]; "
          "fallback: int[] = [9]; present := api.OrElse(values[0..<1], "
          "fallback); "
          "absent := api.OrElse(values[0..<3], fallback); absent[0] = 7; "
          "return present[0] * 100 + fallback[0] * 10 + absent[0]; }\n"
          "Callback :: (value: int) -> int { values: int[] = [value, 4]; "
          "fallback: int[] = [9]; present := api.With(values[0..<1], fallback, "
          "Keep); "
          "absent := api.With(values[0..<3], fallback, Keep); absent[0] = 7; "
          "return present[0] * 100 + fallback[0] * 10 + absent[0]; }\n"
          "Main :: (value: int) -> int { return Plain(value) * 1000 + "
          "Callback(value); }\n",
          0 },
        { "api/a.chibi",
          "package api\npub OrElse :: (value: $T?, fallback: T) -> T { "
          "return value ?? fallback; }\n"
          "pub With :: (value: $T?, fallback: T, transform: fn(value: T) -> T) "
          "-> T { "
          "return transform(value ?? fallback); }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 3, 397397));
        REQUIRE(prepare(1, files, 2, backends[b], 0, "/project"));
        REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 2);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, "project:app", "Main", backends[b], 5, 597597));
    }
    return true;
}
TEST_CASE(package_imports_pointer_callbacks_preserve_shadowed_scoped_inputs)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\n"
          "Bump :: (value: ^int) -> int { value^ += 2; return value^; }\n"
          "Held :: (value: ^int) -> int { pointer := value; pointer^ += 2; "
          "return pointer^; }\n"
          "Observe :: (value: ^int) -> int { other := 7; observed := "
          "api.Apply(&other, (value: ^int) -> int { value^ += 2; return "
          "value^; }); "
          "return value^ * 100 + observed * 10 + other; }\n"
          "Main :: (value: int) -> int { original := value; observed := "
          "Observe(&original); return observed * 10 + original; }\n"
          "Named :: (value: int) -> int { original := value; observed := "
          "api.Apply(&original, Bump); return original * 10 + observed; }\n"
          "Managed :: (value: int) -> int { original := value; observed := "
          "api.Apply(&original, Held); return original * 10 + observed; }\n",
          0 },
        { "api/a.chibi",
          "package api\npub Apply :: (input: ^int, function: fn(value: ^int) "
          "-> int) "
          "-> int { return function(input); }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 3, 3993));
        REQUIRE(run(0, "project:app", "Named", backends[b], 3, 55));
        REQUIRE(run(0, "project:app", "Managed", backends[b], 3, 55));
    }
    return true;
}
TEST_CASE(package_imports_generic_nominals_and_definition_edits_do_not_alias)
{
    source_package_input files[] = {
        { "app/a.chibi", "package app\nimport api \"../api\"\nimport other \"../other\"\nRow :: struct { value: int; }\nMain :: (value: int) -> int { row: Row = {value: value}; item := api.Identity(row); second := api.Identity(other.Make(value)); return item.value + second.value; }\n", 0 },
        { "api/a.chibi",
          "package api\npub Identity :: (value: $T) -> T { return value; }\n",
          0 },
        { "other/a.chibi",
          "package other\npub Row :: struct { value: int; }\npub Make :: "
          "(value: int) -> Row { return {value: value + 1}; }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 3, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 5, 11));
        files[1].text = "package api\npub Identity :: (value: $T) -> T { "
                        "return value; }\nPrivate :: 2;\n";
        REQUIRE(prepare(1, files, 3, backends[b], 0, "/project"));
        REQUIRE(valid(1));
        REQUIRE(
            package(0, "project:app")->generation != package(1, "project:app")->generation
        );
        REQUIRE(run(1, "project:app", "Main", backends[b], 7, 15));
        REQUIRE(run(0, "project:app", "Main", backends[b], 8, 17));
        files[1].text = "package api\npub Identity :: (value: $T) -> T { "
                        "return value; }\n";
    }
    return true;
}
TEST_CASE(package_imports_callable_definition_errors_point_to_the_importing_call)
{
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport api \"../api\"\nMissing :: (value: int) -> bool "
          "{ return true; }\nPositive :: (value: int) -> bool { return value > "
          "0; }\nMain :: (value: int) -> int { if (api.Check(value, Positive)) "
          "{ return 1; } return 0; }\n",
          0 },
        { "api/a.chibi", "package api\npub Check :: (value: $T, predicate: fn(item: T) -> bool) -> bool { return Missing(value); }\n", 0 },
    };
    REQUIRE(prepare(0, files, 2, backends[0], -1, "/project"));
    REQUIRE(!authoring_packages_valid(owners[0]));
    const authoring_package *app = package(0, "project:app");
    REQUIRE(app && !app->valid);
    bool definition = false;
    for (int i = 0; i < app->diagnostic_count; i++) {
        const script_diagnostic *diagnostic = &app->diagnostics[i];
        if (strstr(diagnostic->message, "api/a.chibi") &&
            strstr(diagnostic->message, "Missing")) {
            REQUIRE(diagnostic->range.document == 0 && diagnostic->range.line == 5);
            definition = true;
        }
    }
    REQUIRE(definition);
    return true;
}
TEST_CASE(package_imports_nested_specialization_keeps_stable_definition_owners)
{
    char a_source[1024] = "package a\n", b_source[1024] = "package b\n",
         c_source[1024] = "package c\n";
    char *sources[] = { a_source, b_source, c_source };
    const int aliases[] = { 6, 18, 27 };
    for (int package = 0; package < 3; package++) {
        for (int i = 0; i < aliases[package]; i++) {
            size_t before = strlen(sources[package]);
            int bytes = snprintf(
                sources[package] + before,
                sizeof a_source - before,
                "import alias%d \"../%s\"\n",
                i,
                package ? "dep" : "a"
            );
            REQUIRE(bytes > 0 && (size_t)bytes < sizeof a_source - before);
        }
        size_t before = strlen(sources[package]);
        int bytes = snprintf(
            sources[package] + before,
            sizeof a_source - before,
            "pub Apply :: (value: $T, transform: fn(item: T) -> T) -> T { "
            "return transform(value); }\n%s",
            package ? "" : "pub Identity :: (value: $T) -> T { return value; }\npub Consume :: (value: int) -> int { return value; }\n"
        );
        REQUIRE(bytes > 0 && (size_t)bytes < sizeof a_source - before);
    }
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport a \"../a\"\nimport b \"../b\"\nimport c "
          "\"../c\"\nRow :: struct { value: int; }\nSame :: (value: int) -> "
          "int { return value; }\nTwice :: (value: int) -> int { return value "
          "* 2; }\nKeep :: (value: Row) -> Row { return value; }\nMain :: "
          "(value: int) -> int { warm := a.Apply(value, Same); scaled := "
          "a.Consume(b.Apply(value, Twice)); row: Row = {value: scaled}; "
          "result := a.Identity(c.Apply(row, Keep)); return result.value + "
          "warm; }\n",
          0 },
        { "a/a.chibi", a_source, 0 },
        { "b/a.chibi", b_source, 0 },
        { "c/a.chibi", c_source, 0 },
        { "dep/a.chibi",
          "package dep\npub F0 :: (value: int) -> int { return value; }\npub "
          "F1 :: (value: int) -> int { return value; }\npub F2 :: (value: int) "
          "-> int { return value; }\npub F3 :: (value: int) -> int { return "
          "value; }\npub F4 :: (value: int) -> int { return value; }\npub F5 "
          ":: (value: int) -> int { return value; }\npub F6 :: (value: int) -> "
          "int { return value; }\npub F7 :: (value: int) -> int { return "
          "value; }\npub F8 :: (value: int) -> int { return value; }\npub F9 "
          ":: (value: int) -> int { return value; }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 5, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 7, 21));
        REQUIRE(run(0, "project:app", "Main", backends[b], -4, -12));
    }
    return true;
}
/* Imported definitions grow type and callable tables during outer expressions. */
static bool growing_environment_sources(char *records, size_t record_capacity, char *functions, size_t function_capacity)
{
    int record_size = snprintf(records, record_capacity, "package settings\npub Identity :: (value: $T) -> T { return value; }\n");
    int function_size = snprintf(functions, function_capacity, "package settings\n");
    REQUIRE(record_size > 0 && (size_t)record_size < record_capacity);
    REQUIRE(function_size > 0 && (size_t)function_size < function_capacity);
    for (int i = 0; i < 20; i++) {
        int added = snprintf(
            records + record_size,
            record_capacity - (size_t)record_size,
            "Private%d :: struct { value: int; }\n",
            i
        );
        REQUIRE(added > 0 && (size_t)added < record_capacity - (size_t)record_size);
        record_size += added;
        added = snprintf(
            functions + function_size,
            function_capacity - (size_t)function_size,
            "F%d :: (value: int) -> int { return value; }\n",
            i
        );
        REQUIRE(added > 0 && (size_t)added < function_capacity - (size_t)function_size);
        function_size += added;
    }
    return true;
}
TEST_CASE(package_imports_structured_arguments_accept_callable_algorithms)
{
    char records[1024], functions[1024];
    REQUIRE(growing_environment_sources(records, sizeof records, functions, sizeof functions));
    const source_package_input files[] = {
        { "app/a.chibi",
          "package app\nimport rows \"../rows\"\nimport algorithms "
          "\"../algorithms\"\n"
          "Double :: (value: int) -> int { return value * 2; }\n"
          "Main :: (value: int) -> int { return rows.Read(row.value: "
          "algorithms.Apply(value, Double)); }\n",
          0 },
        { "rows/a.chibi",
          "package rows\npub Row :: struct { value: int; }\n"
          "pub Read :: (row: Row) -> int { return row.value; }\n",
          0 },
        { "algorithms/a.chibi",
          "package algorithms\nimport settings \"../settings\"\n"
          "pub Apply :: (value: int, transform: fn(value: int) -> int) -> int "
          "{ return transform(value); }\n",
          0 },
        { "settings/a.chibi", records, 0 },
        { "settings/b.chibi", functions, 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, files, 5, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 7, 14));
        REQUIRE(run(0, "project:app", "Main", backends[b], -4, -8));
    }
    return true;
}
TEST_CASE(package_imports_authored_structured_arguments_and_literals_keep_outer_types)
{
    char records[1024], functions[1024], authored[1024];
    REQUIRE(growing_environment_sources(records, sizeof records, functions, sizeof functions));
    int length = snprintf(
        authored,
        sizeof authored,
        "package app\nimport rows \"../rows\"\nimport algorithms "
        "\"../algorithms\"\n"
        "Read :: (row: rows.Row, suffix: int) -> int { return row.value + "
        "row.bonus + suffix; }\n"
        "Main :: (value: int) -> int { return Read(row.value: "
    );
    REQUIRE(length > 0 && (size_t)length < sizeof authored);
    for (int i = 0; i < 8; i++) {
        int added = snprintf(
            authored + length,
            sizeof authored - (size_t)length,
            "%salgorithms.Apply(value, (x: int) => x * 2)",
            i ? " + " : ""
        );
        REQUIRE(added > 0 && (size_t)added < sizeof authored - (size_t)length);
        length += added;
    }
    int added = snprintf(authored + length, sizeof authored - (size_t)length, ", row.bonus: value + 5, suffix: value + 3); }\n");
    REQUIRE(added > 0 && (size_t)added < sizeof authored - (size_t)length);
    const char *consumers[] = {
        authored,
        "package app\nimport rows \"../rows\"\nimport algorithms "
        "\"../algorithms\"\n"
        "Main :: (value: int) -> int { row: rows.Row = {value: "
        "algorithms.Apply(value, (x: int) => x * 2), bonus: value + 5}; return "
        "rows.Read(row); }\n",
    };
    for (int sample = 0; sample < 2; sample++) {
        const source_package_input files[] = {
            { "app/a.chibi", consumers[sample], 0 },
            { "rows/a.chibi", "package rows\npub Row :: struct { value: int; bonus: int; }\npub Read :: (row: Row) -> int { return row.value + row.bonus; }\n", 0 },
            { "algorithms/a.chibi", "package algorithms\nimport settings \"../settings\"\npub Apply :: (value: int, transform: fn(value: int) -> int) -> int { return transform(value); }\n", 0 },
            { "settings/a.chibi", records, 0 },
            { "settings/b.chibi", functions, 0 },
        };
        for (int b = 0; b < 2; b++) {
            REQUIRE(prepare(0, files, 5, backends[b], -1, "/project"));
            REQUIRE(valid(0));
            REQUIRE(run(0, "project:app", "Main", backends[b], 7, sample ? 26 : 134));
            REQUIRE(run(0, "project:app", "Main", backends[b], -4, sample ? -7 : -64));
        }
    }
    return true;
}
static int overload_integer_calls, overload_text_calls;
static const char *overload_integer(arena *, const pin_value *arguments, int, pin_value *out)
{
    overload_integer_calls++;
    out[0] = (pin_value){ .type = PIN_I32, .i = arguments[0].i + 11 };
    return NULL;
}
static const char *overload_text(arena *, const pin_value *, int, pin_value *out)
{
    overload_text_calls++;
    out[0] = (pin_value){ .type = PIN_I32, .i = -777 };
    return NULL;
}
static bool overload_binding(void *, uint64_t id, script_lower_host *out)
{
    static const node_host_call integer = { .value = overload_integer },
                                text = { .value = overload_text };
    if (id != 730 && id != 731) {
        return false;
    }
    *out = (script_lower_host){ .call = id == 730 ? &integer : &text };
    return true;
}
TEST_CASE(package_imports_overload_arguments_accept_growing_definition_environments)
{
    char records[1024], functions[1024];
    REQUIRE(growing_environment_sources(records, sizeof records, functions, sizeof functions));
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport algorithms \"../algorithms\"\nMain :: (value: int) -> int { return Pick(algorithms.Apply(value, (x: int) => x * 2)); }\n", 0 },
        { "algorithms/a.chibi", "package algorithms\nimport settings \"../settings\"\npub Apply :: (value: int, transform: fn(value: int) -> int) -> int { return transform(value); }\n", 0 },
        { "settings/a.chibi", records, 0 },
        { "settings/b.chibi", functions, 0 },
    };
    static const script_catalog_parameter integer = { .name = "value",
                                                      .type = { .storage = PIN_I32 } },
                                          text = { .name = "value",
                                                   .type = { .storage = PIN_STR } };
    static const script_catalog_capability overloads[] = {
        { .id = 730,
          .name = "Pick",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &integer,
          .parameter_count = 1,
          .results = &integer,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_HOST },
        { .id = 731,
          .name = "Pick",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &text,
          .parameter_count = 1,
          .results = &integer,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_HOST },
    };
    const script_catalog catalog = { .capabilities = overloads, .capability_count = 2 };
    const script_lower_bindings selected = { .host = overload_binding,
                                             .generation = 1 };
    for (int b = 0; b < 2; b++) {
        overload_integer_calls = overload_text_calls = 0;
        REQUIRE(prepare_catalog(0, files, 4, backends[b], -1, "/project", &catalog, &selected));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 7, 25));
        REQUIRE(run(0, "project:app", "Main", backends[b], -4, 3));
        REQUIRE(overload_integer_calls == 2 && overload_text_calls == 0);
    }
    return true;
}
TEST_CASE(package_imports_event_receivers_keep_sender_contracts_while_catalogs_grow)
{
    char records[1024], functions[1024];
    REQUIRE(growing_environment_sources(records, sizeof records, functions, sizeof functions));
    const source_package_input files[] = {
        { "algorithms/a.chibi", "package algorithms\nimport settings \"../settings\"\npub Apply :: (value: $T, transform: fn(value: T) -> T) -> T { return transform(value); }\n", 0 },
        { "settings/a.chibi", records, 0 },
        { "settings/b.chibi", functions, 0 },
    };
    static const script_catalog_parameter receiver = { .name = "Entity",
                                                       .type = { .storage = PIN_HANDLE,
                                                                 .nominal = 40 } },
                                          payload = { .name = "Amount",
                                                      .type = { .storage = PIN_I32 } };
    static const script_catalog_capability capabilities[] = {
        { .id = 60,
          .name = "Send Event",
          .kind = SCRIPT_CAPABILITY_ACTION,
          .parameters = &receiver,
          .parameter_count = 1,
          .event_sender = true },
        { .id = 61,
          .name = "Damage",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &payload,
          .parameter_count = 1 },
    };
    const script_catalog catalog = { .capabilities = capabilities,
                                     .capability_count = 2,
                                     .entity_type = 40 };
    REQUIRE(prepare_catalog(0, files, 3, backends[0], -1, "/project", &catalog, &bindings));
    REQUIRE(valid(0));
    direct_syntax = script_parse("package app\nimport algorithms \"../algorithms\"\nSend :: (other: entity) { emit (algorithms.Apply(other, (item: entity) => item)).Damage(Amount: 3); }\n");
    REQUIRE(!direct_syntax->diagnostic_count);
    const script_package_document document = { "/project/app/a.chibi",
                                               "project:app/a.chibi",
                                               direct_syntax };
    int start = (int)(strstr(direct_syntax->text, "import algorithms") -
                      direct_syntax->text);
    script_range range = { .start = start,
                           .end = start + (int)strlen("import algorithms \"../algorithms\""),
                           .line = 2,
                           .column = 1,
                           .document = 0 };
    const script_package_import import = {
        .document = 0,
        .range = range,
        .alias = "algorithms",
        .identity = "project:algorithms",
        .exports = package(0, "project:algorithms")->exports
    };
    const script_package_options options = { .documents = &document,
                                             .document_count = 1,
                                             .identity = "project:app",
                                             .native = &catalog,
                                             .imports = &import,
                                             .import_count = 1 };
    direct_analysis = script_analyze_package(&options);
    REQUIRE(direct_analysis->valid);
    bool sent = false;
    for (int i = 0; i < direct_analysis->expression_count; i++) {
        const script_expression *expression = &direct_analysis->expressions[i];
        if (expression->kind == SCRIPT_EXPR_CALL && expression->capability == 60) {
            REQUIRE(expression->member == 61 && expression->argument_count == 2);
            sent = true;
        }
    }
    REQUIRE(sent);
    return true;
}
TEST_CASE(package_imports_private_templates_stay_private)
{
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nimport api \"../api\"\nMain :: (value: int) -> int { return api.Hidden(value); }\n", 0 },
        { "api/a.chibi", "package api\nHidden :: (value: $T) -> T { return value; }\n", 0 },
    };
    REQUIRE(prepare(0, files, 2, backends[0], -1, "/project"));
    REQUIRE(!authoring_packages_valid(owners[0]));
    const authoring_package *app = package(0, "project:app");
    REQUIRE(app && app->diagnostic_count > 0);
    bool private = false;
    for (int i = 0; i < app->diagnostic_count; i++) {
        private |= strstr(app->diagnostics[i].message, "private") != NULL;
    }
    REQUIRE(private);
    return true;
}
/* Other packages see only pub declarations. A private record still reaches
 * them as a value of a pub function, though they cannot name it. */
TEST_CASE(package_imports_see_only_pub_declarations)
{
    static const char provider
        [] = "package lib\n"
             "pub Size: int : 3;\n"
             "Hidden :: 4;\n"
             "Secret :: struct {\n    x: int;\n}\n"
             "pub Make :: (x: int) -> Secret {\n    return {x: x};\n}\n"
             "Twice :: (value: int) -> int {\n    return value * 2;\n}\n"
             "pub Value :: (value: int) -> int {\n"
             "    return Twice(value) + Hidden;\n}\n";
    static const struct
    {
        const char *body, *message;
    } invalid[] = {
        { "return lib.Twice(value);", "'lib.Twice' is private: declare it pub to use it from another package" },
        { "return lib.Hidden;", "'lib.Hidden' is private: declare it pub to use it from another package" },
        { "secret: lib.Secret = lib.Make(value);\n    return secret.x;", "'lib.Secret' is private: declare it pub to use it from another package" },
    };
    char text[512];
    for (int b = 0; b < 2; b++) {
        snprintf(text, sizeof text, "package app\nimport lib \"../lib\"\nMain :: (value: int) -> int {\n    return lib.Value(value) + lib.Size + lib.Make(value).x;\n}\n");
        const source_package_input files[] = { { "app/a.chibi", text, 0 },
                                               { "lib/a.chibi", provider, 0 } };
        REQUIRE(prepare(0, files, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 2, 13));
        const authoring_package *lib = package(0, "project:lib");
        REQUIRE(lib->exports->member_count == 3 && lib->exports->private_count == 3);
        for (int sample = 0; sample < 3; sample++) {
            snprintf(
                text,
                sizeof text,
                "package app\nimport lib \"../lib\"\nMain :: (value: int) -> "
                "int {\n    %s\n}\n",
                invalid[sample].body
            );
            REQUIRE(prepare(1, files, 2, backends[b], -1, "/project"));
            const authoring_package *app = package(1, "project:app");
            REQUIRE(app && !app->valid && app->diagnostic_count > 0);
            REQUIRE(!strcmp(app->diagnostics[0].message, invalid[sample].message));
        }
    }
    return true;
}
TEST_CASE(package_import_catalog_snapshot_owns_union_payload_defaults)
{
    char *label = strdup("owned label"), *first = strdup("first"),
         *second = strdup("second");
    pin_value items[] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = first,
                                       .length = (uint32_t)strlen(first) } },
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = second,
                                       .length = (uint32_t)strlen(second) } }
    };
    pin_array array = { .element_type = PIN_STR, .count = 2, .capacity = 2, .items = items };
    script_catalog_field fields[] = {
        { .id = 10,
          .name = "label",
          .type = { .storage = PIN_STR },
          .has_default = true,
          .default_value = { .type = PIN_STR,
                             .text = &(
                                 const pin_string
                             ){ .bytes = label, .length = (uint32_t)strlen(label) } } },
        { .id = 11,
          .name = "words",
          .type = { .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY },
          .has_default = true,
          .default_value = { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &array } },
    };
    script_catalog_variant variant = {
        .tag = 1,
        .name = "Payload",
        .type = { .storage = PIN_UNION, .nominal = 1, .flags = SCRIPT_TYPE_UNION },
        .tagged = true,
        .fields = fields,
        .field_count = 2
    };
    script_catalog_type type = {
        .id = 1,
        .name = "Choice",
        .type = { .storage = PIN_UNION, .nominal = 1, .flags = SCRIPT_TYPE_UNION },
        .variants = &variant,
        .variant_count = 1
    };
    script_catalog source = { .types = &type, .type_count = 1 }, snapshot;
    bool copied = script_catalog_snapshot(&metadata_storage, &source, &snapshot);
    memset(label, 'x', strlen(label));
    memset(first, 'x', strlen(first));
    memset(second, 'x', strlen(second));
    free(label);
    free(first);
    free(second);
    memset(items, 0, sizeof items);
    memset(fields, 0, sizeof fields);
    memset(&array, 0, sizeof array);
    REQUIRE(copied && snapshot.type_count == 1 && snapshot.types[0].variant_count == 1);
    const script_catalog_variant *owned = &snapshot.types[0].variants[0];
    REQUIRE(
        owned->field_count == 2 && owned->fields[0].has_default &&
        owned->fields[1].has_default
    );
    REQUIRE(owned->fields[0].default_value.type == PIN_STR);
    REQUIRE(arena_contains(&metadata_storage, owned->fields[0].default_value.text));
    REQUIRE(!strcmp(pin_text(owned->fields[0].default_value).bytes, "owned label"));
    const pin_array *words = owned->fields[1].default_value.ptr;
    REQUIRE(words && arena_contains(&metadata_storage, words) && words->count == 2);
    REQUIRE(
        words->element_type == PIN_STR && words->items[0].type == PIN_STR &&
        words->items[1].type == PIN_STR
    );
    REQUIRE(
        arena_contains(&metadata_storage, words->items[0].text) &&
        arena_contains(&metadata_storage, words->items[1].text)
    );
    REQUIRE(
        !strcmp(pin_text(words->items[0]).bytes, "first") &&
        !strcmp(pin_text(words->items[1]).bytes, "second")
    );
    const source_package_input files[] = {
        { "app/a.chibi", "package app\nMain :: (value: int) -> int { return value; }\n", 0 },
    };
    REQUIRE(prepare_catalog(0, files, 1, BEHAVIOR_BYTECODE, -1, "/project", &snapshot, &bindings));
    REQUIRE(valid(0));
    REQUIRE(prepare_catalog(1, files, 1, BEHAVIOR_BYTECODE, 0, "/project", &snapshot, &bindings));
    REQUIRE(valid(1) && authoring_packages_reused(owners[1]) == 1);
    REQUIRE(package(1, "project:app")->generation == package(0, "project:app")->generation);
    script_catalog changed = snapshot;
    script_catalog_type changed_type = snapshot.types[0];
    script_catalog_variant changed_variant = *owned;
    script_catalog_field changed_fields[2];
    memcpy(changed_fields, owned->fields, sizeof changed_fields);
    changed_fields[0].default_value.text = &(
        const pin_string
    ){ .bytes = "changed label", .length = (uint32_t)strlen("changed label") };
    changed_variant.fields = changed_fields;
    changed_type.variants = &changed_variant;
    changed.types = &changed_type;
    REQUIRE(prepare_catalog(2, files, 1, BEHAVIOR_BYTECODE, 1, "/project", &changed, &bindings));
    REQUIRE(valid(2) && authoring_packages_reused(owners[2]) == 0);
    REQUIRE(package(2, "project:app")->generation != package(1, "project:app")->generation);
    return true;
}

static const char *default_probe(arena *, const pin_value *, int, pin_value *out)
{
    default_host_calls++;
    out[0] = (pin_value){ .type = PIN_I32, .i = 99 };
    return NULL;
}
static bool default_probe_binding(void *, uint64_t id, script_lower_host *out)
{
    if (id != 700) {
        return false;
    }
    static const node_host_call call = { .value = default_probe };
    *out = (script_lower_host){ .call = &call };
    return true;
}
TEST_CASE(package_imports_exported_defaults_are_constant_values_without_host_execution)
{
    const source_package_input positive[] = {
        { "app/a.chibi", "package app\nimport lib \"../defaults\"\nMain :: (value: int) -> int { return value + lib.Binary() + lib.Named() + lib.Aggregate(); }\n", 0 },
        { "defaults/a.chibi",
          "package defaults\nBase: int : 7;\nWords :: [\"alpha\", "
          "\"beta\"];\n"
          "pub Binary :: (amount: int = 2 + 3) -> int { return amount; }\n"
          "pub Named :: (amount: int = Base) -> int { return amount; }\n"
          "pub Aggregate :: (words: string[] = Words) -> int { if (words[1] == "
          "\"beta\") { return 9; } return -99; }\n",
          0 },
    };
    static const char *negative[] = {
        "package defaults\npub Needs :: (first: int, second: int = first) -> "
        "int "
        "{ return second; }\n",
        "package defaults\npub Needs :: (amount: int = Probe()) -> int { "
        "return "
        "amount; }\n",
    };
    static const script_catalog_parameter output = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
    static const script_catalog_capability probe = {
        .id = 700,
        .name = "Probe",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .results = &output,
        .result_count = 1,
        .effects = SCRIPT_EFFECT_HOST,
    };
    const script_catalog counted = { .capabilities = &probe, .capability_count = 1 };
    const script_lower_bindings counted_bindings = { .host = default_probe_binding,
                                                     .generation = 1 };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(0, positive, 2, backends[b], -1, "/project"));
        REQUIRE(valid(0));
        REQUIRE(run(0, "project:app", "Main", backends[b], 0, 21));
        for (int i = 0; i < 2; i++) {
            const source_package_input rejected[] = {
                { "app/a.chibi",
                  i == 0 ? "package app\nimport lib \"../defaults\"\nMain :: "
                           "(value: int) -> int { return lib.Needs(value); }\n"
                         : "package app\nimport lib \"../defaults\"\nMain :: "
                           "(value: int) -> int { return lib.Needs(); }\n",
                  0 },
                { "defaults/a.chibi", negative[i], 0 },
            };
            default_host_calls = 0;
            REQUIRE(
                prepare_catalog(0, rejected, 2, backends[b], -1, "/project", &counted, &counted_bindings)
            );
            REQUIRE(!authoring_packages_valid(owners[0]) && !default_host_calls);
            const authoring_package *provider = package(0, "project:defaults");
            REQUIRE(provider && !provider->valid && !provider->module);
            bool diagnosed = false;
            for (int j = 0; j < provider->diagnostic_count; j++) {
                diagnosed |= !strcmp(provider->diagnostics[j].message, "Exported parameter default requires a bounded compile-time value");
            }
            REQUIRE(diagnosed);
        }
    }
    return true;
}

TEST_CASE(package_exports_keep_private_managed_pointee_metadata)
{
    const source_package_input files[] = {
        { "app/main.chibi", "package app\nimport lib \"../lib\"\nMain :: () -> int { return 0; }\n", 0 },
        { "lib/main.chibi", "package lib\nPrivateRecord :: struct { value: int; }\npub Holder :: struct { target: ^PrivateRecord?; }\n", 0 },
    };
    REQUIRE(prepare(0, files, 2, BEHAVIOR_BYTECODE, -1, "/project"));
    REQUIRE(valid(0));
    const authoring_package *lib = package(0, "project:lib");
    REQUIRE(lib && lib->exports && lib->exports->metadata);
    const script_catalog *catalog = lib->exports->metadata;
    const script_catalog_type *holder = NULL;
    for (int i = 0; i < catalog->type_count; i++) {
        const char *name = strrchr(catalog->types[i].name, '.');
        if (!strcmp(name ? name + 1 : catalog->types[i].name, "Holder")) {
            holder = &catalog->types[i];
        }
    }
    REQUIRE(holder && holder->field_count == 1);
    const script_catalog_type
        *pointer = script_catalog_type_by_id(catalog, holder->fields[0].type.nominal);
    REQUIRE(pointer && pointer->type.storage == PIN_POINTER);
    const script_catalog_type
        *pointee = script_catalog_type_by_id(catalog, pointer->element_type.nominal);
    REQUIRE(pointee && pointee->type.storage == PIN_STRUCT && pointee->field_count == 1);
    const char *name = strrchr(pointee->name, '.');
    REQUIRE(!strcmp(name ? name + 1 : pointee->name, "PrivateRecord"));
    REQUIRE(lib->exports->member_count == 1);
    REQUIRE(!strcmp(lib->exports->members[0].name, "Holder"));
    return true;
}
TEST_CASE(package_imports_malformed_public_exports_are_rejected_before_lookup)
{
    static const char *text = "package app\nimport lib \"../lib\"\nMain :: "
                              "() -> int { return lib.F(); }\n";
    direct_syntax = script_parse(text);
    REQUIRE(direct_syntax && !direct_syntax->diagnostic_count);
    const script_package_document document = { "/project/app/a.chibi",
                                               "project:app/a.chibi",
                                               direct_syntax };
    static const script_catalog_parameter output = { .name = "value",
                                                     .type = { .storage = PIN_I32 } };
    static const script_catalog_capability functions[] = {
        { .id = 800,
          .name = "F",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &output,
          .result_count = 1 },
        { .id = 801,
          .name = "G",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .results = &output,
          .result_count = 1 },
    };
    const script_catalog metadata = { .capabilities = functions,
                                      .capability_count = 2 };
    script_package_export members[2];
    script_package_exports exports = { .identity = "project:lib",
                                       .metadata = &metadata,
                                       .members = members };
    int start = (int)(strstr(text, "import lib") - text);
    script_range range = { .start = start,
                           .end = start + (int)strlen("import lib \"../lib\""),
                           .line = 2,
                           .column = 1,
                           .document = 0 };
    const script_package_import import = { .document = 0,
                                           .range = range,
                                           .alias = "lib",
                                           .identity = "project:lib",
                                           .exports = &exports };
    const script_package_options options = { .documents = &document,
                                             .document_count = 1,
                                             .identity = "project:app",
                                             .native = &native,
                                             .imports = &import,
                                             .import_count = 1 };
    /* A valid control, then nine fixed malformed public metadata specimens. */
    for (int sample = 0; sample < 10; sample++) {
        members[0] = (script_package_export){ .name = "F",
                                              .id = 800,
                                              .kind = SCRIPT_PACKAGE_FUNCTION };
        members[1] = (script_package_export){ .name = "F",
                                              .id = 801,
                                              .kind = SCRIPT_PACKAGE_FUNCTION };
        exports.member_count = 1;
        exports.private_count = 0;
        switch (sample) {
            case 1: members[0].name = NULL; break;
            case 2: members[0].name = ""; break;
            case 3: members[0].name = "1invalid"; break;
            case 4: members[0].kind = (script_package_export_kind)99; break;
            case 5: members[0].id = 0; break;
            case 6: members[0].id = 9999; break;
            case 7: exports.member_count = 2; break;
            case 8: exports.member_count = 4097; break;
            case 9: exports.private_count = 1; break;
        }
        script_analysis_free(direct_analysis);
        direct_analysis = script_analyze_package(&options);
        REQUIRE(direct_analysis);
        if (!sample) {
            REQUIRE(direct_analysis->valid && !direct_analysis->diagnostic_count);
            continue;
        }
        REQUIRE(!direct_analysis->valid);
        bool diagnosed = false;
        for (int i = 0; i < direct_analysis->diagnostic_count; i++) {
            const script_diagnostic *error = &direct_analysis->diagnostics[i];
            if (strcmp(error->message, "Invalid package import binding")) {
                continue;
            }
            REQUIRE(
                error->range.document == 0 &&
                error->range.start == range.start && error->range.end == range.end
            );
            diagnosed = true;
        }
        REQUIRE(diagnosed);
    }
    return true;
}

static bool prepare_with_collection(
    int slot,
    source_package_input *files,
    int count,
    behavior_backend backend,
    int previous
)
{
    REQUIRE(count > 0 && count <= MAX_FILES);
    for (int i = 0; i < count; i++) {
        files[i].size = strlen(files[i].text);
    }
    source_package_collection core = { "core", "/sdk/core" };
    source_package_snapshot_options options = { .project_root = "/project",
                                                .files = files,
                                                .file_count = count,
                                                .collections = &core,
                                                .collection_count = 1 };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    REQUIRE(snapshot && !source_package_snapshot_error_at(snapshot));
    authoring_packages_free(owners[slot]);
    owners[slot] = authoring_packages_prepare(
        snapshot,
        &native,
        &bindings,
        backend,
        previous >= 0 ? owners[previous] : NULL
    );
    REQUIRE(owners[slot]);
    return true;
}

TEST_CASE(package_imports_ignore_unused_collection_bodies_but_check_every_project)
{
    source_package_input files[] = {
        { "app/App.chibi", "package app\n@test AppTest :: () {}\npub Main :: (value: int) -> int { return value + 1; }\n", 0 },
        { "/sdk/core/host_only/HostOnly.chibi", "package hosted\npub Read :: () -> int { return unavailable_host(); }\n", 0 },
        { "other/Other.chibi", "package other\n@test OtherTest :: () {}\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare_with_collection(0, files, 3, backends[b], -1));
        REQUIRE(valid(0) && authoring_packages_count(owners[0]) == 2);
        REQUIRE(!package(0, "core:host_only"));
        REQUIRE(run(0, "project:app", "Main", backends[b], 4, 5));
        package_test *found = package_tests_find(owners[0]);
        bool app = false, other = false;
        for (int i = 0; i < acount(found); i++) {
            app |= !strcmp(found[i].package, "app");
            other |= !strcmp(found[i].package, "other");
        }
        bool both = acount(found) == 2 && app && other;
        afree(found);
        REQUIRE(both);
        files[2].text = "package other\npub Broken :: () -> int { return "
                        "missing; }\n";
        REQUIRE(prepare_with_collection(1, files, 3, backends[b], 0));
        REQUIRE(!authoring_packages_valid(owners[1]));
        REQUIRE(package(1, "project:other") && !package(1, "project:other")->valid);
        REQUIRE(
            package(1, "project:app")->generation == package(0, "project:app")->generation
        );
        files[2].text = "package other\n@test OtherTest :: () {}\n";
        cleanup();
    }
    return true;
}

TEST_CASE(package_imports_check_direct_transitive_and_blank_collection_imports)
{
    const char *apps[] = {
        "package app\nimport host \"core:host_only\"\npub Main :: () -> int { "
        "return host.Read(); }\n",
        "package app\nimport middle \"core:middle\"\npub Main :: () -> int { "
        "return middle.Read(); }\n",
        "package app\nimport _ \"core:host_only\"\npub Main :: () -> int { "
        "return 1; }\n",
    };
    source_package_input files[] = {
        { "app/App.chibi", NULL, 0 },
        { "/sdk/core/host_only/HostOnly.chibi", "package hosted\npub Read :: () -> int { return unavailable_host(); }\n", 0 },
        { "/sdk/core/middle/Middle.chibi", "package middle\nimport host \"core:host_only\"\npub Read :: () -> int { return host.Read(); }\n", 0 },
    };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 3; i++) {
            files[0].text = apps[i];
            REQUIRE(prepare_with_collection(0, files, 3, backends[b], -1));
            REQUIRE(!authoring_packages_valid(owners[0]));
            const authoring_package *host = package(0, "core:host_only");
            REQUIRE(host && !host->valid && host->analysis && host->diagnostic_count > 0);
            REQUIRE(strstr(host->diagnostics[0].message, "unavailable_host"));
            REQUIRE(package(0, "project:app") && !package(0, "project:app")->valid);
            REQUIRE(authoring_packages_count(owners[0]) == (i == 1 ? 3 : 2));
            cleanup();
        }
    }
    return true;
}

TEST_CASE(package_imports_collection_only_snapshot_checks_first_input_root)
{
    source_package_input files[] = {
        { "/sdk/core/z_root/Root.chibi",
          "package root\nimport shared \"core:shared\"\npub Main :: (value: "
          "int) -> int { return value + shared.Read(); }\n",
          0 },
        { "/sdk/core/a_unused/Unused.chibi", "package unused\npub Read :: () -> int { return unavailable_host(); }\n", 0 },
        { "/sdk/core/shared/Shared.chibi",
          "package shared\npub Read :: () -> int { return 7; }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare_with_collection(0, files, 3, backends[b], -1));
        REQUIRE(valid(0) && authoring_packages_count(owners[0]) == 2);
        REQUIRE(!package(0, "core:a_unused"));
        REQUIRE(run(0, "core:z_root", "Main", backends[b], 4, 11));
        const char *root = files[0].text;
        files[0]
            .text = "package root\npub Main :: (value: int) -> int { return "
                    "missing; }\n";
        REQUIRE(prepare_with_collection(1, files, 3, backends[b], 0));
        REQUIRE(!authoring_packages_valid(owners[1]));
        REQUIRE(authoring_packages_count(owners[1]) == 1);
        REQUIRE(package(1, "core:z_root") && !package(1, "core:z_root")->valid);
        files[0].text = root;
        cleanup();
    }
    return true;
}

TEST_CASE(package_imports_removing_collection_import_releases_only_new_dependency)
{
    source_package_input files[] = {
        { "app/App.chibi", "package app\nimport host \"core:host_only\"\npub Main :: (value: int) -> int { return host.Read() + value; }\n", 0 },
        { "/sdk/core/host_only/HostOnly.chibi",
          "package hosted\npub Read :: () -> int { return 7; }\n",
          0 },
    };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare_with_collection(0, files, 2, backends[b], -1));
        REQUIRE(valid(0) && authoring_packages_count(owners[0]) == 2);
        const authoring_package *old = package(0, "project:app");
        retained = old->module;
        execution_module_retain(retained);
        uint64_t id = function_id(old, "Main");
        const char *importing = files[0].text;
        files[0].text = "package app\npub Main :: (value: int) -> int { return "
                        "value; }\n";
        REQUIRE(prepare_with_collection(1, files, 2, backends[b], 0));
        REQUIRE(valid(1) && authoring_packages_count(owners[1]) == 1);
        REQUIRE(!package(1, "core:host_only") && authoring_packages_reused(owners[1]) == 0);
        authoring_packages_free(owners[0]);
        owners[0] = NULL;
        REQUIRE(run(1, "project:app", "Main", backends[b], 4, 4));
        REQUIRE(call(retained, id, backends[b], 4, 11));
        files[0].text = importing;
        REQUIRE(prepare_with_collection(2, files, 2, backends[b], 1));
        REQUIRE(valid(2) && authoring_packages_count(owners[2]) == 2);
        REQUIRE(run(2, "project:app", "Main", backends[b], 4, 11));
        cleanup();
    }
    return true;
}

TEST_CASE(package_imports_malformed_definition_environments_are_rejected)
{
    static const char
        text[] = "package app\nimport lib \"../lib\"\nRow :: struct { value: "
                 "int; }\nMain :: (value: int) -> int { row: Row = {value: "
                 "value}; return lib.Identity(row).value; }\n";
    direct_syntax = script_parse(text);
    definition_syntax = script_parse(
        "package lib\npub Identity :: (value: $T) -> T { return value; }\n"
    );
    REQUIRE(!direct_syntax->diagnostic_count && !definition_syntax->diagnostic_count);
    const script_package_document caller = { "/project/app/a.chibi",
                                             "project:app/a.chibi",
                                             direct_syntax };
    script_package_document definition = { "/project/lib/a.chibi",
                                           "project:lib/a.chibi",
                                           definition_syntax };
    const script_template_parameter parameter = { .name = "value",
                                                  .binder = "T",
                                                  .type = { .flags = SCRIPT_TYPE_UNTYPED },
                                                  .binds_type = true };
    const script_catalog_template template = { .id = 800,
                                               .name = "Identity",
                                               .owner = "project:lib",
                                               .kind = SCRIPT_TEMPLATE_FUNCTION,
                                               .parameters = &parameter,
                                               .parameter_count = 1,
                                               .generic = true };
    const script_catalog metadata = { .templates = &template, .template_count = 1 };
    const script_package_export member = { .name = "Identity",
                                           .id = 800,
                                           .kind = SCRIPT_PACKAGE_TEMPLATE };
    script_package_environment environment;
    const script_package_exports exports = { .identity = "project:lib",
                                             .metadata = &metadata,
                                             .members = &member,
                                             .member_count = 1,
                                             .environment = &environment };
    int start = (int)(strstr(text, "import lib") - text);
    script_range range = { .start = start,
                           .end = start + (int)strlen("import lib \"../lib\""),
                           .line = 2,
                           .column = 1,
                           .document = 0 };
    const script_package_import import = { .document = 0,
                                           .range = range,
                                           .alias = "lib",
                                           .identity = "project:lib",
                                           .exports = &exports };
    const script_package_options options = { .documents = &caller,
                                             .document_count = 1,
                                             .identity = "project:app",
                                             .native = &native,
                                             .imports = &import,
                                             .import_count = 1 };
    script_catalog nested_metadata;
    script_catalog_type nested_type;
    script_catalog_variant nested_variant;
    script_catalog_capability nested_capability;
    script_catalog_template nested_template;
    script_catalog_specialization nested_specialization;
    script_package_exports dependency;
    script_package_import dependency_import;
    int payload = 1;
    script_package_export environment_bindings[2] = {
        member,
        { .name = "Hidden",
          .id = 901,
          .kind = SCRIPT_PACKAGE_CONSTANT,
          .type = { .storage = PIN_STRUCT },
          .value = { .type = PIN_STRUCT, .ptr = &payload } },
    };
    for (int sample = 0; sample < 34; sample++) {
        definition = (script_package_document){ "/project/lib/a.chibi",
                                                "project:lib/a.chibi",
                                                definition_syntax };
        environment = (script_package_environment){ .identity = "project:lib",
                                                    .documents = &definition,
                                                    .document_count = 1,
                                                    .bindings = &member,
                                                    .binding_count = 1 };
        nested_metadata = (script_catalog){ 0 };
        nested_type = (script_catalog_type){ 0 };
        nested_variant = (script_catalog_variant){ 0 };
        nested_capability = (script_catalog_capability){ 0 };
        nested_template = template;
        nested_specialization = (script_catalog_specialization){ .id = 900,
                                                                 .template_id = 800 };
        dependency = (script_package_exports){ .identity = "project:dependency",
                                               .metadata = &nested_metadata };
        dependency_import = (script_package_import){
            .document = 0,
            .alias = "dependency",
            .identity = "project:dependency",
            .exports = &dependency
        };
        if (sample >= 13) {
            environment.imports = &dependency_import;
            environment.import_count = 1;
        }
        switch (sample) {
            case 1: environment.documents = NULL; break;
            case 2: environment.document_count = -1; break;
            case 3:
                environment.document_count = SCRIPT_PACKAGE_MAX_DOCUMENTS + 1;
                break;
            case 4: environment.import_count = 1; break;
            case 5:
                environment.binding_count = 1;
                environment.bindings = NULL;
                break;
            case 6: environment.identity = NULL; break;
            case 7: environment.identity = "project:other"; break;
            case 8: definition.path = NULL; break;
            case 9: definition.key = ""; break;
            case 10: definition.syntax = NULL; break;
            case 11: environment.import_count = -1; break;
            case 12: environment.binding_count = 4097; break;
            /* A self import is valid and must not recurse indefinitely. */
            case 13:
                dependency_import.identity = "project:lib";
                dependency_import.exports = &exports;
                break;
            case 14: nested_metadata.global_count = -1; break;
            case 15: nested_metadata.global_count = 1; break;
            case 16: nested_metadata.relationship_count = -1; break;
            case 17: nested_metadata.relationship_count = 1; break;
            case 18: nested_metadata.template_count = -1; break;
            case 19: nested_metadata.template_count = 1; break;
            case 20: nested_metadata.specialization_count = -1; break;
            case 21: nested_metadata.specialization_count = 1; break;
            case 22: nested_metadata.type_count = 1; break;
            case 23:
                nested_metadata.types = &nested_type;
                nested_metadata.type_count = 1;
                nested_type.field_count = 1;
                break;
            case 24:
                nested_metadata.types = &nested_type;
                nested_metadata.type_count = 1;
                nested_type.variant_count = 1;
                break;
            case 25:
                nested_metadata.types = &nested_type;
                nested_metadata.type_count = 1;
                nested_type.variants = &nested_variant;
                nested_type.variant_count = 1;
                nested_variant.field_count = 1;
                break;
            case 26: nested_metadata.capability_count = 1; break;
            case 27:
                nested_metadata.capabilities = &nested_capability;
                nested_metadata.capability_count = 1;
                nested_capability.parameter_count = 1;
                break;
            case 28:
                nested_metadata.capabilities = &nested_capability;
                nested_metadata.capability_count = 1;
                nested_capability.result_count = 1;
                break;
            case 29:
                nested_metadata.capabilities = &nested_capability;
                nested_metadata.capability_count = 1;
                nested_capability.entry_count = 1;
                break;
            case 30:
                nested_metadata.capabilities = &nested_capability;
                nested_metadata.capability_count = 1;
                nested_capability.outcome_count = 1;
                break;
            case 31:
                nested_metadata.templates = &nested_template;
                nested_metadata.template_count = 1;
                nested_template.parameters = NULL;
                break;
            case 32:
                nested_metadata.specializations = &nested_specialization;
                nested_metadata.specialization_count = 1;
                nested_specialization.binding_count = 1;
                break;
            case 33:
                environment.bindings = environment_bindings;
                environment.binding_count = 2;
                break;
        }
        script_analysis_free(direct_analysis);
        direct_analysis = script_analyze_package(&options);
        if (!sample || sample == 13) {
            REQUIRE(direct_analysis->valid);
            continue;
        }
        REQUIRE(!direct_analysis->valid && direct_analysis->diagnostic_count > 0);
        if (sample == 33) {
            REQUIRE(
                strstr(direct_analysis->diagnostics[0].message, "Cannot retain generic definition environment")
            );
            REQUIRE(direct_analysis->diagnostics[0].range.document == 0);
            REQUIRE(
                direct_analysis->diagnostics[0].range.start ==
                (int)(strstr(text, "lib.Identity") - text)
            );
            continue;
        }
        REQUIRE(strstr(direct_analysis->diagnostics[0].message, "Invalid package import binding"));
        REQUIRE(direct_analysis->diagnostics[0].range.start == range.start);
    }
    return true;
}

void test_language_package_imports()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_imports_transitive_calls_execute_on_each_backend);
    RUN_TEST_CASE(package_imports_reused_alias_spelling_remains_file_local);
    RUN_TEST_CASE(package_imports_self_alias_resolves_sibling_function);
    RUN_TEST_CASE(package_imports_do_not_leak_unqualified_names_package_names_or_blank_bindings);
    RUN_TEST_CASE(package_imports_own_record_union_and_aggregate_constant_values);
    RUN_TEST_CASE(package_imports_ids_and_results_survive_input_order_and_root_relocation);
    RUN_TEST_CASE(package_imports_incremental_reuse_follows_all_edges_and_retains_old_valid_generation);
    RUN_TEST_CASE(package_imports_cached_closure_tracks_sibling_membership_and_paths);
    RUN_TEST_CASE(package_imports_export_generic_functions);
    RUN_TEST_CASE(package_imports_generic_definition_scope_and_transitive_requests);
    RUN_TEST_CASE(package_imports_private_templates_stay_private);
    RUN_TEST_CASE(package_imports_nested_specialization_keeps_stable_definition_owners);
    RUN_TEST_CASE(package_imports_structured_arguments_accept_callable_algorithms);
    RUN_TEST_CASE(package_imports_authored_structured_arguments_and_literals_keep_outer_types);
    RUN_TEST_CASE(package_imports_overload_arguments_accept_growing_definition_environments);
    RUN_TEST_CASE(package_imports_event_receivers_keep_sender_contracts_while_catalogs_grow);
    RUN_TEST_CASE(package_imports_callable_definition_errors_point_to_the_importing_call);
    RUN_TEST_CASE(package_imports_generic_nominals_and_definition_edits_do_not_alias);
    RUN_TEST_CASE(package_imports_callable_arguments_use_the_definition_environment);
    RUN_TEST_CASE(package_imports_private_aggregate_constants_survive_callable_generation_teardown);
    RUN_TEST_CASE(package_imports_generic_views_preserve_foreign_nominals_and_pointer_identity);
    RUN_TEST_CASE(package_imports_optional_binders_preserve_array_fallback_values);
    RUN_TEST_CASE(package_imports_pointer_callbacks_preserve_shadowed_scoped_inputs);
    RUN_TEST_CASE(package_imports_see_only_pub_declarations);
    RUN_TEST_CASE(package_imports_exported_defaults_are_constant_values_without_host_execution);
    RUN_TEST_CASE(package_import_catalog_snapshot_owns_union_payload_defaults);
    RUN_TEST_CASE(package_exports_keep_private_managed_pointee_metadata);
    RUN_TEST_CASE(package_imports_malformed_public_exports_are_rejected_before_lookup);
    RUN_TEST_CASE(package_imports_ignore_unused_collection_bodies_but_check_every_project);
    RUN_TEST_CASE(package_imports_check_direct_transitive_and_blank_collection_imports);
    RUN_TEST_CASE(package_imports_collection_only_snapshot_checks_first_input_root);
    RUN_TEST_CASE(package_imports_removing_collection_import_releases_only_new_dependency);
    RUN_TEST_CASE(package_imports_malformed_definition_environments_are_rejected);
    pu_clear_setup();
}
