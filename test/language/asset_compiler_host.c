#include "authoring/asset_compiler_host.h"

#include "execution/function.h"
#include "support/asset_value.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

/* Public installed-component consumer; the authored generator owns the resource. */
static av_doc *document;
static void cleanup()
{
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
TEST_CASE(asset_source_public_scene_build_and_reuse)
{
    static const char
        SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                   "pub Build :: () -> byte[] { return "
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
TEST_CASE(asset_source_authored_validation_rejects_duplicate_field)
{
    static const char SOURCE[] = "package probe\nimport data \"game:assets\"\n"
                                 "pub Build :: () -> byte[] { return "
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
    static const char SOURCE[] = "package probe\npub Build :: () -> byte[] { "
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
    REQUIRE(!document && strstr(error.message, "Unknown function 'Missing'"));
    REQUIRE(
        error.document == 0 && error.line == 2 && error.column > 0 &&
        error.end > error.start
    );
    REQUIRE(!asset_source_cache_stats().evaluations && !behavior_execution_scope_active());
    return true;
}
void test_language_asset_compiler_host()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(asset_source_public_scene_build_and_reuse);
    RUN_TEST_CASE(asset_source_authored_validation_rejects_duplicate_field);
    RUN_TEST_CASE(asset_source_malformed_build_reports_position);
    pu_clear_setup();
}
