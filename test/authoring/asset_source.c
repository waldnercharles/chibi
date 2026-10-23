#include "assets/asset_source.h"

#include "authoring/asset_compiler_host.h"
#include "authoring/source_asset.h"
#include "authoring/source_intelligence.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_document.h"
#include "support/binary_cache.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

TEST_CASE(asset_source_navigates_event_payloads)
{
    const char *text = "Payload :: behavior { on Tick :: (dt: float) { "
                       "first:=dt; second:=dt; } }";
    const script_catalog_parameter payload = { .name = "dt",
                                               .type = { .storage = PIN_F32 } };
    const script_catalog_capability event = { .id = 100,
                                              .name = "Tick",
                                              .kind = SCRIPT_CAPABILITY_EVENT,
                                              .parameters = &payload,
                                              .parameter_count = 1,
                                              .completion_outcome = -1 };
    const script_catalog catalog = { .capabilities = &event,
                                     .capability_count = 1,
                                     .tick_event = 100 };
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis->valid);
    const authoring_document document = { .path = "Payload.chibi",
                                          .syntax = syntax,
                                          .analysis = analysis };
    authoring_navigation *navigation = authoring_navigation_new(&document, 1, &catalog);
    int offset = (int)(strstr(text, "first:=dt") - text) + 7;
    const authoring_reference *use = authoring_navigation_at(navigation, 0, offset);
    REQUIRE(use);
    const authoring_reference *definition = authoring_navigation_definition(navigation, use->target);
    REQUIRE(definition && definition->location.range.start == (int)(strstr(text, "dt:") - text));
    int references = 0;
    for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
        const authoring_reference *reference = authoring_navigation_reference_at(navigation, i);
        references += reference->kind == AUTHORING_READ &&
                      authoring_target_equal(reference->target, use->target);
    }
    REQUIRE(references == 2);
    char *hover = authoring_source_hover(navigation, 0, offset);
    REQUIRE(hover && !strcmp(hover, "dt: float"));
    free(hover);
    authoring_navigation_free(navigation);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return true;
}

/* Canonical source edits preserve values and source-owned helper computations. */
TEST_CASE(asset_source_reconcile_keeps_authored_text)
{
    char error[256];
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "amount", 12);
    av_value *list = av_obj_add_arr(doc, root, "items");
    av_arr_append(list, av_sint(doc, 1));
    char *source = asset_document_write(root);
    REQUIRE(source);
    av_obj_set(doc, root, "amount", av_sint(doc, 24));
    av_arr_append(list, av_sint(doc, 2));
    char *edited = asset_source_reconcile(source, root, error, sizeof error);
    REQUIRE(edited);
    av_doc *check = asset_document_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    free(source);
    av_doc_free(doc);

    source = xstrdup(
        "package asset\nimport data \"game:assets\"\n"
        "// keep helper and formula\n"
        "Amount :: (base: int) -> int { return base * 2 }\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object(["
        "data.Field.Named(\"amount\", data.Value.Int(int64(Amount(6))))])) }\n"
    );
    doc = asset_document_read(source, error, sizeof error);
    REQUIRE(doc);
    edited = asset_source_reconcile(source, av_doc_root(doc), error, sizeof error);
    REQUIRE(edited && !strcmp(source, edited));
    free(edited);
    av_obj_add_arr(doc, av_doc_root(doc), "new_items");
    edited = asset_source_reconcile(source, av_doc_root(doc), error, sizeof error);
    /* An unsupported edit must not erase the helper/formula to serialize data. */
    REQUIRE(!edited || (strstr(edited, "Amount ::") && strstr(edited, "Amount(6)")));
    free(edited);
    av_doc_free(doc);
    free(source);
    return true;
}

/* Incomplete ordinary Chibi retains token highlighting and source diagnostics. */
TEST_CASE(asset_source_parses_incomplete_drafts)
{
    const char
        *text = "package asset\nimport data \"game:assets\"\n"
                "pub Build :: () -> byte[] { return data.Encode(data.Object(["
                "data.Field.Named(\"name\", data.Value.Text(\"unterminated";
    asset_source *source = asset_source_parse(text);
    REQUIRE(!source->document && acount(source->diagnostics) > 0);
    REQUIRE(acount(source->spans) > 0);
    int line = 0, column = 0;
    asset_source_position(source, (int)(strstr(text, "pub Build") - text), &line, &column);
    REQUIRE(line == 3 && column == 1);
    bool string = false;
    for (int i = 0; i < acount(source->spans); i++) {
        string |= !strcmp(source->spans[i].cls, "tok-string");
    }
    REQUIRE(string);
    asset_source_free(source);
    return true;
}

TEST_CASE(asset_source_reconciles_generated_subnormal_literals)
{
    char error[256];
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_set(doc, root, "tiny", av_real(doc, 0x1p-1074));
    char *source = asset_document_write(root);
    REQUIRE(source && !strstr(source, "Overrides"));
    const char *operator = strstr(source, "* 2.2204460492503131e-16");
    REQUIRE(operator);
    const char *comment = "/* keep scaled value */ ";
    char *commented = xmalloc(strlen(source) + strlen(comment) + 1);
    snprintf(
        commented,
        strlen(source) + strlen(comment) + 1,
        "%.*s%s%s",
        (int)(operator - source),
        source,
        comment,
        operator
    );
    free(source);
    av_obj_set(doc, root, "tiny", av_real(doc, -0x1.8p-1023));
    char *edited = asset_source_reconcile(commented, root, error, sizeof error);
    REQUIRE(edited && strstr(edited, "/* keep scaled value */") && !strstr(edited, "Overrides"));
    av_doc *check = asset_document_read(edited, error, sizeof error);
    REQUIRE(check && av_get_type(av_obj_get(av_doc_root(check), "tiny")) == AV_REAL);
    REQUIRE(av_get_num(av_obj_get(av_doc_root(check), "tiny")) == -0x1.8p-1023);
    av_doc_free(check);
    free(edited);
    free(commented);
    av_doc_free(doc);
    return true;
}

TEST_CASE(asset_source_reads_and_sets_scaled_real_parameters)
{
    const char *source =
        "package asset\nimport data \"game:assets\"\n"
        "pub Parameters :: () -> data.Value { return data.Object(["
        "data.Field.Named(\"tiny\", data.Value.Real(2.2250738585072014e-308 "
        "/* keep parameter */ * 2.2204460492503131e-16))]); }\n"
        "pub Build :: () -> byte[] { return data.Encode(Parameters()); }\n";
    char error[256];
    av_doc *parameters = authoring_prototype_source_parameters(source, error, sizeof error);
    REQUIRE(parameters && av_get_type(av_obj_get(av_doc_root(parameters), "tiny")) == AV_REAL);
    REQUIRE(av_get_num(av_obj_get(av_doc_root(parameters), "tiny")) == 0x1p-1074);
    av_doc_free(parameters);
    av_doc *request = av_doc_new();
    av_value *args = av_arr(request);
    av_doc_set_root(request, args);
    av_arr_add_str(request, args, source);
    av_arr_add_str(request, args, "tiny");
    av_arr_append(args, av_sint(request, 4));
    av_arr_add_str(request, args, "");
    av_arr_append(args, av_sint(request, 0));
    av_arr_append(args, av_uint(request, 0));
    av_arr_append(args, av_real(request, -0x1.8p-1023));
    av_arr_append(args, av_bool(request, false));
    asset_compiler_diagnostic diagnostic = { 0 };
    char *edited = asset_source_invoke_bytes(
        "project:asset_compiler:1:SetParameter:callable",
        args,
        NULL,
        &diagnostic
    );
    av_doc_free(request);
    REQUIRE(edited && strstr(edited, "/* keep parameter */"));
    parameters = authoring_prototype_source_parameters(edited, error, sizeof error);
    REQUIRE(parameters && av_get_type(av_obj_get(av_doc_root(parameters), "tiny")) == AV_REAL);
    REQUIRE(av_get_num(av_obj_get(av_doc_root(parameters), "tiny")) == -0x1.8p-1023);
    av_doc_free(parameters);
    av_doc *built = asset_document_read(edited, error, sizeof error);
    REQUIRE(built && av_get_type(av_obj_get(av_doc_root(built), "tiny")) == AV_REAL);
    REQUIRE(av_get_num(av_obj_get(av_doc_root(built), "tiny")) == -0x1.8p-1023);
    av_doc_free(built);
    free(edited);
    /* This spelling exception does not admit arbitrary arithmetic or helpers. */
    const char *computed[] = {
        "2.2250738585072014e-308 * 2.2204460492503131e-15",
        "Other() * 2.2204460492503131e-16",
    };
    for (size_t i = 0; i < sizeof computed / sizeof *computed; i++) {
        char rejected[512];
        snprintf(
            rejected,
            sizeof rejected,
            "package asset\nimport data \"game:assets\"\n"
            "pub Parameters :: () -> data.Value { return data.Object(["
            "data.Field.Named(\"tiny\", data.Value.Real(%s))]); }\n"
            "pub Build :: () -> byte[] { return data.Encode(Parameters()); }\n",
            computed[i]
        );
        parameters = authoring_prototype_source_parameters(rejected, error, sizeof error);
        bool refused = parameters == NULL;
        av_doc_free(parameters);
        REQUIRE(refused);
    }
    return true;
}

/* Generic edits request numeric identity; reflected native edits may retain
 * an existing integral constructor, including exact values above 2^53. */
TEST_CASE(asset_source_native_reconcile_preserves_only_exact_integer_spelling)
{
    const struct
    {
        const char *name;
        bool unsigned_before;
        double real;
        av_type native_kind;
        int64_t signed_value;
        uint64_t unsigned_value;
    } values[] = {
        { "signed_min", false, -0x1p63, AV_SINT, INT64_MIN, 0 },
        { "signed_below_max", false, 0x1.fffffffffffffp62, AV_SINT, INT64_C(9223372036854774784), 0 },
        { "signed_overflow", false, 0x1p63, AV_REAL, 0, 0 },
        { "unsigned_below_max", true, 0x1.fffffffffffffp63, AV_UINT, 0, UINT64_C(18446744073709549568) },
        { "unsigned_overflow", true, 0x1p64, AV_REAL, 0, 0 },
        { "unsigned_negative", true, -1.0, AV_REAL, 0, 0 },
        { "fraction", false, 1.5, AV_REAL, 0, 0 },
        { "negative_zero", false, -0.0, AV_REAL, 0, 0 },
        { "positive_zero", false, 0.0, AV_SINT, 0, 0 },
        { "large_exact", false, 0x1p53, AV_SINT, INT64_C(9007199254740992), 0 },
    };
    av_doc *before = av_doc_new(), *after = av_doc_new();
    av_value *old = av_obj(before), *next = av_obj(after);
    av_doc_set_root(before, old);
    av_doc_set_root(after, next);
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        av_obj_set(
            before,
            old,
            values[i].name,
            values[i].unsigned_before ? av_uint(before, UINT64_MAX) : av_sint(before, 1)
        );
        av_obj_set(after, next, values[i].name, av_real(after, values[i].real));
    }
    av_obj_set(before, old, "real_zero", av_real(before, 0.0));
    av_obj_set(after, next, "real_zero", av_real(after, -0.0));
    char *source = asset_document_write(old);
    REQUIRE(source);
    // Prefixing a comment exercises scalar patching rather than the canonical
    // full-document replacement shortcut, including the sign-only zero edit.
    const char *comment = "/* preserve numeric source */\n";
    char *commented = xmalloc(strlen(comment) + strlen(source) + 1);
    sprintf(commented, "%s%s", comment, source);
    free(source);
    asset_compiler_diagnostic diagnostic = { 0 };
    char *generic = asset_source_reconcile_compiled(commented, old, next, NULL, &diagnostic);
    REQUIRE(generic && strstr(generic, comment));
    char error[256];
    av_doc *check = asset_document_read(generic, error, sizeof error);
    REQUIRE(check);
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        const av_value *value = av_obj_get(av_doc_root(check), values[i].name);
        REQUIRE(av_get_type(value) == AV_REAL && av_get_num(value) == values[i].real);
        REQUIRE(!!signbit(av_get_num(value)) == !!signbit(values[i].real));
    }
    const av_value *zero = av_obj_get(av_doc_root(check), "real_zero");
    REQUIRE(av_get_type(zero) == AV_REAL && signbit(av_get_num(zero)));
    av_doc_free(check);
    free(generic);
    char *native = asset_source_reconcile_native_compiled(commented, old, next, NULL, &diagnostic);
    REQUIRE(native && strstr(native, comment));
    check = asset_document_read(native, error, sizeof error);
    REQUIRE(check);
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        const av_value *value = av_obj_get(av_doc_root(check), values[i].name);
        REQUIRE(av_get_type(value) == values[i].native_kind);
        if (values[i].native_kind == AV_SINT) {
            REQUIRE(av_get_sint(value) == values[i].signed_value);
        }
        if (values[i].native_kind == AV_UINT) {
            REQUIRE(av_get_uint(value) == values[i].unsigned_value);
        }
        if (values[i].native_kind == AV_REAL) {
            REQUIRE(av_get_num(value) == values[i].real);
            REQUIRE(!!signbit(av_get_num(value)) == !!signbit(values[i].real));
        }
    }
    zero = av_obj_get(av_doc_root(check), "real_zero");
    REQUIRE(av_get_type(zero) == AV_REAL && signbit(av_get_num(zero)));
    av_doc *zeros = av_doc_new();
    av_doc_set_root(zeros, av_clone(zeros, av_doc_root(check)));
    av_obj_set(zeros, av_doc_root(zeros), "real_zero", av_real(zeros, 0.0));
    char *positive = asset_source_reconcile_compiled(
        native,
        av_doc_root(check),
        av_doc_root(zeros),
        NULL,
        &diagnostic
    );
    REQUIRE(positive && strstr(positive, comment));
    av_doc *positive_doc = asset_document_read(positive, error, sizeof error);
    REQUIRE(positive_doc);
    zero = av_obj_get(av_doc_root(positive_doc), "real_zero");
    REQUIRE(
        av_get_type(zero) == AV_REAL && av_get_num(zero) == 0.0 &&
        !signbit(av_get_num(zero))
    );
    av_doc_free(positive_doc);
    av_doc_free(zeros);
    free(positive);
    av_doc_free(check);
    free(native);
    free(commented);
    av_doc_free(before);
    av_doc_free(after);
    return true;
}

void test_asset_source()
{
    RUN_TEST_CASE(asset_source_navigates_event_payloads);
    RUN_TEST_CASE(asset_source_reconcile_keeps_authored_text);
    RUN_TEST_CASE(asset_source_parses_incomplete_drafts);
    RUN_TEST_CASE(asset_source_reconciles_generated_subnormal_literals);
    RUN_TEST_CASE(asset_source_reads_and_sets_scaled_real_parameters);
    RUN_TEST_CASE(asset_source_native_reconcile_preserves_only_exact_integer_spelling);
}

/* Exercise raw Build transport, bypassing game:assets.Encode's own checks. */
static av_doc *transport_document;
static char *transport_source;

static void transport_cleanup()
{
    av_doc_free(transport_document);
    free(transport_source);
    transport_document = NULL;
    transport_source = NULL;
}

TEST_CASE(asset_transport_checks_raw_build_depth)
{
    char source[512], error[256];
    for (int depth = 1024; depth <= 1025; depth++) {
        int length = snprintf(
            source,
            sizeof source,
            "package asset\n"
            "pub Build :: () -> byte[] {\n"
            " out: byte[] = []; i: int = 0;\n"
            " while (i < %d) { out.append(91); i += 1; }\n"
            " out.append(48); i = 0;\n"
            " while (i < %d) { out.append(93); i += 1; }\n"
            " return out\n}\n",
            depth,
            depth
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        transport_document = asset_document_read(source, error, sizeof error);
        if (!transport_document) {
            fprintf(stderr, "raw Build depth %d: %s\n", depth, error);
        }
        if (depth == 1025) {
            REQUIRE(!transport_document && strstr(error, "value/depth"));
            continue;
        }
        REQUIRE(transport_document);
        const av_value *value = av_doc_root(transport_document);
        for (int i = 0; i < depth; i++) {
            REQUIRE(av_is_arr(value) && av_arr_size(value) == 1);
            value = av_arr_get(value, 0);
        }
        REQUIRE(av_get_int(value) == 0);
        transport_cleanup();
    }
    return true;
}

TEST_CASE(asset_transport_rejects_escaped_nul)
{
    /* JSON {"x":"a\u0000b"} and {"a\u0000b":0}, not raw NUL bytes. */
    const char *sources[] = {
        "package asset\npub Build :: () -> byte[] { return "
        "[123,34,120,34,58,34,97,92,117,48,48,48,48,98,34,125] }\n",
        "package asset\npub Build :: () -> byte[] { return "
        "[123,34,97,92,117,48,48,48,48,98,34,58,48,125] }\n",
    };
    char error[256];
    for (int i = 0; i < 2; i++) {
        transport_document = asset_document_read(sources[i], error, sizeof error);
        REQUIRE(!transport_document && strstr(error, "NUL"));
    }
    return true;
}

static av_doc *transport_bytes(const char *bytes, char *error, size_t error_size)
{
    size_t length = strlen(bytes), capacity = 6 * length + 128;
    char *source = xmalloc(capacity);
    int used = snprintf(source, capacity, "package asset\npub Build :: () -> byte[] { return [");
    for (size_t i = 0; i < length; i++) {
        used += snprintf(
            source + used,
            capacity - (size_t)used,
            "%s%u",
            i ? "," : "",
            (unsigned char)bytes[i]
        );
    }
    snprintf(source + used, capacity - (size_t)used, "] }\n");
    av_doc *doc = asset_document_read(source, error, error_size);
    free(source);
    return doc;
}

TEST_CASE(asset_transport_checks_numeric_metadata)
{
    const char *invalid[] = {
        "CHIBI-ASSET1\n{\"value\":0}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"i\",\"extra\":0}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":0}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"\"}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"ii\"}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"x\"}",
        "CHIBI-ASSET1\n{\"value\":18446744073709551615,\"numbers\":\"i\"}",
        "CHIBI-ASSET1\n{\"value\":-1,\"numbers\":\"u\"}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"r\"}",
        "CHIBI-ASSET1\n{\"value\":0.0,\"numbers\":\"i\"}",
        "CHIBI-ASSET1\n{\"value\":0,\"numbers\":\"i\\u0000\"}",
    };
    char error[256];
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        transport_document = transport_bytes(invalid[i], error, sizeof error);
        REQUIRE(!transport_document && strstr(error, "numeric transport"));
    }
    /* An ordinary raw JSON object cannot be mistaken for the typed envelope. */
    transport_document = transport_bytes("{\"value\":0,\"numbers\":\"i\"}", error, sizeof error);
    REQUIRE(transport_document && av_is_obj(av_doc_root(transport_document)));
    REQUIRE(av_is_uint(av_obj_get(av_doc_root(transport_document), "value")));
    transport_cleanup();
    transport_document = transport_bytes("CHIBI-ASSET1\n{\"value\":[1,18446744073709551615,1.0,-1],\"numbers\":\"iuri\"}", error, sizeof error);
    REQUIRE(transport_document);
    const av_value *values = av_doc_root(transport_document);
    REQUIRE(av_is_sint(av_arr_get(values, 0)) && av_get_sint(av_arr_get(values, 0)) == 1);
    REQUIRE(av_is_uint(av_arr_get(values, 1)) && av_get_uint(av_arr_get(values, 1)) == UINT64_MAX);
    REQUIRE(
        av_get_type(av_arr_get(values, 2)) == AV_REAL &&
        av_get_num(av_arr_get(values, 2)) == 1.0
    );
    REQUIRE(av_is_sint(av_arr_get(values, 3)) && av_get_sint(av_arr_get(values, 3)) == -1);
    return true;
}

static av_doc *transport_nested(int depth)
{
    av_doc *doc = av_doc_new();
    av_value *value = av_arr(doc);
    av_doc_set_root(doc, value);
    for (int i = 1; i < depth; i++) {
        av_value *child = av_arr(doc);
        av_arr_append(value, child);
        value = child;
    }
    av_arr_append(value, av_sint(doc, 0));
    return doc;
}

TEST_CASE(asset_transport_writer_matches_read_depth)
{
    char error[256];
    /* A real round trip beyond the former native projection ceiling. */
    transport_document = transport_nested(65);
    transport_source = asset_document_write(av_doc_root(transport_document));
    REQUIRE(transport_source);
    av_doc *copy = asset_document_read(transport_source, error, sizeof error);
    bool equal = copy &&
                 av_equals(av_doc_root(copy), av_doc_root(transport_document));
    av_doc_free(copy);
    REQUIRE(equal);
    transport_cleanup();

    /* Projection accepts the same inclusive boundary as raw Build reading.
     * Do not compile a generated 1024-level constructor expression here. */
    for (int depth = 1024; depth <= 1025; depth++) {
        transport_document = transport_nested(depth);
        av_value *parent = av_doc_root(transport_document);
        for (int i = 1; i < depth; i++) {
            parent = av_arr_get(parent, 0);
        }
        /* Reuse the chain with a scalar, empty array, then empty object at
         * precisely the same leaf depth. Containers consume no child depth. */
        for (int kind = 0; kind < 3; kind++) {
            if (kind) {
                av_arr_replace(parent, 0, kind == 1 ? av_arr(transport_document) : av_obj(transport_document));
            }
            transport_source = asset_document_write(av_doc_root(transport_document));
            REQUIRE(depth == 1024 ? transport_source != NULL : transport_source == NULL);
            free(transport_source);
            transport_source = NULL;
        }
        transport_cleanup();
    }
    return true;
}

void test_asset_transport()
{
    pu_setup(NULL, transport_cleanup);
    RUN_TEST_CASE(asset_transport_checks_raw_build_depth);
    RUN_TEST_CASE(asset_transport_rejects_escaped_nul);
    RUN_TEST_CASE(asset_transport_checks_numeric_metadata);
    RUN_TEST_CASE(asset_transport_writer_matches_read_depth);
    pu_clear_setup();
}

static const char
    *cache_source = "package asset\nimport data \"game:assets\"\n"
                    "import helper \"game:cache_fixture\"\n"
                    "pub Build :: () -> byte[] { return "
                    "data.Encode(data.Value.Int(int64(helper.Value()))) }\n";
static const char
    *cache_changed_source = "package asset\nimport data \"game:assets\"\n"
                            "import helper \"game:cache_fixture\"\n"
                            "pub Build :: () -> byte[] { return "
                            "data.Encode(data.Value.Int(int64(helper.Value() + "
                            "1))) }\n";
static const char *cache_helper = "package cache_fixture\npub Value :: () -> "
                                  "int { return 7 }\n";
/* Use the same nested union/array aggregate shape as game:assets.Value. */
static const char *cache_mutating_source =
    "package asset\nimport data \"game:assets\"\n"
    "Row :: union { Seed(values: int[]) }\n"
    "SeedRow :: () -> Row { return Row.Seed([1, 2]) }\n"
    "First :: (row: Row) -> int {\n"
    " switch row { case .Seed(values): return values[0]; } return 0;\n}\n"
    "pub Build :: () -> byte[] {\n"
    " original := SeedRow(); answer: int = 0;\n"
    " switch original { case .Seed(values): changed := values; "
    "changed[0] += 6; answer = changed[0]; }\n"
    " assert(First(original) == 1); i: int = 0;\n"
    " while (i < 200) { i += 1; }\n"
    " return data.Encode(data.Value.Int(int64(answer)))\n}\n";
static char cache_directory[256];
static asset_source_library *cache_libraries;
static unsigned char *cache_bad_compiler;
static const char *cache_executable;

static asset_compiler_options cache_options(const char *directory)
{
    asset_compiler_options options = *asset_source_default_options();
    cache_libraries = xcalloc((size_t)options.library_count + 1, sizeof *cache_libraries);
    memcpy(cache_libraries, options.libraries, (size_t)options.library_count * sizeof *cache_libraries);
    cache_libraries[options.library_count] = (asset_source_library){
        "/sdk/game/cache_fixture/Helper.chibi",
        cache_helper
    };
    options.libraries = cache_libraries;
    options.library_count++;
    options.cache_directory = directory;
    return options;
}

static void cache_cleanup()
{
    asset_source_release_thread();
    free(cache_libraries);
    free(cache_bad_compiler);
    cache_libraries = NULL;
    cache_bad_compiler = NULL;
    if (cache_directory[0]) {
        chmod(cache_directory, 0700);
        tool_remove_tree(cache_directory);
        cache_directory[0] = 0;
    }
}

static bool cache_value(const char *source, asset_compiler_options *options, int expected)
{
    asset_compiler_diagnostic diagnostic;
    av_doc *doc = asset_source_read(source, NULL, options, &diagnostic);
    bool valid = doc && av_get_int(av_doc_root(doc)) == expected &&
                 !diagnostic.message[0];
    if (!valid) {
        fprintf(stderr, "asset cache read: %s\n", diagnostic.message);
    }
    av_doc_free(doc);
    return valid;
}

/* Exec the same installed-SDK consumer; no inherited compiler or memory cache. */
int test_asset_cache_worker(const char *directory)
{
    asset_compiler_options options = cache_options(directory);
    options.evaluation_budget = 1;
    asset_compiler_diagnostic diagnostic;
    av_doc *doc = asset_source_read(cache_source, NULL, &options, &diagnostic);
    bool limited = !doc && strstr(diagnostic.message, "budget");
    av_doc_free(doc);
    options.evaluation_budget = 0;
    bool valid = limited && cache_value(cache_source, &options, 7);
    asset_compiler_cache_stats stats = asset_source_cache_stats();
    valid &= stats.compile_attempts == 0 && stats.persistent_hits == 1 &&
             stats.memory_hits == 1 && stats.preparation_attempts == 1 &&
             stats.evaluations == 2;
    cache_cleanup();
    return valid ? 0 : 1;
}

/* Arm after the second cold compilation starts: cache-key evaluation and the
 * public preflight cancellation poll must not consume the compiler's ticks. */
static bool cache_cancel_compilation(void *user)
{
    if (asset_source_cache_stats().compile_attempts < 2) {
        return false;
    }
    int *remaining = user;
    return --*remaining <= 0;
}

typedef struct
{
    uint64_t evaluations;
    int polls;
    bool fired, held, release, nested_ok;
    asset_compiler_options nested;
} cache_callback;

static bool cache_cancel_evaluation(void *user)
{
    cache_callback *callback = user;
    if (asset_source_cache_stats().evaluations <= callback->evaluations) {
        return false;
    }
    return ++callback->polls == 128;
}

static bool cache_reenter_evaluation(void *user)
{
    cache_callback *callback = user;
    if (callback->fired ||
        asset_source_cache_stats().evaluations <= callback->evaluations ||
        ++callback->polls < 128) {
        return false;
    }
    callback->fired = true;
    if (callback->release) {
        asset_source_release_thread();
    } else {
        callback->nested_ok = cache_value(cache_source, &callback->nested, 9);
    }
    size_t resident = asset_source_cache_stats().resident_bytes;
    callback->held = resident > 0 && resident <= 128u * 1024u * 1024u;
    return false;
}

TEST_CASE(asset_cache_reuses_invalidates_and_recovers)
{
    snprintf(cache_directory, sizeof cache_directory, "/tmp/chibi-asset-cache-XXXXXX");
    REQUIRE(mkdtemp(cache_directory));
    asset_source_release_thread();
    asset_compiler_options options = cache_options(cache_directory);
    options.compile_budget = 1;
    asset_compiler_diagnostic failed;
    av_doc *rejected = asset_source_read(cache_source, NULL, &options, &failed);
    bool limited = !rejected && strstr(failed.message, "budget");
    av_doc_free(rejected);
    REQUIRE(limited);
    REQUIRE(asset_source_cache_stats().compile_attempts == 1);
    binary_cache_entry *failed_entries = NULL;
    int failed_count = 0;
    REQUIRE(binary_cache_list(cache_directory, &failed_entries, &failed_count));
    free(failed_entries);
    REQUIRE(!failed_count);

    options.compile_budget = 0;
    int remaining = 10;
    options.cancelled = cache_cancel_compilation;
    options.user = &remaining;
    rejected = asset_source_read(cache_source, NULL, &options, &failed);
    bool cancelled = !rejected && strstr(failed.message, "cancelled");
    av_doc_free(rejected);
    REQUIRE(cancelled && remaining <= 0);
    REQUIRE(asset_source_cache_stats().compile_attempts == 2);
    failed_entries = NULL;
    REQUIRE(binary_cache_list(cache_directory, &failed_entries, &failed_count));
    free(failed_entries);
    REQUIRE(!failed_count);

    options.cancelled = NULL;
    options.user = NULL;
    REQUIRE(cache_value(cache_source, &options, 7));
    asset_compiler_cache_stats stats = asset_source_cache_stats();
    REQUIRE(stats.compile_attempts == 3 && !stats.persistent_hits && !stats.memory_hits);
    binary_cache_entry *entries = NULL;
    int count = 0;
    REQUIRE(binary_cache_list(cache_directory, &entries, &count));
    char first[BINARY_CACHE_KEY_BYTES + 1] = "";
    if (count == 1) {
        strcpy(first, entries[0].key);
    }
    free(entries);
    REQUIRE(count == 1);
    char log[512];
    snprintf(log, sizeof log, "%s/worker.log", cache_directory);
    const char *args[] = { cache_executable, "--asset-cache-worker", cache_directory, NULL };
    REQUIRE(!tool_run(args, ".", log));
    REQUIRE(cache_value(cache_source, &options, 7));
    REQUIRE(asset_source_cache_stats().memory_hits == 1);

    REQUIRE(cache_value(cache_changed_source, &options, 8));
    REQUIRE(asset_source_cache_stats().compile_attempts == 4);
    /* Replace the immutable snapshot; cache identity includes library contents. */
    asset_source_library *changed = xcalloc((size_t)options.library_count, sizeof *changed);
    memcpy(changed, cache_libraries, (size_t)options.library_count * sizeof *changed);
    changed[options.library_count - 1]
        .source = "package cache_fixture\npub Value :: () -> int { return 9 "
                  "}\n";
    asset_source_release_thread();
    free(cache_libraries);
    cache_libraries = changed;
    options.libraries = changed;
    char blocked[512];
    snprintf(blocked, sizeof blocked, "%s/not-a-directory", cache_directory);
    tool_write(blocked, "unavailable cache storage");
    options.cache_directory = blocked;
    REQUIRE(cache_value(cache_source, &options, 9));
    REQUIRE(asset_source_cache_stats().compile_attempts == 1);
    REQUIRE(cache_value(cache_source, &options, 9));
    REQUIRE(asset_source_cache_stats().memory_hits == 1);

    /* Compiler identity is its bytes, not the allocation address. */
    asset_source_release_thread();
    cache_libraries[options.library_count - 1].source = cache_helper;
    options.cache_directory = cache_directory;
    cache_bad_compiler = xmalloc(options.compiler_length);
    memcpy(cache_bad_compiler, options.compiler, options.compiler_length);
    const void *compiler = options.compiler;
    options.compiler = cache_bad_compiler;
    REQUIRE(cache_value(cache_source, &options, 7));
    REQUIRE(
        asset_source_cache_stats().persistent_hits == 1 &&
        !asset_source_cache_stats().compile_attempts
    );
    /* An invalid different compiler must not reuse the previous valid code. */
    asset_source_release_thread();
    cache_bad_compiler[0] ^= 255;
    asset_compiler_diagnostic diagnostic;
    av_doc *doc = asset_source_read(cache_source, NULL, &options, &diagnostic);
    bool refused = !doc && diagnostic.message[0];
    av_doc_free(doc);
    REQUIRE(refused);
    REQUIRE(
        !asset_source_cache_stats().memory_hits && !asset_source_cache_stats().persistent_hits
    );
    options.compiler = compiler;

    /* Restore the original snapshot, poison its stored envelope, and repair it. */
    asset_source_release_thread();
    cache_libraries[options.library_count - 1].source = cache_helper;
    options.cache_directory = cache_directory;
    size_t stored_length = 0;
    unsigned char *stored = binary_cache_read(cache_directory, first, 16777216, &stored_length);
    bool framed = stored && stored_length >= 29;
    if (framed) {
        memset(stored + 1, 255, 8);
    }
    bool poisoned = framed &&
                    binary_cache_write(cache_directory, first, stored, stored_length, 16777216);
    free(stored);
    REQUIRE(poisoned);
    /* Framing/checksum are valid; the nonzero entry names no callable. */
    REQUIRE(cache_value(cache_source, &options, 7));
    REQUIRE(asset_source_cache_stats().compile_attempts == 1);
    REQUIRE(!asset_source_cache_stats().persistent_hits);
    asset_source_release_thread();
    REQUIRE(cache_value(cache_source, &options, 7));
    REQUIRE(
        asset_source_cache_stats().persistent_hits == 1 &&
        !asset_source_cache_stats().compile_attempts
    );
    /* Decode once, but reevaluate on every read with fresh nested values. */
    asset_source_release_thread();
    options.cache_directory = "";
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    stats = asset_source_cache_stats();
    REQUIRE(stats.preparation_attempts == 1 && stats.evaluations == 1);
    REQUIRE(stats.resident_bytes > 0 && stats.resident_bytes <= 128u * 1024u * 1024u);
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    options.evaluation_budget = 1;
    rejected = asset_source_read(cache_mutating_source, NULL, &options, &failed);
    limited = !rejected && strstr(failed.message, "budget");
    av_doc_free(rejected);
    REQUIRE(limited);
    options.evaluation_budget = 0;
    cache_callback callback = { .evaluations = asset_source_cache_stats().evaluations };
    options.cancelled = cache_cancel_evaluation;
    options.user = &callback;
    rejected = asset_source_read(cache_mutating_source, NULL, &options, &failed);
    cancelled = !rejected && strstr(failed.message, "cancelled");
    av_doc_free(rejected);
    REQUIRE(cancelled && callback.polls == 128);
    options.cancelled = NULL;
    options.user = NULL;
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    stats = asset_source_cache_stats();
    REQUIRE(stats.preparation_attempts == 1 && stats.evaluations == 5);

    /* Thread release detaches the cache, while this invocation owns its lease. */
    callback = (cache_callback){ .evaluations = stats.evaluations, .release = true };
    options.cancelled = cache_reenter_evaluation;
    options.user = &callback;
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    REQUIRE(callback.fired && callback.held);
    REQUIRE(asset_source_cache_stats().resident_bytes == 0);
    options.cancelled = NULL;
    options.user = NULL;
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    REQUIRE(asset_source_cache_stats().preparation_attempts == 1);

    /* A nested read replaces the immutable library snapshot during Build. */
    changed = xcalloc((size_t)options.library_count, sizeof *changed);
    memcpy(changed, cache_libraries, (size_t)options.library_count * sizeof *changed);
    changed[options.library_count - 1]
        .source = "package cache_fixture\npub Value :: () -> int { return 9 "
                  "}\n";
    callback = (cache_callback){ .evaluations = asset_source_cache_stats().evaluations,
                                 .nested = options };
    callback.nested.libraries = changed;
    options.cancelled = cache_reenter_evaluation;
    options.user = &callback;
    bool survived = cache_value(cache_mutating_source, &options, 7);
    free(changed);
    REQUIRE(survived && callback.fired && callback.nested_ok && callback.held);
    options.cancelled = NULL;
    options.user = NULL;
    REQUIRE(cache_value(cache_mutating_source, &options, 7));
    asset_source_release_thread();
    REQUIRE(asset_source_cache_stats().resident_bytes == 0);
    return true;
}

TEST_CASE(asset_cache_backend_and_retention)
{
    snprintf(cache_directory, sizeof cache_directory, "/tmp/chibi-asset-cache-XXXXXX");
    REQUIRE(mkdtemp(cache_directory));
    REQUIRE(binary_cache_write(cache_directory, "small", "first", 5, 16));
    REQUIRE(!binary_cache_write(cache_directory, "small", "too-large", 9, 4));
    size_t length = 0;
    unsigned char *bytes = binary_cache_read(cache_directory, "small", 16, &length);
    bool preserved = bytes && length == 5 && !memcmp(bytes, "first", 5);
    free(bytes);
    REQUIRE(preserved);
    REQUIRE(binary_cache_write(cache_directory, "small", "replacement", 11, 16));
    bytes = binary_cache_read(cache_directory, "small", 16, &length);
    bool replaced = bytes && length == 11 && !memcmp(bytes, "replacement", 11);
    free(bytes);
    REQUIRE(replaced);
    char path[512];
    snprintf(path, sizeof path, "%s/small.bin", cache_directory);
    tool_write(path, "corrupt frame");
    bytes = binary_cache_read(cache_directory, "small", 16, &length);
    bool corrupt = !bytes && !length;
    free(bytes);
    REQUIRE(corrupt);
    REQUIRE(chmod(cache_directory, 0500) == 0);
    bool readonly = !binary_cache_write(cache_directory, "readonly", "x", 1, 16);
    REQUIRE(chmod(cache_directory, 0700) == 0);
    REQUIRE(readonly);

    /* Three metadata rows force eviction of the oldest, not filename order. */
    av_doc *args = av_doc_new();
    av_value *values = av_arr(args), *sizes = av_arr(args), *stamps = av_arr(args);
    av_doc_set_root(args, values);
    av_arr_append(values, sizes);
    av_arr_append(values, stamps);
    av_arr_append(values, av_uint(args, 100000000));
    av_arr_append(sizes, av_uint(args, 100000000));
    av_arr_append(sizes, av_uint(args, 100000000));
    av_arr_append(sizes, av_uint(args, 3));
    av_arr_append(stamps, av_uint(args, 20));
    av_arr_append(stamps, av_uint(args, 10));
    av_arr_append(stamps, av_uint(args, 30));
    asset_compiler_options options = *asset_source_default_options();
    options.cache_directory = "";
    asset_compiler_diagnostic diagnostic;
    av_doc *plan = asset_source_invoke(
        "project:asset_compiler:1:CacheEvict:callable",
        values,
        &options,
        &diagnostic
    );
    const av_value *result = plan ? av_obj_get(av_arr_get(av_doc_root(plan), 0), "values")
                                  : NULL;
    const av_value *remove = av_arr_get(result, 1);
    bool oldest = av_arr_size(result) == 2 && av_get_bool(av_arr_get(result, 0)) &&
                  av_arr_size(remove) == 1 && av_get_int(av_arr_get(remove, 0)) == 1;
    av_doc_free(plan);
    REQUIRE(oldest);
    av_arr_replace(values, 2, av_uint(args, UINT64_C(268435457)));
    plan = asset_source_invoke("project:asset_compiler:1:CacheEvict:callable", values, &options, &diagnostic);
    result = plan ? av_obj_get(av_arr_get(av_doc_root(plan), 0), "values") : NULL;
    bool oversized = av_arr_size(result) == 2 &&
                     !av_get_bool(av_arr_get(result, 0)) &&
                     !av_arr_size(av_arr_get(result, 1));
    av_doc_free(plan);
    av_doc_free(args);
    REQUIRE(oversized);
    return true;
}

void test_asset_cache(const char *executable)
{
    cache_executable = executable;
    pu_setup(NULL, cache_cleanup);
    RUN_TEST_CASE(asset_cache_reuses_invalidates_and_recovers);
    RUN_TEST_CASE(asset_cache_backend_and_retention);
    pu_clear_setup();
}
