#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "authoring/studio_outliner_definition.h"
#include "execution/definition.h"
#include "host/native_catalog.h"
#include "runtime/arrays.h"
#include "runtime/text_format.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* One preparation-only case, one bytecode collection preparation, two modules:
 * eight controller handlers and twelve library functions. The library's 22
 * named constants are evaluated by the existing compiler (100000 per-frame
 * budget); no controller/helper is invoked at runtime. Exactly two regular
 * source files are read once, at most 13111 bytes including two EOF probes.
 * CHIBI_OUTLINER_CONTENT must explicitly name their absolute content directory;
 * there is no worker-specific or relative-path fallback.
 * Seven fixed memory overlays use one empty owned temporary directory, removed
 * in cleanup. No source writes, world, registration, dispatch, renderer, clock,
 * sleep, child process, network, backend fallback or generated executable.
 *
 * The census validates all 20 declarations, then visits the 14 bodies reachable
 * from Refresh/RevealObject. Six ordinary control handlers are excluded from K.
 * Static inspection caps (not production scene limits): 32768 canonical
 * instructions, 32 call descriptors and 20 owned definitions per body. At most
 * 20 inventory lines, two source lines and one total line are printed on success.
 * Runtime input checks are exact byte lengths plus FNV-1a-64. SHA-256 strings
 * below are independently recorded review provenance, not runtime SHA checks.
 */
enum
{
    WORK_SOURCES = 2,
    WORK_DECLARATIONS = 20,
    WORK_REACHABLE = 14,
    WORK_CONSTANTS = 22,
    WORK_MAX_INSTRUCTIONS = 32768,
    WORK_MAX_CALLS = 32,
    WORK_MAX_SYMBOLS = 4096,
};
static const struct
{
    const char *path, *name, *reviewed_sha256;
    size_t bytes;
    uint64_t fingerprint;
    int functions, constants;
} inputs[WORK_SOURCES] = {
    { "behaviors/OutlinerRows.chibi",
      "OutlinerRows",
      "510f2155a49ff77724efd38be5d1098f5ac5957c62acb0c06eb755de22e8a450",
      1441,
      UINT64_C(0x8c5ca50476106b0b),
      8,
      0 },
    { "libraries/OutlinerRowPolicies.chibi",
      "OutlinerRowPolicies",
      "e2c727396b2a5004327fcf17cd3e1a8194629c928db75c4927ef6f4cae9c28a5",
      11668,
      UINT64_C(0x14e0b3fe818523ed),
      12,
      WORK_CONSTANTS },
};
static const char *const handlers[] = {
    "Begin",      "ShowRows",   "Refresh",     "FoldRoot",
    "UnfoldRoot", "ToggleRoot", "RootClicked", "RevealObject",
};
static const char *const helpers[] = {
    "Depth",    "Icon",           "Drop",      "SelectState",
    "FoldByte", "ContainsFolded", "LastIndex", "Title",
    "IntRange", "BehaviorCount",  "Page",      "Project",
};
typedef struct
{
    execution_module_function_info info;
    const char *name;
    int source;
    bool reachable;
    uintptr_t checks;
} work_body;
static struct
{
    char root[512];
    char content[768];
    char *texts[WORK_SOURCES];
    native_capabilities *native;
    authoring_sources *sources;
    const authoring_source *source[WORK_SOURCES];
    work_body bodies[WORK_DECLARATIONS];
    behavior_run_scope preparation;
    int body_count;
    bool catalog_set;
} work;

static void work_cleanup()
{
    if (work.preparation.active) {
        behavior_run_scope_end(&work.preparation);
    }
    authoring_sources_free(work.sources);
    native_capabilities_free(work.native);
    if (work.catalog_set) {
        type_catalog_set_project(NULL);
    }
    for (int i = 0; i < WORK_SOURCES; i++) {
        free(work.texts[i]);
    }
    if (work.root[0]) {
        int removed = rmdir(work.root);
        assert(!removed);
        (void)removed;
    }
    memset(&work, 0, sizeof work);
}
static uint64_t work_fingerprint(const char *text, size_t bytes)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < bytes; i++) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static bool work_content()
{
    const char *content = getenv("CHIBI_OUTLINER_CONTENT");
    size_t length = content ? strnlen(content, sizeof work.content) : 0;
    if (!length || length >= sizeof work.content || content[0] != '/') {
        fprintf(
            stderr,
            "Outliner work: set CHIBI_OUTLINER_CONTENT to an absolute content "
            "directory shorter than %zu bytes.\n",
            sizeof work.content
        );
        return false;
    }
    struct stat information;
    if (stat(content, &information) || !S_ISDIR(information.st_mode)) {
        fprintf(stderr, "Outliner work: CHIBI_OUTLINER_CONTENT is not an existing directory: %s\n", content);
        return false;
    }
    memcpy(work.content, content, length + 1);
    return true;
}
static bool work_read(int index)
{
    char path[1024];
    int length = snprintf(
        path,
        sizeof path,
        "%s/%s",
        work.content,
        inputs[index].path
    );
    REQUIRE(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    struct stat information;
    bool regular = !fstat(fileno(file), &information) &&
                   S_ISREG(information.st_mode) &&
                   information.st_size == (off_t)inputs[index].bytes;
    work.texts[index] = malloc(inputs[index].bytes + 2);
    if (!work.texts[index]) {
        fclose(file);
        REQUIRE(work.texts[index]);
    }
    size_t bytes = regular
                     ? fread(work.texts[index], 1, inputs[index].bytes + 1, file)
                     : 0;
    bool failed = ferror(file) != 0;
    int closed = fclose(file);
    REQUIRE(regular && !failed && !closed && bytes == inputs[index].bytes);
    REQUIRE(!memchr(work.texts[index], 0, bytes));
    work.texts[index][bytes] = 0;
    uint64_t fingerprint = work_fingerprint(work.texts[index], bytes);
    REQUIRE(fingerprint == inputs[index].fingerprint);
    printf(
        "outliner-work source=%s bytes=%zu fnv1a64=%016" PRIx64
        " reviewed_sha256=%s\n",
        inputs[index].path,
        bytes,
        fingerprint,
        inputs[index].reviewed_sha256
    );
    return true;
}
static bool work_catalog()
{
    char error[256];
    type_catalog_set_project(&STUDIO_OUTLINER_TYPE_PACK);
    work.catalog_set = true;
    native_type_catalog *
        types = native_type_catalog_new(STUDIO_OUTLINER_TYPE_DEFINITIONS, 3, error, sizeof error);
    REQUIRE(types);
    uint64_t entity_id = script_catalog_id("type", "ecs.entity");
    script_catalog_type entity = {
        .id = entity_id,
        .name = "ecs.entity",
        .type = { .storage = PIN_HANDLE, .nominal = entity_id },
        .size = sizeof(uint64_t),
    };
    script_catalog base = { .types = &entity, .type_count = 1, .entity_type = entity_id };
    const native_capability_pack type_packs[] = {
        native_type_catalog_pack(types),
        { .catalog = &base },
    };
    const native_definition_pack packs[] = {
        STUDIO_OUTLINER_DEFINITION_PACK,
        NATIVE_ARRAY_DEFINITION_PACK,
        NATIVE_TEXT_DEFINITION_PACK,
    };
    work.native = host_native_catalog_new(
        type_packs,
        NATIVE_COUNT(type_packs),
        packs,
        NATIVE_COUNT(packs),
        error,
        sizeof error
    );
    native_type_catalog_free(types);
    if (!work.native) {
        fprintf(stderr, "Outliner work catalog: %s\n", error);
    }
    REQUIRE(work.native);
    return true;
}
static bool work_prepare()
{
    REQUIRE(work_content() && work_read(0) && work_read(1) && work_catalog());
    char root[] = "/tmp/chibi-outliner-work-XXXXXX";
    REQUIRE(mkdtemp(root));
    memcpy(work.root, root, sizeof root);
    static const char *const paths[] = {
        "behaviors/OutlinerRows.chibi",
        "libraries/OutlinerRowPolicies.chibi",
        "events/ShowRows.kdl",
        "events/Refresh.kdl",
        "events/RootClicked.kdl",
        "events/RevealObject.kdl",
        "ui/Outliner.rml",
    };
    const char *texts[] = {
        work.texts[0],
        work.texts[1],
        "name \"ShowRows\"\n(array)variables { - name=\"First\" type=\"int\"; "
        "- name=\"Count\" type=\"int\"; }\n",
        "name \"Refresh\"\n",
        "name \"RootClicked\"\n(array)variables { - name=\"Open\" "
        "type=\"bool\"; }\n",
        "name \"RevealObject\"\n(array)variables { - name=\"Object\" "
        "type=\"const char *\"; }\n",
        "<rml><body><div data-event-click=\"emit('FoldRoot'); "
        "emit('UnfoldRoot'); "
        "emit('ToggleRoot')\"></div></body></rml>",
    };
    char full[7][640];
    source_catalog_edit edits[7] = { 0 };
    for (int i = 0; i < 7; i++) {
        int length = snprintf(full[i], sizeof full[i], "%s/%s", root, paths[i]);
        REQUIRE(length > 0 && (size_t)length < sizeof full[i]);
        edits[i] = (source_catalog_edit){ .path = full[i], .text = texts[i] };
    }
    behavior_run_scope_begin(&work.preparation);
    source_catalog *snapshot = source_catalog_replaced(
        root,
        native_capabilities_catalog(work.native),
        edits,
        7
    );
    script_lower_bindings bindings = authoring_native_bindings(work.native);
    work.sources = authoring_sources_prepare_catalog(snapshot, &bindings, BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&work.preparation);
    REQUIRE(work.sources);
    REQUIRE(authoring_sources_catalog_diagnostic_count(work.sources) == 0);
    REQUIRE(authoring_sources_count(work.sources) == WORK_SOURCES);
    for (int i = 0; i < WORK_SOURCES; i++) {
        const authoring_source *source = authoring_sources_at(work.sources, i);
        if (source->diagnostic_count) {
            fprintf(stderr, "Outliner work source: %s\n", source->diagnostics[0].message);
        }
        REQUIRE(source->valid && source->analysis && source->module);
        int index = !strcmp(source->analysis->name, inputs[0].name) ? 0 : 1;
        REQUIRE(!strcmp(source->analysis->name, inputs[index].name));
        REQUIRE(!work.source[index]);
        work.source[index] = source;
    }
    REQUIRE(authoring_sources_valid(work.sources));
    const behavior_run_counts *counts = &work.preparation.counts;
    REQUIRE(!counts->overflow && counts->bytecode_entries == WORK_CONSTANTS);
    REQUIRE(!counts->sljit_entries && !counts->wasm_entries && !counts->inspection_bytecode_fallbacks);
    return true;
}
static int work_index(const behavior_function *function)
{
    for (int i = 0; i < work.body_count; i++) {
        if (work.bodies[i].info.body == function) {
            return i;
        }
    }
    return -1;
}
static bool work_inventory()
{
    for (int s = 0; s < WORK_SOURCES; s++) {
        const authoring_source *source = work.source[s];
        const script_analysis *analysis = source->analysis;
        REQUIRE(execution_module_backend(source->module) == BEHAVIOR_BYTECODE);
        REQUIRE(analysis->function_count == inputs[s].functions);
        REQUIRE(execution_module_function_count(source->module) == inputs[s].functions);
        REQUIRE(analysis->symbol_count >= 0 && analysis->symbol_count <= WORK_MAX_SYMBOLS);
        int constants = 0;
        for (int i = 0; i < analysis->symbol_count; i++) {
            constants += analysis->symbols[i].kind == SCRIPT_CONSTANT;
        }
        REQUIRE(constants == inputs[s].constants);
        bool found[12] = { false };
        for (int i = 0; i < inputs[s].functions; i++) {
            execution_module_function_info
                info = execution_module_function_at(source->module, i);
            REQUIRE(info.body && !info.subscription && !info.parent && !info.action);
            REQUIRE(work_index(info.body) < 0);
            const script_function *syntax = &analysis->functions[i];
            const char *name = info.event_name;
            if (s) {
                REQUIRE(info.kind == EXECUTION_FUNCTION_AUTHORED && !info.event);
                REQUIRE(syntax->symbol >= 0 && syntax->symbol < analysis->symbol_count);
                name = analysis->symbols[syntax->symbol].name;
            } else {
                REQUIRE(info.kind == EXECUTION_FUNCTION_HANDLER && info.event);
            }
            REQUIRE(name);
            int expected = -1;
            for (int j = 0; j < inputs[s].functions; j++) {
                if (!strcmp(name, s ? helpers[j] : handlers[j])) {
                    expected = j;
                }
            }
            REQUIRE(expected >= 0 && !found[expected]);
            found[expected] = true;
            REQUIRE(work.body_count < WORK_DECLARATIONS);
            work.bodies[work.body_count++] = (work_body){ .info = info,
                                                          .name = name,
                                                          .source = s };
        }
        for (int i = 0; i < inputs[s].functions; i++) {
            REQUIRE(found[i]);
        }
    }
    REQUIRE(work.body_count == WORK_DECLARATIONS);
    REQUIRE(execution_module_import_count(work.source[0]->module) == 1);
    REQUIRE(
        execution_module_import_at(work.source[0]->module, 0) == work.source[1]->module
    );
    REQUIRE(execution_module_import_count(work.source[1]->module) == 0);
    return true;
}
static bool work_census()
{
    int queue[WORK_DECLARATIONS], count = 0;
    for (int i = 0; i < work.body_count; i++) {
        work_body *body = &work.bodies[i];
        if (!body->source && (!strcmp(body->name, "Refresh") ||
                              !strcmp(body->name, "RevealObject"))) {
            body->reachable = true;
            queue[count++] = i;
        }
    }
    REQUIRE(count == 2);
    for (int head = 0; head < count; head++) {
        const behavior_function *function = work.bodies[queue[head]].info.body;
        REQUIRE(acount(function->calls) <= WORK_MAX_CALLS);
        REQUIRE(acount(function->definitions) <= WORK_DECLARATIONS);
        for (int i = 0; i < acount(function->calls); i++) {
            REQUIRE(!function->calls[i].task);
            int child = work_index(function->calls[i].function);
            REQUIRE(child >= 0);
            if (!work.bodies[child].reachable) {
                REQUIRE(count < WORK_DECLARATIONS);
                work.bodies[child].reachable = true;
                queue[count++] = child;
            }
        }
        /* Owned definitions are only storage: an uncalled definition must not
         * silently expand the call closure. Every owner must still be known. */
        for (int i = 0; i < acount(function->definitions); i++) {
            REQUIRE(work_index(function->definitions[i]) >= 0);
        }
    }
    REQUIRE(count == WORK_REACHABLE);
    uintptr_t total = 0;
    for (int i = 0; i < work.body_count; i++) {
        work_body *body = &work.bodies[i];
        const behavior_function *function = body->info.body;
        int instructions = acount(function->instructions);
        REQUIRE(instructions > 0 && instructions <= WORK_MAX_INSTRUCTIONS);
        REQUIRE(acount(function->calls) <= WORK_MAX_CALLS);
        REQUIRE(!function->root_steps);
        for (int pc = 0; pc < instructions; pc++) {
            body->checks += function->instructions[pc].op == BF_CHECK;
        }
        REQUIRE(body->checks > 0);
        REQUIRE(
            body->reachable == (body->source || !strcmp(body->name, "Refresh") ||
                                !strcmp(body->name, "RevealObject"))
        );
        if (body->reachable) {
            REQUIRE(body->checks <= UINTPTR_MAX - total);
            total += body->checks;
        }
        printf(
            "outliner-work body=%s.%s id=%016" PRIx64
            " reachable=%d canonical_instructions=%d checks=%" PRIuPTR
            " calls=%d appends=%d\n",
            inputs[body->source].name,
            body->name,
            body->info.id,
            body->reachable,
            instructions,
            body->checks,
            acount(function->calls),
            acount(function->appends)
        );
    }
    REQUIRE(total > 0 && total <= (uintptr_t)WORK_REACHABLE * WORK_MAX_INSTRUCTIONS);
    printf(
        "outliner-work total sources=2 modules=2 declarations=20 reachable=14"
        " constants=22 runtime_calls=0 K=%" PRIuPTR "\n",
        total
    );
    return true;
}
TEST_CASE(outliner_work_counts_frozen_canonical_request_closure)
{
    REQUIRE(work_prepare());
    REQUIRE(work_inventory());
    REQUIRE(work_census());
    return true;
}
void test_language_outliner_work()
{
    pu_setup(NULL, work_cleanup);
    RUN_TEST_CASE(outliner_work_counts_frozen_canonical_request_closure);
    pu_clear_setup();
}
