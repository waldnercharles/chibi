#include "authoring/asset_compiler_host.h"

#include "execution/function.h"
#include "portable_asset_source.h"
#include "support/array.h"
#include "support/asset_value.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public installed-component consumer; the authored generator owns the resource. */
static av_doc *document;
static av_doc *before;
static char *written;
static void cleanup()
{
    free(written);
    written = NULL;
    av_doc_free(before);
    before = NULL;
    av_doc_free(document);
    document = NULL;
    asset_source_release_thread();
}
static asset_compiler_options options()
{
    asset_compiler_options value = *asset_compiler_packaged_options();
    value.cache_directory = "";
    return value;
}
TEST_CASE(asset_source_keeps_the_16_mib_source_bound)
{
    size_t length = 16u * 1024u * 1024u + 1u;
    written = malloc(length + 1);
    REQUIRE(written);
    memset(written, ' ', length);
    written[length] = 0;
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(written, "/project/TooLarge.chibi", &config, &error);
    REQUIRE(!document);
    REQUIRE(!strcmp(error.message, "Asset source must be non-null and at most 16 MiB"));
    return true;
}
static av_doc *read_document(const char *source)
{
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    av_doc *value = asset_source_read(source, "/project/asset/Native.chibi", &config, &error);
    if (!value) {
        fprintf(
            stderr,
            "native DTO Build: %s:%d:%d %s\n",
            error.source,
            error.line,
            error.column,
            error.message
        );
    }
    return value;
}
static void drop_source()
{
    av_doc_free(document);
    document = NULL;
    asset_source_release_thread();
}
/* av_equals intentionally coerces numeric kinds; this contract must not. */
static bool same_values(const av_value *a, const av_value *b)
{
    if (av_get_type(a) != av_get_type(b)) {
        return false;
    }
    switch (av_get_type(a)) {
        case AV_NULL: return true;
        case AV_BOOL: return av_get_bool(a) == av_get_bool(b);
        case AV_SINT: return av_get_sint(a) == av_get_sint(b);
        case AV_UINT: return av_get_uint(a) == av_get_uint(b);
        case AV_REAL: return av_get_num(a) == av_get_num(b);
        case AV_STRING:
            return av_get_len(a) == av_get_len(b) &&
                   !memcmp(av_get_str(a), av_get_str(b), av_get_len(a));
        case AV_ARRAY:
            if (av_arr_size(a) != av_arr_size(b)) {
                return false;
            }
            for (size_t i = 0; i < av_arr_size(a); i++) {
                if (!same_values(av_arr_get(a, i), av_arr_get(b, i))) {
                    return false;
                }
            }
            return true;
        case AV_OBJECT:
            if (av_obj_size(a) != av_obj_size(b)) {
                return false;
            }
            for (size_t i = 0; i < av_obj_size(a); i++) {
                if (!same_values(av_obj_key_at(a, i), av_obj_key_at(b, i)) ||
                    !same_values(av_obj_value_at(a, i), av_obj_value_at(b, i))) {
                    return false;
                }
            }
            return true;
    }
    return false;
}
static av_doc *writer_values()
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_val(doc, root, "signed", av_sint(doc, 1));
    av_obj_add_val(doc, root, "unsigned", av_uint(doc, 1));
    av_obj_add_val(doc, root, "minimum", av_sint(doc, INT64_MIN));
    av_obj_add_val(doc, root, "maximum", av_uint(doc, UINT64_MAX));
    av_obj_add_val(doc, root, "real", av_real(doc, 1.5));
    av_obj_add_val(doc, root, "flag", av_bool(doc, true));
    av_obj_add_val(doc, root, "empty", av_null(doc));
    av_obj_add_val(doc, root, "text", av_str(doc, "caf\xc3\xa9 \xf0\x9f\x99\x82\n\t\"\\{}"));
    av_value *nested = av_obj_add_arr(doc, root, "nested");
    av_arr_append(nested, av_sint(doc, 2));
    av_arr_append(nested, av_uint(doc, 2));
    return doc;
}
static bool cancel_writer(void *user)
{
    (*(int *)user)++;
    return true;
}
TEST_CASE(asset_source_public_scene_build_and_reuse)
{
    static const char
        SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                   "pub Build :: () -> u8[] { return "
                   "data.Encode(data.Scene(\"Empty\", [], [])); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    for (int i = 0; i < 2; i++) {
        document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
        if (!document) {
            fprintf(
                stderr,
                "asset Build: %s:%d:%d %s\n",
                error.source,
                error.line,
                error.column,
                error.message
            );
        }
        REQUIRE(document && !error.message[0]);
        av_value *root = av_doc_root(document);
        REQUIRE(av_is_obj(root));
        const av_value *name = av_obj_get(root, "name");
        REQUIRE(av_is_str(name) && !strcmp(av_get_str(name), "Empty"));
        REQUIRE(av_get_uint(av_obj_get(root, "version")) == 1);
        const av_value *entities = av_obj_get(root, "entities");
        REQUIRE(av_is_arr(entities) && !av_arr_size(entities));
        asset_compiler_cache_stats stats = asset_source_cache_stats();
        REQUIRE(stats.compile_attempts == 1 && stats.preparation_attempts == 1);
        REQUIRE(stats.evaluations == (uint64_t)i + 1 && stats.memory_hits == (uint64_t)i);
        REQUIRE(!behavior_execution_scope_active());
        av_doc_free(document);
        document = NULL;
    }
    return true;
}
TEST_CASE(asset_source_public_byte_libraries_reach_owned_document)
{
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(
        PORTABLE_ASSET_SOURCE,
        "/project/portable-byte-asset/Build.chibi",
        &config,
        &error
    );
    if (!document) {
        fprintf(
            stderr,
            "portable byte asset: %s:%d:%d %s\n",
            error.source,
            error.line,
            error.column,
            error.message
        );
    }
    REQUIRE(document && !error.message[0]);
    asset_source_release_thread();
    REQUIRE(!behavior_execution_scope_active());
    const av_value *root = av_doc_root(document);
    REQUIRE(av_is_obj(root) && av_obj_size(root) == 8);
    static const struct
    {
        const char *name;
        uint64_t value;
    } expected[] = { { "size", 3 },           { "nul", 0 },
                     { "retainedhead", 65 },  { "mutatedhead", 90 },
                     { "bits", 8 },           { "fnv", 0x4F9F2CAB },
                     { "adler", 0x11E60398 }, { "crc", 0xCBF43926 } };
    for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
        const av_value *field = av_obj_get(root, expected[i].name);
        REQUIRE(av_is_uint(field) && av_get_uint(field) == expected[i].value);
    }
    return true;
}
TEST_CASE(asset_source_authored_validation_rejects_duplicate_field)
{
    static const char SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                                 "pub Build :: () -> u8[] { return "
                                 "data.Encode(data.Scene(\"Empty\", [], "
                                 "[data.Field.Named(\"name\", "
                                 "data.Value.Text(\"Other\"))])); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
    fprintf(
        stderr,
        "authored rejection: %s:%d:%d %s\n",
        error.source,
        error.line,
        error.column,
        error.message
    );
    REQUIRE(!document && strstr(error.message, "Duplicate asset object field"));
    REQUIRE(asset_source_cache_stats().evaluations == 1 && !behavior_execution_scope_active());
    return true;
}
TEST_CASE(asset_source_malformed_build_reports_position)
{
    static const char SOURCE[] = "package probe\npub Build :: () -> u8[] { "
                                 "return Missing(); }\n";
    asset_compiler_options config = options();
    asset_compiler_diagnostic error;
    document = asset_source_read(SOURCE, "/project/asset/Probe.chibi", &config, &error);
    fprintf(
        stderr,
        "compile rejection: %s:%d:%d %s\n",
        error.source,
        error.line,
        error.column,
        error.message
    );
    REQUIRE(!document && !strcmp(error.message, "Unknown function 'Missing'"));
    int missing = (int)(strstr(SOURCE, "Missing") - SOURCE);
    int line_start = (int)(strchr(SOURCE, '\n') + 1 - SOURCE);
    REQUIRE(
        error.document == 0 && error.line == 2 &&
        error.column == missing - line_start + 1 && error.start == missing &&
        error.end == missing + (int)strlen("Missing")
    );
    REQUIRE(!asset_source_cache_stats().evaluations && !behavior_execution_scope_active());
    return true;
}
TEST_CASE(asset_source_public_writer_preserves_kinds_and_owned_source)
{
    document = writer_values();
    before = av_doc_clone_value(av_doc_root(document));
    asset_compiler_options config = options();
    config.evaluation_budget = 1; /* The writer uses the compiler facade quota. */
    asset_compiler_diagnostic error;
    written = asset_source_write(av_doc_root(document), &config, &error);
    if (!written) {
        fprintf(stderr, "writer: %s\n", error.message);
    }
    REQUIRE(written && !error.message[0]);
    REQUIRE(same_values(av_doc_root(document), av_doc_root(before)));
    av_doc_free(before);
    before = NULL;
    drop_source();
    REQUIRE(!behavior_execution_scope_active());
    document = read_document(written);
    REQUIRE(document);
    free(written);
    written = NULL;
    asset_source_release_thread();
    before = writer_values();
    REQUIRE(same_values(av_doc_root(document), av_doc_root(before)));
    return true;
}
TEST_CASE(asset_source_writer_and_reader_reject_nul_without_mutation)
{
    static const char NUL[] = { 'a', 0, 'b' };
    static const char *const SOURCES[] = {
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> u8[] { return data.Encode(data.Object("
        "[data.Field.Named(\"value\", data.Value.Text(\"a\\u0000b\"))])); }\n",
        "package probe\nimport data \"game:assets\"\n"
        "pub Build :: () -> u8[] { return data.Encode(data.Object("
        "[data.Field.Named(\"a\\u0000b\", data.Value.Text(\"value\"))])); }\n"
    };
    static const char *const MESSAGES[] = {
        "Asset document text contains unsupported NUL bytes",
        "Asset document key contains unsupported NUL bytes"
    };
    for (int i = 0; i < 2; i++) {
        document = av_doc_new();
        av_value *root = av_obj(document);
        av_doc_set_root(document, root);
        if (i) {
            av_obj_add(root, av_strn(document, NUL, sizeof NUL), av_str(document, "value"));
        } else {
            av_obj_add_val(document, root, "value", av_strn(document, NUL, sizeof NUL));
        }
        before = av_doc_clone_value(root);
        asset_compiler_options config = options();
        asset_compiler_diagnostic error;
        written = asset_source_write(root, &config, &error);
        REQUIRE(!written && !strcmp(error.message, MESSAGES[i]));
        REQUIRE(same_values(root, av_doc_root(before)));
        av_doc_free(before);
        before = NULL;
        drop_source();
        char source[512], original[512];
        snprintf(source, sizeof source, "%s", SOURCES[i]);
        memcpy(original, source, strlen(source) + 1);
        document = asset_source_read(source, "/project/asset/Nul.chibi", &config, &error);
        asset_source_release_thread();
        REQUIRE(!document && !strcmp(error.message, MESSAGES[i]));
        REQUIRE(!strcmp(source, original));
        REQUIRE(!behavior_execution_scope_active());
    }
    return true;
}
TEST_CASE(asset_source_writer_restores_outer_scope_on_budget_and_cancel)
{
    document = writer_values();
    before = av_doc_clone_value(av_doc_root(document));
    asset_compiler_options warm = options();
    asset_compiler_diagnostic error;
    written = asset_source_write(av_doc_root(document), &warm, &error);
    REQUIRE(written);
    free(written);
    written = NULL;
    for (int pure = 0; pure < 2; pure++) {
        for (int cancel = 0; cancel < 2; cancel++) {
            int polls = 0, marker = 0;
            asset_compiler_options config = options();
            config.compile_budget = cancel ? 0 : 1;
            config.cancelled = cancel ? cancel_writer : NULL;
            config.user = &polls;
            behavior_execution_scope outer = { .remaining = 37,
                                               .pure = pure != 0,
                                               .user = &marker };
            behavior_execution_scope_begin(&outer);
            written = asset_source_write(av_doc_root(document), &config, &error);
            bool restored = behavior_execution_scope_active() &&
                            behavior_execution_scope_pure() == (pure != 0) &&
                            outer.remaining == 37 && outer.pure == (pure != 0) &&
                            !outer.cancelled && outer.user == &marker &&
                            outer.active && !outer.previous && !outer.prefer_compiled;
            behavior_execution_scope_end(&outer);
            REQUIRE(!written && strstr(error.message, cancel ? "cancel" : "budget"));
            REQUIRE(restored && !behavior_execution_scope_active());
            REQUIRE(same_values(av_doc_root(document), av_doc_root(before)));
            REQUIRE(!cancel || polls > 0);
        }
    }
    return true;
}
void test_language_asset_compiler_host()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_source_keeps_the_16_mib_source_bound);
    RUN_TEST_CASE(asset_source_public_scene_build_and_reuse);
    RUN_TEST_CASE(asset_source_public_byte_libraries_reach_owned_document);
    RUN_TEST_CASE(asset_source_authored_validation_rejects_duplicate_field);
    RUN_TEST_CASE(asset_source_malformed_build_reports_position);
    RUN_TEST_CASE(asset_source_public_writer_preserves_kinds_and_owned_source);
    RUN_TEST_CASE(asset_source_writer_and_reader_reject_nul_without_mutation);
    RUN_TEST_CASE(asset_source_writer_restores_outer_scope_on_budget_and_cancel);
    pu_clear_setup();
}
