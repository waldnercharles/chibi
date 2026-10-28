#include "authoring/native_bindings.h"
#include "backend.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/module.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/text_format.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/copy.h"
#include "types/record.h"
#include "types/text.h"

#include <ctype.h>
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Source-backed selected-range specimen, not a live Studio read service.
 * Policy: Studio studio_node_calls.c:72-132, studio_outliner.c:67-78,
 * studio_shell.c:80-109, studio_drop.c:154-168 at b26ff174.
 * Two cases, four source preparations, 28 top-level calls and two direct
 * zero-handle formatting checks per target; at most four raw actors.
 * The temporary text bindings expose no scene authority or UI policy.
 * Native/C-Wasm each exercise bytecode and their actual compiled backend.
 * Parent shape, non-NULL strings and C-locale byte semantics are preconditions.
 */
static const char SOURCE[] =
    "OutlinerProjection :: library {\n"
    "    Row :: struct {\n"
    "        key:string; label:string; icon:string=\"\"; "
    "prototype:string=\"\"; drop:string=\"\";\n"
    "        depth:int=1; behaviors:int=0; active:bool=false; "
    "marked:bool=false;\n"
    "        hidden:bool=false; locked:bool=false; renaming:bool=false;\n"
    "    }\n"
    "    Depth :: (parents:int[], index:int)->int {\n"
    "        depth:int=1; parent:int=parents[index];\n"
    "        while parent>=0 { depth=depth+1; parent=parents[parent]; }\n"
    "        return depth;\n"
    "    }\n"
    "    Icon :: (bits:int)->string {\n"
    "        if (bits&1)!=0 { return \"󰵝\"; }\n"
    "        if (bits&2)!=0 { return \"\"; }\n"
    "        if (bits&4)!=0 { return \"󰌶\"; }\n"
    "        if (bits&8)!=0 { return \"󰙴\"; }\n"
    "        if (bits&16)!=0 { return \"󰸊\"; }\n"
    "        if (bits&32)!=0 { return \"󰖊\"; }\n"
    "        if (bits&64)!=0 { return \"󰊗\"; }\n"
    "        if (bits&128)!=0 { return \"󰥻\"; }\n"
    "        if (bits&256)!=0 { return \"󰳚\"; }\n"
    "        if (bits&512)!=0 { return \"󰋕\"; }\n"
    "        if (bits&1024)!=0 { return \"󰮋\"; }\n"
    "        if (bits&2048)!=0 { return \"󰠲\"; }\n"
    "        return \"󰆧\";\n"
    "    }\n"
    "    Drop :: (bits:int, editable:bool)->string {\n"
    "        if !editable { return \"\"; }\n"
    "        text:string=\"object behavior\";\n"
    "        if (bits&1)!=0 { text=format_text(\"{} material\",text); }\n"
    "        if (bits&2)!=0 { text=format_text(\"{} shape\",text); }\n"
    "        if (bits&4)!=0 { text=format_text(\"{} ui\",text); }\n"
    "        if (bits&8)!=0 { text=format_text(\"{} field\",text); }\n"
    "        if (bits&16)!=0 { text=format_text(\"{} system\",text); }\n"
    "        return text;\n"
    "    }\n"
    "    Project :: (entities:entity[], names:string[], prototypes:string[], "
    "parents:int[],\n"
    "        behaviors:int[], icons:int[], assignments:int[], selected:bool[], "
    "views:int[],\n"
    "        active:int, rename:entity, renameOutliner:bool, editable:bool,\n"
    "        filter:string, first:int, count:int)->(Rows:Row[],Shown:int) {\n"
    "        matches:int[]=[]; index:int=0; total:int=length(entities);\n"
    "        while index<total {\n"
    "            if fixture_contains_folded(filter,names[index]) ||\n"
    "                fixture_contains_folded(filter,prototypes[index]) {\n"
    "                matches=append(matches,index);\n"
    "            }\n"
    "            index=index+1;\n"
    "        }\n"
    "        shown:int=length(matches);\n"
    "        start:int=first; amount:int=count;\n"
    "        if start<0 { start=0; }\n"
    "        if start>shown { start=shown; }\n"
    "        if amount<0 { amount=0; }\n"
    "        if amount>shown-start { amount=shown-start; }\n"
    "        rows:Row[]=[]; i:int=0;\n"
    "        while i<amount {\n"
    "            at:int=matches[start+i];\n"
    "            isActive:bool=active==at;\n"
    "            row:Row={key:fixture_entity_decimal(entities[at]), "
    "label:names[at],\n"
    "                icon:Icon(icons[at]), prototype:prototypes[at],\n"
    "                drop:Drop(assignments[at],editable), "
    "depth:Depth(parents,at),\n"
    "                behaviors:behaviors[at], active:isActive,\n"
    "                marked:selected[at] && !isActive,\n"
    "                hidden:(views[at]&1)!=0, locked:(views[at]&2)!=0,\n"
    "                renaming:renameOutliner && rename==entities[at]};\n"
    "            rows=append(rows,row); i=i+1;\n"
    "        }\n"
    "        return(rows,shown);\n"
    "    }\n"
    "}\n";

enum
{
    RAW_ARRAYS = 9,
    ACTORS = 4,
    ARGUMENTS = 16
};
static const behavior_backend BACKENDS[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const uint64_t IDS[] = {
    UINT64_C(0x8000000100000007),
    UINT64_C(0x8000000200000007),
    UINT64_C(0xfedcba9800000009),
    UINT64_C(0x0020000000000001),
};
static const char *const KEYS[] = {
    "9223372041149743111",
    "9223372045444710407",
    "18364758542507835401",
    "9007199254740993",
};
static const char *const NAMES[] = { "Alpha", "Bolt", "Élan", "Delta" };
static const char *const CHANGED_NAMES[] = { "Alpha-new", "Bolt-new", "Élan-new", "Delta-new" };
static const char *const PROTOTYPES[] = { "", "CameraRig", "Box", "Unresolved" };
static const int PARENTS[] = { -1, 0, 1, -1 };
static const int BEHAVIORS[] = { 0, 2, 1, 3 };
static const int ICONS[] = { 0, 5, 4, 0 };
static const int ASSIGNMENTS[] = { 0, 31, 10, 4 };
static const int VIEWS[] = { 0, 1, 2, 3 };
static const bool SELECTED[] = { false, true, true, true };
/* Independent expected UTF-8 bytes, rather than another copy of the authored
 * selection algorithm. U+F01A7, U+F0D5D and U+F0336 respectively. */
static const char *const GLYPHS[] = {
    "\xf3\xb0\x86\xa7",
    "\xf3\xb0\xb5\x9d",
    "\xf3\xb0\x8c\xb6",
    "\xf3\xb0\x86\xa7",
};
static const char *const DROPS[] = {
    "object behavior",
    "object behavior material shape ui field system",
    "object behavior shape field",
    "object behavior ui",
};
static const int DEPTHS[] = { 1, 2, 3, 1 };
static const char *const FIELDS[] = {
    "key",       "label",  "icon",   "prototype", "drop",   "depth",
    "behaviors", "active", "marked", "hidden",    "locked", "renaming",
};

typedef struct
{
    int raw_count, first, count;
    const char *filter;
    int shown, row_count, rows[ACTORS], match_calls;
    bool readonly;
} request;
static const request REQUESTS[] = {
    { 0, 0, 4, "", 0, 0, { 0 }, 0, false },
    { 4, 0, 4, "", 4, 4, { 0, 1, 2, 3 }, 4, false },
    { 4, 1, 2, "", 4, 2, { 1, 2 }, 4, false },
    { 4, 3, 9, "", 4, 1, { 3 }, 4, false },
    { 4, -2, 2, "", 4, 2, { 0, 1 }, 4, false },
    { 4, 0, -1, "", 4, 0, { 0 }, 4, false },
    { 4, 99, 4, "", 4, 0, { 0 }, 4, false },
    { 4, 0, 4, "ALP", 1, 1, { 0 }, 7, false },
    { 4, 0, 4, "camera", 1, 1, { 1 }, 8, false },
    { 4, 0, 4, "absent", 0, 0, { 0 }, 8, false },
    { 4, 0, 4, "É", 1, 1, { 2 }, 7, false },
    { 4, 0, 4, "", 4, 4, { 0, 1, 2, 3 }, 4, true },
};

typedef struct
{
    arena storage;
    behavior_managed_heap heap;
    behavior_function_context context;
    behavior_function_activation activation;
    pin_value results[2];
} call;
static call calls[2];
static arena producer, inputs;
static char *producer_names[2][ACTORS];
static pin_value arguments[ARGUMENTS];
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static native_capabilities *native;
static uint64_t function_id, record_id, record_layout;
static behavior_run_scope runs;
static pin_copy_scope copies;
static int decimal_calls, match_calls;

static char *copy_text(arena *storage, const char *text)
{
    size_t length = strlen(text) + 1;
    char *out = arena_alloc(storage, length);
    memcpy(out, text, length);
    return out;
}
/* Deliberately test-local: the standard handle formatter includes its nominal
 * prefix. This mechanical conversion preserves every identity bit. */
static const char *entity_decimal(arena *storage, const pin_value *args, int count, pin_value *out)
{
    if (count != 1 || args[0].type != PIN_HANDLE ||
        args[0].container != PIN_CONTAINER_SINGLE) {
        return "Entity decimal fixture argument";
    }
    decimal_calls++;
    char text[24];
    snprintf(text, sizeof text, "%" PRIu64, args[0].u64);
    return pin_value_text(storage, out, text, strlen(text));
}
/* Same byte comparison as studio_matches; not Unicode case folding. */
static const char *contains_folded(arena *storage, const pin_value *args, int count, pin_value *out)
{
    (void)storage;
    if (count != 2 || args[0].type != PIN_STR || args[1].type != PIN_STR ||
        args[0].container != PIN_CONTAINER_SINGLE ||
        args[1].container != PIN_CONTAINER_SINGLE || !pin_text(args[0]).bytes ||
        !pin_text(args[1]).bytes) {
        return "Folded substring fixture argument";
    }
    match_calls++;
    const char *query = pin_text(args[0]).bytes, *text = pin_text(args[1]).bytes;
    bool found = !query[0];
    for (const char *h = text; !found && *h; h++) {
        size_t i = 0;
        while (query[i] && h[i] &&
               tolower((unsigned char)query[i]) == tolower((unsigned char)h[i])) {
            i++;
        }
        found = !query[i];
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = found };
    return NULL;
}
static const node_host_call DECIMAL_CALL = { .value = entity_decimal };
static const node_host_call MATCH_CALL = { .value = contains_folded };
static const node_access TEXT_ACCESS = { 0 };
static const native_parameter_definition DECIMAL_INPUT[] = {
    { .name = "Entity", .storage = PIN_HANDLE, .type_name = "ecs.entity" },
};
static const native_parameter_definition MATCH_INPUT[] = {
    { .name = "Query", .storage = PIN_STR, .type_name = "const char *" },
    { .name = "Text", .storage = PIN_STR, .type_name = "const char *" },
};
static const native_parameter_definition TEXT_OUTPUT[] = {
    { .name = "Result", .storage = PIN_STR, .type_name = "const char *" },
};
static const native_parameter_definition BOOL_OUTPUT[] = {
    { .name = "Result", .storage = PIN_BOOL },
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Fixture Entity Decimal",
      .source_name = "fixture_entity_decimal",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = DECIMAL_INPUT,
      .input_count = 1,
      .outputs = TEXT_OUTPUT,
      .output_count = 1,
      .completion_output = -1,
      .pure = true },
    { .canonical_name = "Fixture Contains Folded",
      .source_name = "fixture_contains_folded",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .inputs = MATCH_INPUT,
      .input_count = 2,
      .outputs = BOOL_OUTPUT,
      .output_count = 1,
      .completion_output = -1,
      .pure = true },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Fixture Entity Decimal",
      .implementation = { .call = &DECIMAL_CALL, .access = &TEXT_ACCESS } },
    { .canonical_name = "Fixture Contains Folded",
      .implementation = { .call = &MATCH_CALL, .access = &TEXT_ACCESS } },
};

static void release_call(call *c)
{
    behavior_function_release_host(&c->context);
    behavior_function_activation_free(&c->activation);
    c->context = (behavior_function_context){ 0 };
    c->activation = (behavior_function_activation){ 0 };
}
static void clear_call(call *c)
{
    release_call(c);
    arena_free(&c->storage);
    behavior_managed_heap_free(&c->heap);
    *c = (call){ 0 };
}
static void retire_module()
{
    release_call(&calls[0]);
    release_call(&calls[1]);
    execution_module_free(module);
    module = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    native_capabilities_free(native);
    native = NULL;
}
static void cleanup()
{
    if (copies.active) {
        pin_copy_scope_end(&copies);
    }
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    retire_module();
    clear_call(&calls[0]);
    clear_call(&calls[1]);
    arena_free(&inputs);
    arena_free(&producer);
    memset(producer_names, 0, sizeof producer_names);
    memset(arguments, 0, sizeof arguments);
}
static const behavior_function *body()
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info f = execution_module_function_at(module, i);
        if (f.id == function_id) {
            return f.body;
        }
    }
    return NULL;
}
static bool same_text(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}
/* Finalization owns a descriptor copy. Compare the actual dispatch contract,
 * including callbacks and flags, rather than the descriptor object's address. */
static bool same_call(const node_host_call *a, const node_host_call *b)
{
    return a && b && same_text(a->service, b->service) &&
           same_text(a->missing_service, b->missing_service) &&
           a->invoke == b->invoke && a->value == b->value &&
           a->context == b->context && a->action == b->action &&
           a->optional_service == b->optional_service &&
           a->variadic == b->variadic && a->reusable == b->reusable;
}
/* Validate invoked lowering sites without wrapping or replacing real bindings.
 * Dynamic site totals are derived from the fixed source/request control flow;
 * these are static instruction counts, not a general host-event counter. */
static bool check_bindings()
{
    /* Both appends grow a local's own array, so they lower to in-place pushes. */
    const node_host_call *expected[] = { &NATIVE_ARRAY_length_CALL,
                                         &NATIVE_FORMAT_TEXT_CALL,
                                         &DECIMAL_CALL,
                                         &MATCH_CALL };
    const int sites[] = { 2, 5, 1, 2 };
    int counts[4] = { 0 }, pushes = 0;
    for (int n = 0; n < execution_module_function_count(module); n++) {
        const behavior_function *f = execution_module_function_at(module, n).body;
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if (i->op != BF_HOST_CALL) {
                continue;
            }
            REQUIRE(i->target >= 0 && i->target < acount(f->host_calls));
            const behavior_function_host_call *h = &f->host_calls[i->target];
            const node_host_call *intrinsic = execution_intrinsic_resolve(h->capability_id);
            if (intrinsic) {
                REQUIRE(execution_intrinsic_id(intrinsic) == h->capability_id);
                REQUIRE(h->capability_abi == execution_intrinsic_abi(h->capability_id));
                REQUIRE(same_call(h->call, intrinsic));
                pushes += intrinsic == &EXECUTION_array_push_call;
                continue;
            }
            int kind = -1;
            for (int k = 0; k < 4; k++) {
                if (same_call(h->call, expected[k])) {
                    kind = k;
                    break;
                }
            }
            REQUIRE(kind >= 0);
            REQUIRE(
                h->call->value && !h->call->invoke && !h->call->context &&
                !h->call->action && !h->call->service
            );
            counts[kind]++;
        }
    }
    for (int k = 0; k < 4; k++) {
        REQUIRE(counts[k] == sites[k]);
    }
    REQUIRE(pushes == 2);
    return true;
}
static bool prepare(behavior_backend backend)
{
    const char *locale = setlocale(LC_CTYPE, NULL);
    REQUIRE(locale && (!strcmp(locale, "C") || !strcmp(locale, "POSIX")));
    native_definition_pack packs[] = {
        NATIVE_ARRAY_DEFINITION_PACK,
        NATIVE_TEXT_DEFINITION_PACK,
        { .definitions = DEFINITIONS,
          .definition_count = 2,
          .bindings = BINDINGS,
          .binding_count = 2 },
    };
    char error[256];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 3, error, sizeof error);
    if (!native) {
        fprintf(stderr, "outliner catalog: %s\n", error);
    }
    REQUIRE(native);
    const script_catalog *catalog = native_capabilities_catalog(native);
    syntax = script_parse(SOURCE);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        fprintf(stderr, "outliner parse: %s\n", syntax->diagnostics[i].message);
    }
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "outliner semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid && analysis->function_count == 4);
    function_id = 0;
    for (int i = 0; i < analysis->function_count; i++) {
        REQUIRE(analysis->functions[i].symbol >= 0);
        const script_symbol *s = &analysis->symbols[analysis->functions[i].symbol];
        if (!strcmp(s->name, "Project")) {
            function_id = s->id;
        }
    }
    REQUIRE(function_id);
    script_lower_bindings bindings = authoring_native_bindings(native);
    plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "outliner lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "outliner finish: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    REQUIRE(execution_module_function_count(module) == 4);
    REQUIRE(check_bindings());
    REQUIRE(body() && body()->argument_count == ARGUMENTS && body()->result_count == 2);
    REQUIRE(body()->result_defaults[0].record);
    record_id = body()->result_defaults[0].record->id;
    record_layout = body()->result_defaults[0].record->layout;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    return true;
}
static bool make_inputs(const request *r, bool changed)
{
    arena_free(&inputs);
    arena_free(&producer);
    memset(producer_names, 0, sizeof producer_names);
    for (int n = 0; n < r->raw_count; n++) {
        REQUIRE(PARENTS[n] == -1 || (PARENTS[n] >= 0 && PARENTS[n] < n));
        producer_names[0][n] = copy_text(&producer, changed ? CHANGED_NAMES[n] : NAMES[n]);
        producer_names[1][n] = copy_text(&producer, PROTOTYPES[n]);
    }
    const pin_type types[] = { PIN_HANDLE, PIN_STR, PIN_STR,  PIN_I32, PIN_I32,
                               PIN_I32,    PIN_I32, PIN_BOOL, PIN_I32 };
    for (int a = 0; a < RAW_ARRAYS; a++) {
        const char *name = a == 0           ? "ecs.entity"
                         : a == 1 || a == 2 ? "const char *"
                                            : NULL;
        pin_array *array = pin_array_snapshot_allocate(&inputs, types[a], name, r->raw_count);
        REQUIRE(array && arena_contains(&inputs, array));
        REQUIRE(array->count == r->raw_count);
        REQUIRE(!array->count || arena_contains(&inputs, array->items));
        for (int n = 0; n < r->raw_count; n++) {
            pin_string descriptor;
            pin_value v = { .type = types[a], .type_name = name };
            switch (a) {
                case 0: v.u64 = IDS[n]; break;
                case 1:
                    descriptor = (pin_string){ producer_names[0][n],
                                               (uint32_t)strlen(producer_names[0][n]) };
                    v.text = &descriptor;
                    break;
                case 2:
                    descriptor = (pin_string){ producer_names[1][n],
                                               (uint32_t)strlen(producer_names[1][n]) };
                    v.text = &descriptor;
                    break;
                case 3: v.i = PARENTS[n]; break;
                case 4: v.i = BEHAVIORS[n]; break;
                case 5: v.i = ICONS[n]; break;
                case 6: v.i = ASSIGNMENTS[n]; break;
                case 7: v.b = r->readonly ? n < 2 : SELECTED[n]; break;
                case 8: v.i = VIEWS[n]; break;
            }
            REQUIRE(!pin_value_snapshot_to_arena(&inputs, &array->items[n], &v));
            /* Nonempty text is copied into the inputs; empty text has no
             * backing. */
            if ((a == 1 || a == 2) && pin_text(v).length) {
                REQUIRE(
                    pin_text(array->items[n]).bytes != pin_text(v).bytes &&
                    arena_contains(&inputs, array->items[n].text)
                );
            } else if (a == 1 || a == 2) {
                REQUIRE(!array->items[n].text);
            }
        }
        arguments[a] = (pin_value){ .type = types[a],
                                    .type_name = array->type_name,
                                    .container = PIN_CONTAINER_ARRAY,
                                    .ptr = array };
    }
    arguments[9] = (pin_value){ .type = PIN_I32, .i = r->readonly ? 1 : 2 };
    arguments[10] = (pin_value){ .type = PIN_HANDLE,
                                 .type_name = "ecs.entity",
                                 .u64 = IDS[1] };
    arguments[11] = (pin_value){ .type = PIN_BOOL, .b = !r->readonly };
    arguments[12] = (pin_value){ .type = PIN_BOOL, .b = !r->readonly };
    REQUIRE(!pin_value_text(&inputs, &arguments[13], r->filter, strlen(r->filter)));
    arguments[14] = (pin_value){ .type = PIN_I32, .i = r->first };
    arguments[15] = (pin_value){ .type = PIN_I32, .i = r->count };
    return true;
}
/* Read-only oracle: invocation may borrow readonly inputs, never change them. */
/* The inputs own nonempty text; empty text has no backing. */
static bool input_text(const pin_value *value)
{
    return pin_text(*value).length ? arena_contains(&inputs, value->text)
                                   : !value->text;
}
static bool check_inputs(const request *r, bool changed)
{
    const pin_type types[] = { PIN_HANDLE, PIN_STR, PIN_STR,  PIN_I32, PIN_I32,
                               PIN_I32,    PIN_I32, PIN_BOOL, PIN_I32 };
    for (int a = 0; a < RAW_ARRAYS; a++) {
        const pin_value *v = &arguments[a];
        const pin_array *array = v->ptr;
        REQUIRE(v->type == types[a] && v->container == PIN_CONTAINER_ARRAY);
        REQUIRE(!v->record && array && arena_contains(&inputs, array));
        REQUIRE(array->element_type == types[a] && array->count == r->raw_count);
        REQUIRE(
            !array->record &&
            (!array->count ||
             arena_contains(&inputs, array->stride ? array->data : (void *)array->items))
        );
        for (int n = 0; n < r->raw_count; n++) {
            pin_value element = pin_array_get(array, n);
            const pin_value *item = &element;
            REQUIRE(item->type == types[a] && item->container == PIN_CONTAINER_SINGLE);
            REQUIRE(!item->record);
            switch (a) {
                case 0:
                    REQUIRE(item->u64 == IDS[n]);
                    REQUIRE(item->type_name && !strcmp(item->type_name, "ecs.entity"));
                    break;
                case 1:
                case 2:
                    REQUIRE(input_text(item));
                    REQUIRE(!strcmp(
                        pin_text(*item).bytes,
                        a == 2 ? PROTOTYPES[n] : (changed ? CHANGED_NAMES[n] : NAMES[n])
                    ));
                    break;
                case 3: REQUIRE(item->i == PARENTS[n]); break;
                case 4: REQUIRE(item->i == BEHAVIORS[n]); break;
                case 5: REQUIRE(item->i == ICONS[n]); break;
                case 6: REQUIRE(item->i == ASSIGNMENTS[n]); break;
                case 7:
                    REQUIRE(item->b == (r->readonly ? n < 2 : SELECTED[n]));
                    break;
                case 8: REQUIRE(item->i == VIEWS[n]); break;
            }
        }
    }
    REQUIRE(arguments[9].type == PIN_I32 && arguments[9].i == (r->readonly ? 1 : 2));
    REQUIRE(arguments[10].type == PIN_HANDLE && arguments[10].u64 == IDS[1]);
    REQUIRE(arguments[11].type == PIN_BOOL && arguments[11].b == !r->readonly);
    REQUIRE(arguments[12].type == PIN_BOOL && arguments[12].b == !r->readonly);
    REQUIRE(
        arguments[13].type == PIN_STR && input_text(&arguments[13]) &&
        !strcmp(pin_text(arguments[13]).bytes, r->filter)
    );
    REQUIRE(arguments[14].type == PIN_I32 && arguments[14].i == r->first);
    REQUIRE(arguments[15].type == PIN_I32 && arguments[15].i == r->count);
    return true;
}
static bool execute(call *c, behavior_backend backend, const request *r)
{
    clear_call(c);
    behavior_managed_heap_init(&c->heap);
    const behavior_function *f = body();
    memcpy(c->results, f->result_defaults, sizeof c->results);
    behavior_function_prepare_results(f, c->results, &c->storage);
    c->context = (behavior_function_context){ .activation = &c->activation,
                                              .arguments = arguments,
                                              .argument_count = ARGUMENTS,
                                              .results = c->results,
                                              .budget = 10000,
                                              .managed_heap = &c->heap };
    behavior_function_bind_storage(&c->context, &c->storage);
    REQUIRE(behavior_function_bind(f, &c->context, NULL, 0));
    behavior_function_prepare_host(f, &c->context);
    if (behavior_function_resumable(f)) {
        behavior_function_activation_init(f, &c->activation);
    }
    size_t before_used, before_capacity, after_used, after_capacity;
    arena_usage(&c->storage, &before_used, &before_capacity);
    uint64_t before_blocks = arena_block_allocations();
    decimal_calls = match_calls = 0;
    behavior_run_scope_reset(&runs);
    pin_copy_scope_reset(&copies);
    behavior_run_scope_begin(&runs);
    pin_copy_scope_begin(&copies);
    int status = behavior_function_run(f, &c->context, backend != BEHAVIOR_BYTECODE);
    pin_copy_scope_end(&copies);
    behavior_run_scope_end(&runs);
    uint64_t blocks = arena_block_allocations() - before_blocks;
    arena_usage(&c->storage, &after_used, &after_capacity);
    printf(
        "outliner-projection backend=%d first=%d count=%d rows=%d status=%d "
        "decimal=%d match=%d row_bytes=%" PRIu64 " text=%" PRIu64
        " nominal=%" PRIu64 " value_buffer=%" PRIu64 " headers=%" PRIu64
        " bytecode=%" PRIu64 " sljit=%" PRIu64 " wasm=%" PRIu64
        " output_used_before=%zu output_used_after=%zu"
        " output_capacity_before=%zu output_capacity_after=%zu "
        "blocks_delta=%" PRIu64 "\n",
        backend,
        r->first,
        r->count,
        r->row_count,
        status,
        decimal_calls,
        match_calls,
        copies.counts.record_bytes,
        copies.counts.text_bytes,
        copies.counts.nominal_bytes,
        copies.counts.value_buffer_bytes,
        copies.counts.value_header_moves,
        runs.counts.bytecode_entries,
        runs.counts.sljit_entries,
        runs.counts.wasm_entries,
        before_used,
        after_used,
        before_capacity,
        after_capacity,
        blocks
    );
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "outliner execute: %d %s\n",
            status,
            c->context.error_message ? c->context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && c->context.returned);
    REQUIRE(decimal_calls == r->row_count && match_calls == r->match_calls);
    REQUIRE(!copies.counts.overflow && !runs.counts.overflow);
    REQUIRE(
        !runs.counts.inspection_bytecode_fallbacks &&
        !runs.counts.inspected_sljit_entries && !runs.counts.inspected_wasm_entries
    );
    /* Each of three per-row authored helpers enters once, with one parent
     * continuation entry after it, in addition to the initial Project entry. */
    uint64_t expected = 1 + 6 * (uint64_t)r->row_count;
    REQUIRE(runs.counts.bytecode_entries == (backend == BEHAVIOR_BYTECODE ? expected : 0));
    REQUIRE(runs.counts.sljit_entries == (backend == BEHAVIOR_SLJIT ? expected : 0));
    REQUIRE(runs.counts.wasm_entries == (backend == BEHAVIOR_WASM ? expected : 0));
    release_call(c);
    return true;
}
/* The call's managed heap owns its results; a nonempty array's payload is
 * its refcounted backing. */
static bool check(call *c, const request *r, bool changed)
{
    const pin_value *value = &c->results[0];
    const pin_record *record = value->record;
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(record && pin_record_valid(record));
    REQUIRE(record->id == record_id && record->layout == record_layout);
    REQUIRE(record->type.field_count == 12 && arena_contains(&c->heap.storage, record));
    REQUIRE(
        value->type_name == record->type.name &&
        arena_contains(&c->heap.storage, value->type_name)
    );
    REQUIRE(c->results[1].type == PIN_I32 && c->results[1].i == r->shown);
    const pin_array *array = value->ptr;
    REQUIRE(array && arena_contains(&c->heap.storage, array));
    REQUIRE(array->element_type == PIN_STRUCT && pin_record_same(array->record, record));
    REQUIRE(array->type_name == record->type.name && array->count == r->row_count);
    REQUIRE(!array->count || array->backing);
    for (int i = 0; i < array->count; i++) {
        int n = r->rows[i];
        pin_value element = pin_array_get(array, i);
        const pin_value *row = &element;
        REQUIRE(row->type == PIN_STRUCT && row->container == PIN_CONTAINER_SINGLE);
        REQUIRE(
            pin_record_same(row->record, record) &&
            row->type_name == record->type.name
        );
        REQUIRE(row->ptr && arena_contains(&c->heap.storage, row->ptr));
        const char *text[] = { KEYS[n],
                               changed ? CHANGED_NAMES[n] : NAMES[n],
                               GLYPHS[n],
                               PROTOTYPES[n],
                               r->readonly ? "" : DROPS[n] };
        int integers[] = { DEPTHS[n], BEHAVIORS[n] };
        bool active = n == (r->readonly ? 1 : 2);
        bool flags[] = { active,
                         (r->readonly ? n < 2 : SELECTED[n]) && !active,
                         (VIEWS[n] & 1) != 0,
                         (VIEWS[n] & 2) != 0,
                         !r->readonly && n == 1 };
        for (int field = 0; field < 12; field++) {
            const type_field *f = &record->type.fields[field];
            REQUIRE(!strcmp(f->name, FIELDS[field]));
            pin_value member;
            REQUIRE(!pin_read_field(&inputs, row->ptr, f, &member));
            if (field < 5) {
                REQUIRE(member.type == PIN_STR && pin_text(member).bytes);
                /* The row holds its own descriptor; a read shares its bytes. */
                const pin_string *stored = *(const pin_string **)((char *)row->ptr +
                                                                  f->offset);
                REQUIRE(*text[field] ? arena_contains(&c->heap.storage, stored) : !stored);
                REQUIRE(!strcmp(pin_text(member).bytes, text[field]));
            } else if (field < 7) {
                REQUIRE(member.type == PIN_I32 && member.i == integers[field - 5]);
            } else {
                REQUIRE(member.type == PIN_BOOL && member.b == flags[field - 7]);
            }
        }
    }
    return true;
}
static void retire_inputs()
{
    /* Text is immutable and shared, so the inputs are freed rather than
     * poisoned: the results keep their own references. The producer's names
     * are poisoned, since the inputs copied them. */
    for (int a = 0; a < 2; a++) {
        for (int n = 0; n < ACTORS; n++) {
            if (producer_names[a][n]) {
                memset(producer_names[a][n], 'x', strlen(producer_names[a][n]));
            }
        }
    }
    arena_free(&producer);
    arena_free(&inputs);
    memset(producer_names, 0, sizeof producer_names);
    memset(arguments, 0, sizeof arguments);
}
TEST_CASE(outliner_projection_matches_all_fields_and_selected_windows)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(BACKENDS[b]));
        pin_value zero = { .type = PIN_HANDLE, .type_name = "ecs.entity" }, text;
        REQUIRE(!entity_decimal(&inputs, &zero, 1, &text));
        REQUIRE(text.type == PIN_STR && !strcmp(pin_text(text).bytes, "0"));
        REQUIRE(arena_contains(&inputs, text.text));
        for (int i = 0; i < 12; i++) {
            REQUIRE(make_inputs(&REQUESTS[i], false));
            REQUIRE(execute(&calls[0], BACKENDS[b], &REQUESTS[i]));
            REQUIRE(check_inputs(&REQUESTS[i], false));
            REQUIRE(check(&calls[0], &REQUESTS[i], false));
        }
    }
    return true;
}
TEST_CASE(outliner_projection_owns_results_across_producer_and_module_retirement)
{
    const request *r = &REQUESTS[1];
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(BACKENDS[b]));
        REQUIRE(make_inputs(r, false));
        REQUIRE(execute(&calls[0], BACKENDS[b], r));
        REQUIRE(check_inputs(r, false));
        REQUIRE(check(&calls[0], r, false));
        retire_inputs();
        REQUIRE(check(&calls[0], r, false));
        REQUIRE(make_inputs(r, true));
        REQUIRE(execute(&calls[1], BACKENDS[b], r));
        REQUIRE(check_inputs(r, true));
        REQUIRE(check(&calls[0], r, false));
        REQUIRE(check(&calls[1], r, true));
        retire_inputs();
        retire_module();
        REQUIRE(check(&calls[0], r, false));
        REQUIRE(check(&calls[1], r, true));
    }
    return true;
}
void test_language_outliner_projection()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(outliner_projection_matches_all_fields_and_selected_windows);
    RUN_TEST_CASE(outliner_projection_owns_results_across_producer_and_module_retirement);
}
